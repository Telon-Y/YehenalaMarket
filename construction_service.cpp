#include "construction_service.h"
#include "construction_completion.h"
#include "construction_queue.h"

#include "country.h"
#include "local_market.h"
#include "province.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

constexpr int kMaxPendingPerType = 50;
constexpr int kMaxTotalFarms = 10000;
constexpr int kMaxCoalMines = 500;
constexpr int kMaxIronMines = 500;
constexpr int kMaxConstructionDepartments = 1000;
constexpr int kMaxGoldMines = 50;
constexpr double kDefaultPriceCeilingFactor = 1.25;

ConstructionRequest NormalizeRequest(ConstructionRequest request) {
    if (request.funding.countryId < 0)
        request.funding.countryId = request.countryId;
    if (request.funding.kind ==
            ConstructionFundingKind::ProvinceInvestmentPool &&
        request.funding.provinceId < 0) {
        request.funding.provinceId = request.targetProvinceId;
    }
    if (request.owner.countryId < 0)
        request.owner.countryId = request.countryId;
    if (request.owner.provinceId < 0)
        request.owner.provinceId = request.targetProvinceId;
    return request;
}

ConstructionProject* FindProject(CountryConstructionState& state,
                                 ConstructionProjectId id) {
    for (ConstructionProject& project : state.projects)
        if (project.id == id) return &project;
    return nullptr;
}

bool ContainsClientRequest(const CountryConstructionState& state,
                           std::uint64_t clientRequestId) {
    if (clientRequestId == 0) return false;
    const auto matches = [clientRequestId](const ConstructionProject& project) {
        return project.clientRequestId == clientRequestId;
    };
    return std::any_of(state.projects.begin(), state.projects.end(), matches) ||
           std::any_of(state.history.begin(), state.history.end(), matches);
}

void Record(CountryConstructionState& state, int cycle,
            ConstructionProjectId projectId, ConstructionLedgerKind kind,
            int provinceId, Money amount = Money(0),
            Money quantity = Money(0)) {
    state.ledger.push_back(
        {cycle, projectId, kind, provinceId, amount, quantity});
}

std::array<int, TYPE_COUNT>& CapacityFor(CountryConstructionState& state,
                                         int provinceId) {
    return state.reservedCapacity[provinceId];
}

const std::array<int, TYPE_COUNT>* FindCapacity(
    const CountryConstructionState& state, int provinceId) {
    const auto it = state.reservedCapacity.find(provinceId);
    return it == state.reservedCapacity.end() ? nullptr : &it->second;
}

int RemainingCapacity(const LocalMarket& market,
                      const CountryConstructionState& state,
                      int provinceId, int typeIndex) {
    const auto* reserved = FindCapacity(state, provinceId);
    const int typeReserved = reserved == nullptr ? 0 : (*reserved)[typeIndex];
    int capacity = std::max(0, kMaxPendingPerType - typeReserved);
    const auto& counts = market.getBuildingCounts();

    switch (typeIndex) {
    case FARM_GRAIN:
    case COTTON: {
        const int farmReserved = reserved == nullptr
            ? 0 : (*reserved)[FARM_GRAIN] + (*reserved)[COTTON];
        const int used = counts[FARM_GRAIN] + counts[COTTON] + farmReserved;
        capacity = std::min(capacity, kMaxTotalFarms - used);
        break;
    }
    case COAL_MINE:
        capacity = std::min(
            capacity, kMaxCoalMines - counts[typeIndex] - typeReserved);
        break;
    case IRON_MINE:
        capacity = std::min(
            capacity, kMaxIronMines - counts[typeIndex] - typeReserved);
        break;
    case GOLD_MINE:
        capacity = std::min(
            capacity, kMaxGoldMines - counts[typeIndex] - typeReserved);
        break;
    case CONST_DEPT:
        capacity = std::min(
            capacity, kMaxConstructionDepartments - counts[typeIndex] -
                          typeReserved);
        break;
    default:
        break;
    }
    const int resourceCap = market.getResourceCap(typeIndex);
    if (resourceCap >= 0) {
        capacity = std::min(
            capacity, resourceCap - counts[typeIndex] - typeReserved);
    }
    return std::max(0, capacity);
}

