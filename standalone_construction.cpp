#include "construction_service.h"
#include "construction_completion.h"
#include "construction_queue.h"

#include "local_market.h"

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

ConstructionProject* FindProject(CountryConstructionState& state,
                                 ConstructionProjectId id) {
    for (ConstructionProject& project : state.projects)
        if (project.id == id) return &project;
    return nullptr;
}

void Record(CountryConstructionState& state, int cycle,
            ConstructionProjectId projectId, ConstructionLedgerKind kind,
            int provinceId, Money amount = Money(0),
            Money quantity = Money(0)) {
    state.ledger.push_back(
        {cycle, projectId, kind, provinceId, amount, quantity});
}

int TargetId(const LocalMarket& market) {
    return market.getProvinceId() >= 0
        ? market.getProvinceId() : market.getMarketId();
}

ConstructionRequest NormalizeRequest(const LocalMarket& market,
                                     ConstructionRequest request) {
    const int targetId = TargetId(market);
    if (request.targetProvinceId < 0)
        request.targetProvinceId = targetId;
    if (request.funding.provinceId < 0)
        request.funding.provinceId = request.targetProvinceId;
    if (request.owner.provinceId < 0)
        request.owner.provinceId = request.targetProvinceId;
    return request;
}

int RemainingCapacity(const LocalMarket& market,
                      const CountryConstructionState& state,
                      int targetId, int typeIndex) {
    const auto it = state.reservedCapacity.find(targetId);
    const auto* reserved = it == state.reservedCapacity.end()
        ? nullptr : &it->second;
    const int typeReserved = reserved == nullptr ? 0 : (*reserved)[typeIndex];
    int capacity = std::max(0, kMaxPendingPerType - typeReserved);
    const auto& counts = market.getBuildingCounts();
    switch (typeIndex) {
    case FARM_GRAIN:
    case COTTON: {
        const int farms = counts[FARM_GRAIN] + counts[COTTON] +
            (reserved == nullptr
                ? 0 : (*reserved)[FARM_GRAIN] + (*reserved)[COTTON]);
        capacity = std::min(capacity, kMaxTotalFarms - farms);
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
            capacity, kMaxConstructionDepartments -
                          counts[typeIndex] - typeReserved);
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

void ReleaseCapacity(CountryConstructionState& state, int targetId,
                     int typeIndex, int count, int cycle,
                     ConstructionProjectId projectId) {
    auto it = state.reservedCapacity.find(targetId);
    if (it == state.reservedCapacity.end() || count <= 0) return;
    const int released = std::min(
        count, std::max(0, it->second[typeIndex]));
    it->second[typeIndex] -= released;
    Record(state, cycle, projectId,
           ConstructionLedgerKind::CapacityReleased,
           targetId, Money(0), Money(released));
    if (std::all_of(it->second.begin(), it->second.end(),
                    [](int value) { return value == 0; })) {
        state.reservedCapacity.erase(it);
    }
}

void ArchiveTerminal(CountryConstructionState& state) {
    std::vector<ConstructionProject> live;
    live.reserve(state.projects.size());
    for (ConstructionProject& project : state.projects) {
        if (project.live()) live.push_back(std::move(project));
        else state.history.push_back(std::move(project));
    }
    state.projects = std::move(live);
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

}  // namespace

ConstructionQuote StandaloneConstructionService::quote(
    const LocalMarket& market, const CountryConstructionState& state,
    const ConstructionRequest& sourceRequest) {
    ConstructionQuote result;
    const ConstructionRequest request = NormalizeRequest(market, sourceRequest);
    if (request.quantity <= 0) {
        result.error = ConstructionCommandError::InvalidCount;
        return result;
    }
    if (request.typeIndex < 0 || request.typeIndex >= TYPE_COUNT) {
        result.error = ConstructionCommandError::InvalidType;
        return result;
    }
    if (market.getBuildingTemplates()[request.typeIndex].isFinancial) {
        result.error = ConstructionCommandError::FinancialBuilding;
        return result;
    }
    if (request.targetProvinceId != TargetId(market)) {
        result.error = ConstructionCommandError::UnknownProvince;
        return result;
    }
    const bool privateProject = request.funding.kind ==
        ConstructionFundingKind::ProvinceInvestmentPool;
    if ((!privateProject && request.funding.kind !=
             ConstructionFundingKind::SandboxTreasury) ||
        request.countryId >= 0 || request.funding.countryId >= 0 ||
        request.funding.provinceId != request.targetProvinceId) {
        result.error = ConstructionCommandError::InvalidFunding;
        return result;
    }
    if (request.owner.countryId >= 0 ||
        request.owner.provinceId != request.targetProvinceId ||
        (privateProject && request.owner.type != OWNER_FINANCE) ||
        (!privateProject && request.owner.type != OWNER_GOVERNMENT)) {
        result.error = ConstructionCommandError::InvalidOwner;
        return result;
    }

    result.availableSlots = RemainingCapacity(
        market, state, request.targetProvinceId, request.typeIndex);
    if (request.quantity > result.availableSlots) {
        result.error = ConstructionCommandError::CapacityReached;
        return result;
    }
    result.observedUnitPrice = market.getPrices()[CONSTR_GOOD_INDEX];
    const bool explicitPriceLimit = request.maximumUnitPrice > Money(0);
    result.maximumUnitPrice = explicitPriceLimit
        ? request.maximumUnitPrice
        : result.observedUnitPrice * Money(kDefaultPriceCeilingFactor);
    if (!isfinite(result.observedUnitPrice) ||
        result.observedUnitPrice <= Money(0) ||
        !isfinite(result.maximumUnitPrice) ||
        result.maximumUnitPrice < result.observedUnitPrice) {
        result.error = ConstructionCommandError::InvalidPrice;
        return result;
    }
    result.constructionPointsPerUnit = Money(buildingCost[request.typeIndex]);
    // Reserve the full quantity at the accepted ceiling, exactly like the
    // national queue: a budget that only covers the observed price strands the
    // project as soon as a provider charges more than that observation.
    result.constructionBudget = result.constructionPointsPerUnit *
        Money(request.quantity) * result.maximumUnitPrice;
    result.startupBudget = privateProject
        ? expansionStartupCapital(request.typeIndex) * Money(request.quantity)
        : Money(0);
    result.totalBudget = result.constructionBudget + result.startupBudget;
    result.quotedCycle = market.getStepCount();
    const Money available = privateProject
        ? market.getAvailableInvestmentForConstruction()
        : market.getAvailableSandboxConstructionBudget();
    if (available < result.totalBudget) {
        result.error = privateProject
            ? ConstructionCommandError::InsufficientFunds
            : ConstructionCommandError::InsufficientTreasury;
        return result;
    }
    const Money capacity = std::max(
        Money(1), market.constructionSustainableCapacityForPlan());
    result.estimatedCycles = std::max(
        1, static_cast<int>(std::ceil(
            (result.constructionPointsPerUnit * Money(request.quantity) /
             capacity).toDouble())));
    result.acceptedCount = request.quantity;
    return result;
}

ConstructionCommandResult StandaloneConstructionService::submit(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId& nextProjectId,
    const ConstructionRequest& sourceRequest) {
    ConstructionCommandResult result;
    const ConstructionRequest request = NormalizeRequest(market, sourceRequest);
    const ConstructionQuote quoted = quote(market, state, request);
    if (!quoted) {
        result.error = quoted.error;
        return result;
    }
    if (ContainsClientRequest(state, request.clientRequestId)) {
        result.error = ConstructionCommandError::DuplicateRequest;
        return result;
    }
    const bool privateProject = request.funding.kind ==
        ConstructionFundingKind::ProvinceInvestmentPool;
    const bool reserved = privateProject
        ? market.reserveInvestmentConstructionBudget(quoted.totalBudget)
        : market.reserveSandboxConstructionBudget(quoted.totalBudget);
    if (!reserved) {
        result.error = privateProject
            ? ConstructionCommandError::InsufficientFunds
            : ConstructionCommandError::InsufficientTreasury;
        return result;
    }

    state.reservedCapacity[request.targetProvinceId][request.typeIndex] +=
        request.quantity;
    ConstructionProject project;
    project.id = nextProjectId++;
    if (project.id == 0) project.id = nextProjectId++;
    project.clientRequestId = request.clientRequestId;
    project.payerCountryId = -1;
    project.payerCountryTag = "SANDBOX";
    project.targetProvinceId = request.targetProvinceId;
    project.typeIndex = request.typeIndex;
    project.quantity = request.quantity;
    project.funding = request.funding;
    project.owner = request.owner;
    project.totalBudget = quoted.totalBudget;
    project.unitPrice = quoted.observedUnitPrice;
    project.maximumUnitPrice = quoted.maximumUnitPrice;
    project.reservedBudget = quoted.constructionBudget;
    project.startupCapitalPerUnit = privateProject
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
    project.expectedProfitPriority =
        market.getBuildingCounts()[request.typeIndex] > 0
        ? market.getActualUnitProfits()[request.typeIndex].toDouble()
        : market.getSmoothedProfitRate()[request.typeIndex];
    const ConstructionProjectId id = project.id;
    state.projects.push_back(std::move(project));
    Record(state, quoted.quotedCycle, id,
           ConstructionLedgerKind::Submitted, request.targetProvinceId);
    Record(state, quoted.quotedCycle, id,
           ConstructionLedgerKind::FundsReserved,
           request.targetProvinceId, quoted.totalBudget);
    Record(state, quoted.quotedCycle, id,
           ConstructionLedgerKind::CapacityReserved,
           request.targetProvinceId, Money(0), Money(request.quantity));
    result.acceptedCount = request.quantity;
    result.projectId = id;
    result.unitBudget = quoted.constructionPointsPerUnit *
                        quoted.observedUnitPrice;
    result.totalBudget = quoted.totalBudget;
    return result;
}

bool StandaloneConstructionService::cancel(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId) {
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr || !project->live()) return false;
    const Money released = project->reservedBudget +
                           project->reservedStartupCapital;
    if (project->funding.kind ==
        ConstructionFundingKind::ProvinceInvestmentPool) {
        market.releaseInvestmentConstructionBudget(released);
    } else {
        market.releaseSandboxConstructionBudget(released);
    }
    ReleaseCapacity(state, project->targetProvinceId, project->typeIndex,
                    project->remainingUnits(), market.getStepCount(),
                    project->id);
    project->reservedBudget = Money(0);
    project->reservedStartupCapital = Money(0);
    project->status = ConstructionProjectStatus::Cancelled;
    project->blockReason = ConstructionBlockReason::None;
    project->finishedStep = market.getStepCount();
    Record(state, market.getStepCount(), project->id,
           ConstructionLedgerKind::FundsReleased,
           project->targetProvinceId, released);
    Record(state, market.getStepCount(), project->id,
           ConstructionLedgerKind::Cancelled, project->targetProvinceId);
    ArchiveTerminal(state);
    return true;
}

bool StandaloneConstructionService::pause(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId) {
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr || !project->runnable()) return false;
    project->status = ConstructionProjectStatus::Paused;
    Record(state, market.getStepCount(), projectId,
           ConstructionLedgerKind::Paused, project->targetProvinceId);
    return true;
}