void ReleaseCapacity(CountryConstructionState& state, int provinceId,
                     int typeIndex, int count, int cycle,
                     ConstructionProjectId projectId) {
    if (count <= 0 || typeIndex < 0 || typeIndex >= TYPE_COUNT) return;
    auto it = state.reservedCapacity.find(provinceId);
    if (it == state.reservedCapacity.end()) return;
    const int released = std::min(count, std::max(0, it->second[typeIndex]));
    it->second[typeIndex] -= released;
    Record(state, cycle, projectId, ConstructionLedgerKind::CapacityReleased,
           provinceId, Money(0), Money(released));
    const bool empty = std::all_of(
        it->second.begin(), it->second.end(),
        [](int value) { return value == 0; });
    if (empty) state.reservedCapacity.erase(it);
}

Money ObservedConstructionPrice(const World& world, const Country& country,
                                Money* lowestPrice = nullptr) {
    Money highest = Money(0);
    Money lowest = Money(0);
    for (const int provinceId : country.getProvinceIds()) {
        const Money price = world.getProvinceById(provinceId)
                                .getLocalMarket()
                                .getPrices()[CONSTR_GOOD_INDEX];
        if (!isfinite(price) || price <= Money(0)) continue;
        highest = std::max(highest, price);
        if (lowest <= Money(0) || price < lowest) lowest = price;
    }
    if (lowestPrice != nullptr) *lowestPrice = lowest;
    return highest;
}

Money AvailableFunding(const World& world, const Country& country,
                       const ConstructionFundingRef& funding) {
    switch (funding.kind) {
    case ConstructionFundingKind::CountryTreasury:
        return country.getAvailableTreasury();
    case ConstructionFundingKind::ProvinceInvestmentPool:
        return world.getProvinceById(funding.provinceId)
            .getLocalMarket().getAvailableInvestmentForConstruction();
    case ConstructionFundingKind::SandboxTreasury:
        return Money(0);
    }
    return Money(0);
}

bool ReserveFunding(World& world, Country& country,
                    const ConstructionFundingRef& funding, Money amount) {
    switch (funding.kind) {
    case ConstructionFundingKind::CountryTreasury:
        return country.reserveConstructionBudget(amount);
    case ConstructionFundingKind::ProvinceInvestmentPool:
        return world.getProvinceById(funding.provinceId)
            .getLocalMarket().reserveInvestmentConstructionBudget(amount);
    case ConstructionFundingKind::SandboxTreasury:
        return false;
    }
    return false;
}

bool SettleFunding(World& world, Country& country,
                   const ConstructionFundingRef& funding, Money amount) {
    switch (funding.kind) {
    case ConstructionFundingKind::CountryTreasury:
        return country.settleConstructionPayment(amount);
    case ConstructionFundingKind::ProvinceInvestmentPool:
        return world.getProvinceById(funding.provinceId)
            .getLocalMarket().settleInvestmentConstructionPayment(amount);
    case ConstructionFundingKind::SandboxTreasury:
        return false;
    }
    return false;
}

void ReleaseFunding(World& world, Country& country,
                    const ConstructionFundingRef& funding, Money amount) {
    if (amount <= Money(0)) return;
    switch (funding.kind) {
    case ConstructionFundingKind::CountryTreasury:
        country.releaseConstructionBudget(amount);
        break;
    case ConstructionFundingKind::ProvinceInvestmentPool:
        world.getProvinceById(funding.provinceId)
            .getLocalMarket().releaseInvestmentConstructionBudget(amount);
        break;
    case ConstructionFundingKind::SandboxTreasury:
        break;
    }
}

int ProjectCycle(const World& world, const ConstructionProject& project) {
    return world.getProvinceById(project.targetProvinceId)
        .getLocalMarket().getStepCount();
}

}  // namespace

ConstructionQuote ConstructionService::quote(
    const ConstructionRequest& sourceRequest) const {
    ConstructionQuote result;
    if (world == nullptr) {
        result.error = ConstructionCommandError::QueueRejected;
        return result;
    }
    const ConstructionRequest request = NormalizeRequest(sourceRequest);
    if (request.quantity <= 0) {
        result.error = ConstructionCommandError::InvalidCount;
        return result;
    }
    if (request.typeIndex < 0 || request.typeIndex >= TYPE_COUNT) {
        result.error = ConstructionCommandError::InvalidType;
        return result;
    }

    const auto countryIt = world->countryIndexById.find(request.countryId);
    if (countryIt == world->countryIndexById.end()) {
        result.error = ConstructionCommandError::UnknownCountry;
        return result;
    }
    const auto provinceIt =
        world->provinceIndexById.find(request.targetProvinceId);
    if (provinceIt == world->provinceIndexById.end()) {
        result.error = ConstructionCommandError::UnknownProvince;
        return result;
    }
    const Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    const Province& province = *world->provinces[static_cast<std::size_t>(
        provinceIt->second)];
    if (province.getCountryId() != request.countryId) {
        result.error = ConstructionCommandError::WrongCountry;
        return result;
    }
    const LocalMarket& market = province.getLocalMarket();
    if (market.getBuildingTemplates()[request.typeIndex].isFinancial) {
        result.error = ConstructionCommandError::FinancialBuilding;
        return result;
    }

    if (request.funding.countryId != request.countryId ||
        (request.funding.kind ==
             ConstructionFundingKind::ProvinceInvestmentPool &&
         request.funding.provinceId != request.targetProvinceId) ||
        request.funding.kind == ConstructionFundingKind::SandboxTreasury) {
        result.error = ConstructionCommandError::InvalidFunding;
        return result;
    }
    const bool governmentProject =
        request.funding.kind == ConstructionFundingKind::CountryTreasury;
    if (request.owner.countryId != request.countryId ||
        request.owner.provinceId != request.targetProvinceId ||
        (governmentProject && request.owner.type != OWNER_GOVERNMENT) ||
        (!governmentProject && request.owner.type != OWNER_FINANCE)) {
        result.error = ConstructionCommandError::InvalidOwner;
        return result;
    }

    result.availableSlots = RemainingCapacity(
        market, country.constructionState, request.targetProvinceId,
        request.typeIndex);
    if (request.quantity > result.availableSlots) {
        result.error = ConstructionCommandError::CapacityReached;
        return result;
    }

    Money lowestPrice = Money(0);
    result.observedUnitPrice =
        ObservedConstructionPrice(*world, country, &lowestPrice);
    if (!isfinite(result.observedUnitPrice) ||
        result.observedUnitPrice <= Money(0) || lowestPrice <= Money(0)) {
        result.error = ConstructionCommandError::InvalidPrice;
        return result;
    }
    result.maximumUnitPrice = request.maximumUnitPrice > Money(0)
        ? request.maximumUnitPrice
        : result.observedUnitPrice * Money(kDefaultPriceCeilingFactor);
    if (!isfinite(result.maximumUnitPrice) ||
        result.maximumUnitPrice < lowestPrice) {
        result.error = ConstructionCommandError::InvalidPrice;
        return result;
    }

    result.constructionPointsPerUnit = Money(buildingCost[request.typeIndex]);
    // Reserve against the project's own accepted ceiling, never against the
    // submit-time observation. Settlement is allowed to pay any provider price
    // up to the ceiling, so a reservation quoted from the observed price can be
    // spent before the work is finished, and the project then blocks forever
    // with a few points left (see tests/construction_progress_tests.cpp).
    const Money reservationUnitPrice = result.maximumUnitPrice;
    result.constructionBudget = result.constructionPointsPerUnit *
        Money(request.quantity) * reservationUnitPrice;
    result.startupBudget = governmentProject
        ? Money(0)
        : expansionStartupCapital(request.typeIndex) * Money(request.quantity);
    result.quotedCycle = market.getStepCount();
    result.taxBudget = governmentProject
        ? Money(0)
        : country.quoteTransactionTax(result.constructionBudget,
                                      result.quotedCycle);
    result.totalBudget = result.constructionBudget + result.startupBudget +
                         result.taxBudget;
    if (!isfinite(result.totalBudget) || result.totalBudget <= Money(0)) {
        result.error = ConstructionCommandError::InvalidPrice;
        return result;
    }

    if (AvailableFunding(*world, country, request.funding) <
        result.totalBudget) {
        result.error = governmentProject
            ? ConstructionCommandError::InsufficientTreasury
            : ConstructionCommandError::InsufficientFunds;
        return result;
    }

    Money industrialCapacity = Money(0);
    for (const int provinceId : country.getProvinceIds()) {
        industrialCapacity += std::max(
            Money(0), world->getProvinceById(provinceId).getLocalMarket()
                          .constructionSustainableCapacityForPlan());
    }
    const Money weeklyCapacity =
        NationalConstructionTotalCapacity(industrialCapacity);
    const Money totalPoints = result.constructionPointsPerUnit *
                              Money(request.quantity);
    const double cycles = (totalPoints /
        std::max(Money(1), weeklyCapacity)).toDouble();
    result.estimatedCycles = std::max(
        1, static_cast<int>(std::ceil(std::max(0.0, cycles))));
    result.acceptedCount = request.quantity;
    return result;
}