bool StandaloneConstructionService::resume(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId) {
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr ||
        project->status != ConstructionProjectStatus::Paused) return false;
    project->status = ConstructionProjectStatus::Queued;
    project->blockReason = ConstructionBlockReason::None;
    Record(state, market.getStepCount(), projectId,
           ConstructionLedgerKind::Resumed, project->targetProvinceId);
    return true;
}

bool StandaloneConstructionService::setPriority(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId, int priority) {
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr || !project->live()) return false;
    project->priority = priority;
    Record(state, market.getStepCount(), projectId,
           ConstructionLedgerKind::PriorityChanged,
           project->targetProvinceId, Money(priority));
    return true;
}

bool StandaloneConstructionService::addBudget(
    LocalMarket& market, CountryConstructionState& state,
    ConstructionProjectId projectId, Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return false;
    ConstructionProject* project = FindProject(state, projectId);
    if (project == nullptr || !project->live()) return false;
    const bool privateProject = project->funding.kind ==
        ConstructionFundingKind::ProvinceInvestmentPool;
    const bool reserved = privateProject
        ? market.reserveInvestmentConstructionBudget(amount)
        : market.reserveSandboxConstructionBudget(amount);
    if (!reserved) return false;
    project->reservedBudget += amount;
    project->totalBudget += amount;
    if (project->remainingConstruction > Money(0)) {
        project->maximumUnitPrice = std::max(
            project->maximumUnitPrice,
            project->reservedBudget / project->remainingConstruction);
    }
    project->blockReason = ConstructionBlockReason::None;
    Record(state, market.getStepCount(), projectId,
           ConstructionLedgerKind::BudgetAdded,
           project->targetProvinceId, amount);
    return true;
}