ConstructionCommandResult ConstructionService::submit(
    const ConstructionRequest& sourceRequest) {
    ConstructionCommandResult result;
    if (world == nullptr) {
        result.error = ConstructionCommandError::QueueRejected;
        return result;
    }
    const ConstructionRequest request = NormalizeRequest(sourceRequest);
    const ConstructionQuote quoted = quote(request);
    if (!quoted) {
        result.error = quoted.error;
        result.unitBudget = quoted.constructionPointsPerUnit *
                            quoted.observedUnitPrice;
        result.totalBudget = quoted.totalBudget;
        return result;
    }

    Country& country = world->getCountryById(request.countryId);
    CountryConstructionState& state = country.constructionState;
    if (ContainsClientRequest(state, request.clientRequestId)) {
        result.error = ConstructionCommandError::DuplicateRequest;
        return result;
    }
    if (!ReserveFunding(*world, country, request.funding,
                        quoted.totalBudget)) {
        result.error = request.funding.kind ==
                ConstructionFundingKind::CountryTreasury
            ? ConstructionCommandError::InsufficientTreasury
            : ConstructionCommandError::InsufficientFunds;
        return result;
    }

    std::array<int, TYPE_COUNT>& capacity =
        CapacityFor(state, request.targetProvinceId);
    capacity[request.typeIndex] += request.quantity;

    ConstructionProject project;
    project.id = world->nextNationalConstructionProjectId++;
    project.clientRequestId = request.clientRequestId;
    project.payerCountryId = request.countryId;
    project.payerCountryTag = country.getCountryCode();
    project.targetProvinceId = request.targetProvinceId;
    project.typeIndex = request.typeIndex;
    project.quantity = request.quantity;
    project.funding = request.funding;
    project.owner = request.owner;
    project.totalBudget = quoted.totalBudget;
    project.unitPrice = quoted.observedUnitPrice;
    project.maximumUnitPrice = quoted.maximumUnitPrice;
    project.reservedBudget = quoted.constructionBudget + quoted.taxBudget;
    project.startupCapitalPerUnit = request.funding.kind ==
            ConstructionFundingKind::ProvinceInvestmentPool
        ? expansionStartupCapital(request.typeIndex) : Money(0);
    project.reservedStartupCapital = quoted.startupBudget;
    project.totalConstruction = quoted.constructionPointsPerUnit *
                                Money(request.quantity);
    project.remainingConstruction = project.totalConstruction;
    project.priority = request.playerInitiated
        ? std::numeric_limits<int>::max()
        : state.manuallyOrdered ? 0
        : std::min(request.priority, std::numeric_limits<int>::max() - 1);
    project.sequence = state.nextSequence++;
    project.createdStep = quoted.quotedCycle;

    const LocalMarket& target = world->getProvinceById(
        request.targetProvinceId).getLocalMarket();
    project.expectedProfitPriority =
        target.getBuildingCounts()[request.typeIndex] > 0
        ? target.getActualUnitProfits()[request.typeIndex].toDouble()
        : target.getSmoothedProfitRate()[request.typeIndex];

    const ConstructionProjectId projectId = project.id;
    if (!country.enqueueConstructionProject(std::move(project))) {
        capacity[request.typeIndex] -= request.quantity;
        ReleaseFunding(*world, country, request.funding,
                       quoted.totalBudget);
        result.error = ConstructionCommandError::QueueRejected;
        return result;
    }

    Record(state, quoted.quotedCycle, projectId,
           ConstructionLedgerKind::Submitted, request.targetProvinceId);
    Record(state, quoted.quotedCycle, projectId,
           ConstructionLedgerKind::FundsReserved,
           request.targetProvinceId, quoted.totalBudget);
    Record(state, quoted.quotedCycle, projectId,
           ConstructionLedgerKind::CapacityReserved,
           request.targetProvinceId, Money(0), Money(request.quantity));
    result.acceptedCount = request.quantity;
    result.projectId = projectId;
    result.unitBudget = quoted.constructionPointsPerUnit *
                        quoted.observedUnitPrice;
    result.totalBudget = quoted.totalBudget;
    return result;
}

bool ConstructionService::cancel(int countryId,
                                 ConstructionProjectId projectId) {
    if (world == nullptr || projectId == 0) return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    CountryConstructionState& state = country.constructionState;
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr || !project->live()) return false;
    const int cycle = ProjectCycle(*world, *project);
    const Money released = project->reservedBudget +
                           project->reservedStartupCapital;
    ReleaseFunding(*world, country, project->funding, released);
    ReleaseCapacity(state, project->targetProvinceId, project->typeIndex,
                    project->remainingUnits(), cycle, project->id);
    project->reservedBudget = Money(0);
    project->reservedStartupCapital = Money(0);
    project->status = ConstructionProjectStatus::Cancelled;
    project->blockReason = ConstructionBlockReason::None;
    project->finishedStep = cycle;
    Record(state, cycle, projectId, ConstructionLedgerKind::FundsReleased,
           project->targetProvinceId, released);
    Record(state, cycle, projectId, ConstructionLedgerKind::Cancelled,
           project->targetProvinceId);
    country.pruneFinishedConstructionProjects();
    return true;
}

bool ConstructionService::pause(int countryId,
                                ConstructionProjectId projectId) {
    if (world == nullptr) return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    ConstructionProject* project =
        FindProject(country.constructionState, projectId);
    if (project == nullptr || !project->runnable()) return false;
    project->status = ConstructionProjectStatus::Paused;
    Record(country.constructionState, ProjectCycle(*world, *project),
           projectId, ConstructionLedgerKind::Paused,
           project->targetProvinceId);
    return true;
}

bool ConstructionService::resume(int countryId,
                                 ConstructionProjectId projectId) {
    if (world == nullptr) return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    ConstructionProject* project =
        FindProject(country.constructionState, projectId);
    if (project == nullptr ||
        project->status != ConstructionProjectStatus::Paused) return false;
    project->status = ConstructionProjectStatus::Queued;
    project->blockReason = ConstructionBlockReason::None;
    Record(country.constructionState, ProjectCycle(*world, *project),
           projectId, ConstructionLedgerKind::Resumed,
           project->targetProvinceId);
    return true;
}

bool ConstructionService::setPriority(int countryId,
                                      ConstructionProjectId projectId,
                                      int priority) {
    if (world == nullptr) return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    ConstructionProject* project =
        FindProject(country.constructionState, projectId);
    if (project == nullptr || !project->live()) return false;
    project->priority = priority;
    Record(country.constructionState, ProjectCycle(*world, *project),
           projectId, ConstructionLedgerKind::PriorityChanged,
           project->targetProvinceId, Money(priority));
    return true;
}

bool ConstructionService::addBudget(int countryId,
                                    ConstructionProjectId projectId,
                                    Money amount) {
    if (world == nullptr || !isfinite(amount) || amount <= Money(0))
        return false;
    const auto countryIt = world->countryIndexById.find(countryId);
    if (countryIt == world->countryIndexById.end()) return false;
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    ConstructionProject* project =
        FindProject(country.constructionState, projectId);
    if (project == nullptr || !project->live() ||
        !ReserveFunding(*world, country, project->funding, amount)) {
        return false;
    }
    project->reservedBudget += amount;
    project->totalBudget += amount;
    if (project->remainingConstruction > Money(0)) {
        Money effective = project->reservedBudget /
                          project->remainingConstruction;
        if (project->funding.kind ==
            ConstructionFundingKind::ProvinceInvestmentPool) {
            const Money tax = country.quoteTransactionTax(
                Money(1), ProjectCycle(*world, *project));
            effective /= Money(1) + tax;
        }
        project->maximumUnitPrice = std::max(
            project->maximumUnitPrice, effective);
    }
    project->blockReason = ConstructionBlockReason::None;
    Record(country.constructionState, ProjectCycle(*world, *project),
           projectId, ConstructionLedgerKind::BudgetAdded,
           project->targetProvinceId, amount);
    return true;
}