int StandaloneConstructionService::invalidateAll(
    LocalMarket& market, CountryConstructionState& state) {
    int invalidated = 0;
    const int cycle = market.getStepCount();
    for (ConstructionProject& project : state.projects) {
        if (!project.live()) continue;
        const Money released = project.reservedBudget +
                               project.reservedStartupCapital;
        if (project.funding.kind ==
            ConstructionFundingKind::ProvinceInvestmentPool) {
            market.releaseInvestmentConstructionBudget(released);
        } else {
            market.releaseSandboxConstructionBudget(released);
        }
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
    ArchiveTerminal(state);
    return invalidated;
}

void StandaloneConstructionService::processCycle(
    LocalMarket& market, CountryConstructionState& state,
    Money availableConstruction, Money unitPrice,
    Money& constructionUsed, Money& constructionRevenue) {
    constructionUsed = Money(0);
    constructionRevenue = Money(0);
    if (!isfinite(availableConstruction)) availableConstruction = Money(0);

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
        project->blockReason = ConstructionBlockReason::None;
        const auto installReadyUnits = [&]() {
            return InstallReadyConstructionUnits(*project, market,
                [&](Money startup) {
                    Record(state, market.getStepCount(), project->id,
                           ConstructionLedgerKind::StartupCapitalized,
                           project->targetProvinceId, startup);
                }, [&]() {
                    ReleaseCapacity(state, project->targetProvinceId,
                                    project->typeIndex, 1, market.getStepCount(),
                                    project->id);
                    Record(state, market.getStepCount(), project->id,
                           ConstructionLedgerKind::UnitCompleted,
                           project->targetProvinceId, Money(0), Money(1));
                });
        };
        const auto finishProject = [&]() {
            if (project->completedUnits < project->quantity) return false;
            const Money released = project->reservedBudget +
                                   project->reservedStartupCapital;
            if (project->funding.kind ==
                ConstructionFundingKind::ProvinceInvestmentPool) {
                market.releaseInvestmentConstructionBudget(released);
            } else {
                market.releaseSandboxConstructionBudget(released);
            }
            project->reservedBudget = Money(0);
            project->reservedStartupCapital = Money(0);
            project->remainingConstruction = Money(0);
            project->currentUnitProgress = Money(0);
            project->status = ConstructionProjectStatus::Completed;
            project->blockReason = ConstructionBlockReason::None;
            project->finishedStep = market.getStepCount();
            Record(state, market.getStepCount(), project->id,
                   ConstructionLedgerKind::FundsReleased,
                   project->targetProvinceId, released);
            return true;
        };
        if (!installReadyUnits() || finishProject()) continue;
        if (availableConstruction <= Money(0)) {
            project->blockReason = ConstructionBlockReason::NoSupply;
            continue;
        }
        if (!isfinite(unitPrice) || unitPrice <= Money(0)) {
            project->blockReason = ConstructionBlockReason::NoProvider;
            continue;
        }
        if (unitPrice > project->maximumUnitPrice) {
            project->blockReason = ConstructionBlockReason::PriceLimit;
            continue;
        }
        const Money cycleLimit =
            Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
            Money(std::max(1, project->remainingUnits()));
        const Money used = std::min({
            availableConstruction, project->remainingConstruction,
            cycleLimit, project->reservedBudget / unitPrice});
        if (used <= Money(0)) {
            project->blockReason = ConstructionBlockReason::BudgetLimit;
            continue;
        }
        const Money payment = used * unitPrice;
        const bool paid = project->funding.kind ==
                ConstructionFundingKind::ProvinceInvestmentPool
            ? market.settleInvestmentConstructionPayment(payment)
            : market.settleSandboxConstructionPayment(payment);
        if (!paid) {
            project->blockReason = ConstructionBlockReason::FundingDepleted;
            continue;
        }
        project->reservedBudget = std::max(
            Money(0), project->reservedBudget - payment);
        project->paidBudget += payment;
        project->remainingConstruction = std::max(
            Money(0), project->remainingConstruction - used);
        project->currentUnitProgress += used;
        project->status = ConstructionProjectStatus::Active;
        project->lastSettledStep = market.getStepCount();
        availableConstruction -= used;
        constructionUsed += used;
        constructionRevenue += payment;
        Record(state, market.getStepCount(), project->id,
               ConstructionLedgerKind::ProviderPayment,
               project->targetProvinceId, payment, used);

        installReadyUnits();
        finishProject();
    }
    ArchiveTerminal(state);
}