int ConstructionService::invalidateProvince(int countryId, int provinceId) {
    if (world == nullptr) return 0;
    const auto countryIt = world->countryIndexById.find(countryId);
    const auto provinceIt = world->provinceIndexById.find(provinceId);
    if (countryIt == world->countryIndexById.end() ||
        provinceIt == world->provinceIndexById.end()) {
        return 0;
    }
    Country& country = *world->countries[static_cast<std::size_t>(
        countryIt->second)];
    CountryConstructionState& state = country.constructionState;
    int invalidated = 0;
    for (ConstructionProject& project : state.projects) {
        if (!project.live() || project.targetProvinceId != provinceId)
            continue;
        const int cycle = ProjectCycle(*world, project);
        const Money released = project.reservedBudget +
                               project.reservedStartupCapital;
        ReleaseFunding(*world, country, project.funding, released);
        ReleaseCapacity(state, project.targetProvinceId, project.typeIndex,
                        project.remainingUnits(), cycle, project.id);
        project.reservedBudget = Money(0);
        project.reservedStartupCapital = Money(0);
        project.status = ConstructionProjectStatus::Invalidated;
        project.blockReason = ConstructionBlockReason::None;
        project.finishedStep = cycle;
        Record(state, cycle, project.id,
               ConstructionLedgerKind::FundsReleased,
               project.targetProvinceId, released);
        Record(state, cycle, project.id,
               ConstructionLedgerKind::Invalidated,
               project.targetProvinceId);
        ++invalidated;
    }
    if (invalidated > 0) country.pruneFinishedConstructionProjects();
    return invalidated;
}
void ConstructionSystem::preparePlans() {
    if (world == nullptr) return;
    for (const auto& province : world->provinces)
        province->getLocalMarket().setConstructionOutputPlan(
            Money(0), Money(0), false);

    for (const auto& countryPtr : world->countries) {
        struct PlanRef {
            LocalMarket* market = nullptr;
            Money capacity = Money(0);
            Money sustainable = Money(0);
        };
        std::vector<PlanRef> refs;
        Money totalCapacity = Money(0);
        Money totalSustainable = Money(0);
        for (const int provinceId : countryPtr->getProvinceIds()) {
            const auto provinceIt = world->provinceIndexById.find(provinceId);
            if (provinceIt == world->provinceIndexById.end()) continue;
            LocalMarket& market = world->provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            const Money capacity = std::max(
                Money(0), market.constructionCapacityForPlan());
            const Money sustainable = std::min(
                capacity, std::max(
                    Money(0), market.constructionSustainableCapacityForPlan()));
            refs.push_back({&market, capacity, sustainable});
            totalCapacity += capacity;
            totalSustainable += sustainable;
        }
        if (refs.empty()) continue;

        Money queueDemand = Money(0);
        for (const ConstructionProject& project :
             countryPtr->constructionState.projects) {
            if (!project.runnable()) continue;
            const Money price = project.maximumUnitPrice > Money(0)
                ? project.maximumUnitPrice : Money(0.01);
            const Money budgetCapacity = project.reservedBudget / price;
            const Money projectLimit =
                Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
                Money(std::max(1, project.remainingUnits()));
            queueDemand += std::min({project.remainingConstruction,
                                     projectLimit, budgetCapacity});
        }
        const Money requested = queueDemand > Money(0)
            ? queueDemand : Money(COUNTRY_BASE_CONSTRUCTION_CAPACITY);
        const Money inputTarget = std::min(requested, totalCapacity);
        const Money target = std::min(inputTarget, totalSustainable);
        Money remaining = std::max(Money(0), target);
        Money inputRemaining = std::max(Money(0), inputTarget);
        for (std::size_t index = 0; index < refs.size(); ++index) {
            PlanRef& ref = refs[index];
            Money allocation = Money(0);
            if (index + 1 == refs.size()) {
                allocation = remaining;
            } else if (totalSustainable > Money(0)) {
                allocation = target * ref.sustainable / totalSustainable;
            }
            allocation = std::clamp(allocation, Money(0), ref.capacity);

            Money inputAllocation = Money(0);
            if (index + 1 == refs.size()) {
                inputAllocation = inputRemaining;
            } else if (totalCapacity > Money(0)) {
                inputAllocation = inputTarget * ref.capacity / totalCapacity;
            }
            inputAllocation = std::clamp(
                inputAllocation, Money(0), ref.capacity);
            ref.market->setConstructionOutputPlan(
                allocation, inputAllocation, true);
            remaining = std::max(Money(0), remaining - allocation);
            inputRemaining = std::max(
                Money(0), inputRemaining - inputAllocation);
        }
    }
}

void ConstructionSystem::processCycle() {
    if (world == nullptr) return;
    for (const auto& countryPtr : world->countries) {
        Country& country = *countryPtr;
        CountryConstructionState& state = country.constructionState;
        NationalConstructionPoolState& pool = state.pool;
        pool = NationalConstructionPoolState{};

        for (const int provinceId : country.getProvinceIds()) {
            const auto provinceIt = world->provinceIndexById.find(provinceId);
            if (provinceIt == world->provinceIndexById.end()) continue;
            LocalMarket& source = world->provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            const Money capacity = std::max(
                Money(0), source.getAvailableNationalConstructionCapacity());
            const Money available = std::min(
                capacity, std::max(Money(0), source.getLastConstrProduced()));
            ConstructionProviderOffer offer;
            offer.provinceId = provinceId;
            offer.available = available;
            offer.unitPrice = std::max(
                Money(0.01), source.getPrices()[CONSTR_GOOD_INDEX]);
            pool.industrialSources.push_back(offer);
            pool.industrialCapacity += available;
            pool.industrialAvailable += available;
        }
        pool.baseSupplement =
            NationalConstructionBaseSupplement(pool.industrialCapacity);

        ConstructionProviderOffer baseOffer;
        baseOffer.available = pool.baseSupplement;
        baseOffer.unitPrice = std::max(
            Money(0.01), ObservedConstructionPrice(*world, country));
        baseOffer.nationalBase = true;

        struct ProviderRef {
            ConstructionProviderOffer* offer = nullptr;
            LocalMarket* market = nullptr;
        };
        std::vector<ProviderRef> providers;
        providers.reserve(pool.industrialSources.size() + 1);
        for (ConstructionProviderOffer& offer : pool.industrialSources) {
            LocalMarket& market = world->getProvinceById(
                offer.provinceId).getLocalMarket();
            providers.push_back({&offer, &market});
        }
        if (baseOffer.available > Money(0))
            providers.push_back({&baseOffer, nullptr});
        std::stable_sort(
            providers.begin(), providers.end(),
            [](const ProviderRef& left, const ProviderRef& right) {
                if (left.offer->unitPrice != right.offer->unitPrice)
                    return left.offer->unitPrice < right.offer->unitPrice;
                return left.offer->provinceId < right.offer->provinceId;
            });

        std::vector<ConstructionProject*> ordered;
        for (ConstructionProject& project : state.projects)
            if (project.runnable()) ordered.push_back(&project);
        std::stable_sort(
            ordered.begin(), ordered.end(),
            [](const ConstructionProject* left,
               const ConstructionProject* right) {
                return ConstructionQueueOrder{}(*left, *right);
            });

        for (ConstructionProject* project : ordered) {
            auto provinceIt = world->provinceIndexById.find(
                project->targetProvinceId);
            if (provinceIt == world->provinceIndexById.end() ||
                world->provinces[static_cast<std::size_t>(provinceIt->second)]
                        ->getCountryId() != country.getId()) {
                const Money released = project->reservedBudget +
                                       project->reservedStartupCapital;
                ReleaseFunding(*world, country, project->funding, released);
                const int cycle = project->lastSettledStep >= 0
                    ? project->lastSettledStep : project->createdStep;
                ReleaseCapacity(state, project->targetProvinceId,
                                project->typeIndex, project->remainingUnits(),
                                cycle, project->id);
                project->reservedBudget = Money(0);
                project->reservedStartupCapital = Money(0);
                project->status = ConstructionProjectStatus::Invalidated;
                project->blockReason = ConstructionBlockReason::None;
                project->finishedStep = cycle;
                Record(state, cycle, project->id,
                       ConstructionLedgerKind::FundsReleased,
                       project->targetProvinceId, released);
                Record(state, cycle, project->id,
                       ConstructionLedgerKind::Invalidated,
                       project->targetProvinceId);
                continue;
            }

            LocalMarket& target = world->provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            const int cycle = target.getStepCount();
            project->blockReason = ConstructionBlockReason::None;
            const auto installReadyUnits = [&]() {
                return InstallReadyConstructionUnits(*project, target,
                    [&](Money startup) {
                        Record(state, cycle, project->id,
                               ConstructionLedgerKind::StartupCapitalized,
                               project->targetProvinceId, startup);
                    }, [&]() {
                        ReleaseCapacity(state, project->targetProvinceId,
                                        project->typeIndex, 1, cycle, project->id);
                        Record(state, cycle, project->id,
                               ConstructionLedgerKind::UnitCompleted,
                               project->targetProvinceId, Money(0), Money(1));
                    });
            };
            bool fundingFailed = !installReadyUnits();
            Money cycleRemaining = std::min(
                project->remainingConstruction,
                Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
                    Money(std::max(1, project->remainingUnits())));
            bool usedAny = false;
            bool priceRejected = false;

            for (ProviderRef& provider : providers) {
                if (fundingFailed || project->remainingUnits() == 0 ||
                    cycleRemaining <= Money(0) ||
                    project->remainingConstruction <= Money(0)) break;
                ConstructionProviderOffer& offer = *provider.offer;
                if (offer.available <= Money(0)) continue;
                if (offer.unitPrice > project->maximumUnitPrice) {
                    priceRejected = true;
                    continue;
                }
                const Money taxPerUnit = project->funding.kind ==
                        ConstructionFundingKind::ProvinceInvestmentPool
                    ? country.quoteTransactionTax(Money(1), cycle) : Money(0);
                const Money debitPerUnit =
                    offer.unitPrice * (Money(1) + taxPerUnit);
                if (debitPerUnit <= Money(0)) continue;
                Money requested = std::min({
                    offer.available, cycleRemaining,
                    project->remainingConstruction,
                    project->reservedBudget / debitPerUnit});
                if (requested <= Money(0)) continue;

                Money consumed = requested;
                if (provider.market != nullptr)
                    consumed = provider.market->consumeNationalConstruction(
                        requested);
                if (consumed <= Money(0)) continue;
                const Money payment = consumed * offer.unitPrice;
                const Money tax = project->funding.kind ==
                        ConstructionFundingKind::ProvinceInvestmentPool
                    ? country.quoteTransactionTax(payment, cycle) : Money(0);
                const Money debit = payment + tax;
                if (!SettleFunding(*world, country, project->funding, debit)) {
                    if (provider.market != nullptr)
                        provider.market->rollbackNationalConstruction(consumed);
                    fundingFailed = true;
                    break;
                }

                if (tax > Money(0)) {
                    const Money collected =
                        country.collectTransactionTax(payment, cycle);
                    Record(state, cycle, project->id,
                           ConstructionLedgerKind::TaxPayment,
                           project->targetProvinceId, collected);
                }
                offer.available = std::max(
                    Money(0), offer.available - consumed);
                offer.used += consumed;
                offer.revenue += payment;
                if (provider.market != nullptr) {
                    provider.market->recordNationalConstructionSale(
                        consumed, payment);
                    pool.industrialUsed += consumed;
                    pool.industrialAvailable = std::max(
                        Money(0), pool.industrialAvailable - consumed);
                } else {
                    // National base capacity has no provider market, but the
                    // payer's treasury or investment pool was already debited
                    // above. Post the payment to the target province's
                    // construction department so the money re-enters the local
                    // statement instead of vanishing from the money supply.
                    target.recordNationalConstructionBase(consumed, payment);
                    pool.baseUsed += consumed;
                    pool.baseExpenditure += payment;
                    pool.centralProviderRevenue += payment;
                }
                project->reservedBudget = std::max(
                    Money(0), project->reservedBudget - debit);
                project->paidBudget += debit;
                project->remainingConstruction = std::max(
                    Money(0), project->remainingConstruction - consumed);
                project->currentUnitProgress += consumed;
                project->lastSettledStep = cycle;
                project->status = ConstructionProjectStatus::Active;
                cycleRemaining = std::max(
                    Money(0), cycleRemaining - consumed);
                usedAny = true;
                Record(state, cycle, project->id,
                       ConstructionLedgerKind::ProviderPayment,
                       offer.provinceId, payment, consumed);

                fundingFailed = !installReadyUnits();
                if (fundingFailed) break;
            }

            if (fundingFailed) {
                project->blockReason = ConstructionBlockReason::FundingDepleted;
            } else if (!usedAny) {
                const bool hasSupply = std::any_of(
                    providers.begin(), providers.end(),
                    [](const ProviderRef& provider) {
                        return provider.offer->available > Money(0);
                    });
                if (project->reservedBudget <= Money(0))
                    project->blockReason = ConstructionBlockReason::BudgetLimit;
                else if (priceRejected && hasSupply)
                    project->blockReason = ConstructionBlockReason::PriceLimit;
                else if (!hasSupply)
                    project->blockReason = ConstructionBlockReason::NoSupply;
                else
                    project->blockReason = ConstructionBlockReason::NoProvider;
            }

            if (project->completedUnits >= project->quantity) {
                const Money released = project->reservedBudget +
                                       project->reservedStartupCapital;
                ReleaseFunding(*world, country, project->funding, released);
                project->reservedBudget = Money(0);
                project->reservedStartupCapital = Money(0);
                project->remainingConstruction = Money(0);
                project->currentUnitProgress = Money(0);
                project->status = ConstructionProjectStatus::Completed;
                project->blockReason = ConstructionBlockReason::None;
                project->finishedStep = cycle;
                Record(state, cycle, project->id,
                       ConstructionLedgerKind::FundsReleased,
                       project->targetProvinceId, released);
            }
        }
        country.pruneFinishedConstructionProjects();
    }
}
