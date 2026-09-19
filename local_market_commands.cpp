#include "local_market.h"
#include "construction_service.h"
#include "construction_accounting.h"
#include "local_market_internal.h"
#include "world.h"
#include "country.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>

using namespace std;
// Inventory interfaces.

void LocalMarket::addToInventory(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || goodIdx == CONSTR_GOOD_INDEX ||
        goodIdx == TRANSPORT_CAPACITY_GOOD_INDEX ||
        amount <= Money(0)) return;
    InventoryState& state = warehouse.stock(goodIdx);
    state.onHand += amount;
    state.onHand = clamp(state.onHand, Money(0), Money(1e12L));
}

Money LocalMarket::takeFromInventory(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS ||
        goodIdx == CONSTR_GOOD_INDEX ||
        goodIdx == TRANSPORT_CAPACITY_GOOD_INDEX)
        return Money(0);
    InventoryState& state = warehouse.stock(goodIdx);
    Money taken = std::min(state.available(), amount);
    state.onHand -= taken;
    if (state.onHand < Money(0)) state.onHand = Money(0);
    return taken;
}

void LocalMarket::setInventoryForSetup(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(amount)) return;
    if (goodIdx == CONSTR_GOOD_INDEX ||
        goodIdx == TRANSPORT_CAPACITY_GOOD_INDEX) {
        warehouse.stock(goodIdx) = InventoryState{};
        return;
    }
    InventoryState& state = warehouse.stock(goodIdx);
    state.onHand = clamp(amount, Money(0), Money(1e12L));
    state.reserved = std::min(state.reserved, state.onHand);
}

void LocalMarket::addTradeBalance(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(amount)) return;
    tradeBalance[goodIdx] += amount;
}

void LocalMarket::addTradePayment(int goodIdx, Money quantity, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS ||
        !isfinite(quantity) || quantity <= Money(0) ||
        !isfinite(amount) || amount <= Money(0)) {
        return;
    }
    pendingTradeRevenue[goodIdx] += amount;
    addTradeBalance(goodIdx, quantity);
}

void LocalMarket::addLogisticsRevenue(Money railwayRevenue,
                                      Money warehouseProfit) {
    if (isfinite(railwayRevenue) && railwayRevenue > Money(0))
        pendingRailwayRevenue += railwayRevenue;
    if (isfinite(warehouseProfit) && warehouseProfit > Money(0))
        pendingWarehouseProfit += warehouseProfit;
}

bool LocalMarket::tryTradePayment(Money amount) {
    if (!isfinite(amount) || amount <= Money(0) || investmentPool < amount)
        return false;
    investmentPool -= amount;
    bld.syncBankLevels(investmentPool);
    return true;
}

void LocalMarket::refundTradePayment(int goodIdx, Money quantity,
                                     Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS ||
        !isfinite(quantity) || quantity <= Money(0) ||
        !isfinite(amount) || amount <= Money(0)) {
        return;
    }
    investmentPool = clamp(investmentPool + amount, Money(0),
                           INVEST_POOL_MAX_MONEY);
    bld.syncBankLevels(investmentPool);
    addTradeBalance(goodIdx, quantity);
}

// Securities.

void LocalMarket::issueSecurities(int typeIdx, int count, OwnerType owner) {
    if (typeIdx == BANK || typeIdx == FINANCE || typeIdx == CONST_DEPT ||
        typeIdx == INDUSTRIAL_BANK || typeIdx == SAVINGS_BANK) return;
    const auto& bt = bld.getTemplates()[typeIdx];
    if (bt.isFinancial) return;

    Money unitCost = priceState.prices[CONSTR_GOOD_INDEX] * Money(buildingCost[typeIdx]);
    for (int i = 0; i < count; ++i) {
        Security s;
        s.id = nextSecurityId++;
        s.buildingType = typeIdx;
        s.owner = owner;
        s.type = (typeIdx == FARM_GRAIN || typeIdx == COTTON)
            ? SecurityType::FARM_ESTATE : SecurityType::INDUSTRIAL_SHARE;
        s.faceValue = unitCost;
        s.lastTradePrice = unitCost;
        s.active = true;
        s.tradeSequence = (owner == OWNER_FINANCE) ? 2 : (owner == OWNER_INITIAL ? 1 : 0);
        securities.push_back(s);
    }
    syncFinanceLevelFromSecurities();
}

bool LocalMarket::transferSecurities(int typeIdx, int count, OwnerType from, OwnerType to) {
    int fromSeq = static_cast<int>(from);
    int toSeq = static_cast<int>(to);
    if (toSeq != fromSeq + 1) return false;

    int available = 0;
    for (const auto& s : securities)
        if (s.active && s.buildingType == typeIdx && s.owner == from) ++available;
    count = std::min(count, available);
    if (count <= 0) return false;

    Money paid = bld.transferOwnership(typeIdx, count, from, to, investmentPool, classCash);
    if (paid <= Money(0)) return false;
    Money unitPrice = paid / Money(count);
    int transferred = 0;
    for (auto& s : securities) {
        if (!s.active || s.buildingType != typeIdx || s.owner != from) continue;
        s.owner = to;
        s.tradeSequence = toSeq;
        s.lastTradePrice = unitPrice;
        transferred++;
        if (transferred >= count) break;
    }
    syncFinanceLevelFromSecurities();
    return transferred == count;
}

void LocalMarket::syncFinanceLevelFromSecurities() {
    int financeHoldings = 0;
    for (const auto& security : securities)
        if (security.active && security.owner == OWNER_FINANCE) ++financeHoldings;
    bld.setFinanceLevelFromSecurities(financeHoldings);
}

void LocalMarket::reconcileSecurities() {
    for (int t = 0; t < TYPE_COUNT; ++t) {
        const auto& bt = bld.getTemplates()[t];
        if (bt.isFinancial || t == CONST_DEPT) continue;
        for (int owner = 0; owner < OWNER_COUNT; ++owner) {
            int active = 0;
            for (const auto& security : securities)
                if (security.active && security.buildingType == t && security.owner == owner) ++active;
            int desired = bld.getOwnedBuildings()[t][owner];
            if (active < desired) {
                issueSecurities(t, desired - active, static_cast<OwnerType>(owner));
            } else if (active > desired) {
                int remove = active - desired;
                for (auto it = securities.rbegin(); it != securities.rend() && remove > 0; ++it) {
                    if (it->active && it->buildingType == t && it->owner == owner) {
                        it->active = false;
                        --remove;
                    }
                }
            }
        }
    }
    syncFinanceLevelFromSecurities();
}

void LocalMarket::processSecurityMarket() {
    if (stepCount % 52 != 0) return;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        transferSecurities(t, 1, OWNER_INITIAL, OWNER_FINANCE);
        transferSecurities(t, 1, OWNER_GOVERNMENT, OWNER_INITIAL);
    }
}

// Player and AI commands.

bool LocalMarket::reserveInvestmentConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0) ||
        getAvailableInvestmentForConstruction() < amount) {
        return false;
    }
    reservedInvestmentConstructionBudget = clamp(
        reservedInvestmentConstructionBudget + amount,
        Money(0), INVEST_POOL_MAX_MONEY);
    return true;
}

bool LocalMarket::settleInvestmentConstructionPayment(Money amount) {
    if (!isfinite(amount) || amount < Money(0) ||
        !ConstructionFundsCover(reservedInvestmentConstructionBudget, amount) ||
        !ConstructionFundsCover(investmentPool, amount)) {
        return false;
    }
    investmentPool = std::max(Money(0), investmentPool - amount);
    reservedInvestmentConstructionBudget = std::max(
        Money(0), reservedInvestmentConstructionBudget - amount);
    bld.syncBankLevels(investmentPool);
    return true;
}

void LocalMarket::releaseInvestmentConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return;
    reservedInvestmentConstructionBudget = std::max(
        Money(0), reservedInvestmentConstructionBudget - amount);
}

bool LocalMarket::capitalizePrivateConstruction(int typeIndex, Money amount) {
    if (typeIndex < 0 || typeIndex >= TYPE_COUNT || amount < Money(0))
        return false;
    if (!settleInvestmentConstructionPayment(amount)) return false;
    if (amount > Money(0)) bld.addCash(typeIndex, amount);
    return true;
}

bool LocalMarket::reserveSandboxConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0) ||
        getAvailableSandboxConstructionBudget() < amount) {
        return false;
    }
    reservedSandboxConstructionBudget = clamp(
        reservedSandboxConstructionBudget + amount,
        Money(0), CLASS_CASH_MAX_MONEY);
    return true;
}

bool LocalMarket::settleSandboxConstructionPayment(Money amount) {
    if (!isfinite(amount) || amount < Money(0) ||
        !ConstructionFundsCover(reservedSandboxConstructionBudget, amount) ||
        !ConstructionFundsCover(playerCash, amount)) {
        return false;
    }
    playerCash = std::max(Money(0), playerCash - amount);
    reservedSandboxConstructionBudget = std::max(
        Money(0), reservedSandboxConstructionBudget - amount);
    return true;
}

void LocalMarket::releaseSandboxConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return;
    reservedSandboxConstructionBudget = std::max(
        Money(0), reservedSandboxConstructionBudget - amount);
}

std::array<int, TYPE_COUNT>
LocalMarket::getPendingConstructionCounts() const {
    std::array<int, TYPE_COUNT> counts{};
    if (fiscalCountry != nullptr && ownerWorld != nullptr) {
        for (const ConstructionProject& project :
             fiscalCountry->getConstructionProjects()) {
            if (!project.live() || project.targetProvinceId != provinceId ||
                project.typeIndex < 0 || project.typeIndex >= TYPE_COUNT) {
                continue;
            }
            counts[project.typeIndex] += project.remainingUnits();
        }
        return counts;
    }
    for (const ConstructionProject& project :
         standaloneConstructionState.projects) {
        if (!project.live() || project.typeIndex < 0 ||
            project.typeIndex >= TYPE_COUNT) {
            continue;
        }
        counts[project.typeIndex] += project.remainingUnits();
    }
    return counts;
}

Money LocalMarket::getWeeklyPrivateConstructionDemand(
    Money availableConstruction) const {
    Money demand = Money(0);
    const std::vector<ConstructionProject>& projects = fiscalCountry != nullptr
        ? fiscalCountry->getConstructionProjects()
        : standaloneConstructionState.projects;
    for (const ConstructionProject& project : projects) {
        if (!project.runnable() ||
            project.funding.kind !=
                ConstructionFundingKind::ProvinceInvestmentPool ||
            (fiscalCountry != nullptr &&
             project.targetProvinceId != provinceId)) {
            continue;
        }
        const Money used = std::min({
            availableConstruction, project.remainingConstruction,
            Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
                Money(std::max(1, project.remainingUnits()))});
        demand += used;
        availableConstruction = std::max(
            Money(0), availableConstruction - used);
    }
    return demand;
}

void LocalMarket::aiBuild() {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return;
    const int target = provinceId >= 0 ? provinceId : marketId;
    const auto capacityIt =
        standaloneConstructionState.reservedCapacity.find(target);
    const std::array<int, TYPE_COUNT> emptyPending{};
    const auto& pending = capacityIt ==
            standaloneConstructionState.reservedCapacity.end()
        ? emptyPending : capacityIt->second;
    Money totalRemainingConstruction = Money(0);
    for (const ConstructionProject& project :
         standaloneConstructionState.projects) {
        if (project.live())
            totalRemainingConstruction += project.remainingConstruction;
    }
    for (const AIExpansionCandidate& candidate :
         getAIExpansionCandidates(pending, totalRemainingConstruction)) {
        if (candidate.typeIndex < 0 || candidate.typeIndex >= TYPE_COUNT ||
            candidate.maxUnits <= pending[candidate.typeIndex]) {
            continue;
        }
        ConstructionRequest request;
        request.targetProvinceId = target;
        request.typeIndex = candidate.typeIndex;
        request.quantity = 1;
        request.priority = static_cast<int>(std::clamp(
            candidate.priority * 1000.0,
            static_cast<double>(std::numeric_limits<int>::min()),
            static_cast<double>(std::numeric_limits<int>::max())));
        request.funding = {
            ConstructionFundingKind::ProvinceInvestmentPool, -1, target};
        request.owner = {OWNER_FINANCE, -1, target};
        if (StandaloneConstructionService::submit(
                *this, standaloneConstructionState,
                nextStandaloneConstructionProjectId, request)) {
            break;
        }
    }
}

std::vector<AIExpansionCandidate>
LocalMarket::getAIExpansionCandidates(
    const std::array<int, TYPE_COUNT>& pendingCounts,
    Money totalRemainingConstruction) const {
    std::vector<AIExpansionCandidate> candidates =
        bld.collectAIExpansionCandidates(
        aiProfitThreshold, priceState.prices, buildingWages, maxLabor,
        actualEmploymentRate, pendingCounts, totalRemainingConstruction);
    const auto& counts = bld.getBuildingCounts();
    for (AIExpansionCandidate& candidate : candidates) {
        if (candidate.typeIndex < 0 || candidate.typeIndex >= TYPE_COUNT)
            continue;
        const int cap = resourceCaps[candidate.typeIndex];
        if (cap < 0) continue;
        const int available = std::max(
            0, cap - counts[candidate.typeIndex] -
                   pendingCounts[candidate.typeIndex]);
        candidate.maxUnits = std::min(candidate.maxUnits, available);
    }
    candidates.erase(
        std::remove_if(
            candidates.begin(), candidates.end(),
            [](const AIExpansionCandidate& candidate) {
                return candidate.maxUnits <= 0;
            }),
        candidates.end());
    return candidates;
}

ConstructionQuote LocalMarket::quoteConstruction(
    const ConstructionRequest& request) const {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->quoteConstruction(request);
    if (ownerWorld != nullptr && !legacyDebugControls) {
        ConstructionQuote rejected;
        rejected.error = ConstructionCommandError::InvalidFunding;
        return rejected;
    }
    return StandaloneConstructionService::quote(
        *this, standaloneConstructionState, request);
}

ConstructionCommandResult LocalMarket::submitConstruction(
    const ConstructionRequest& request) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->submitConstruction(request);
    if (ownerWorld != nullptr && !legacyDebugControls) {
        ConstructionCommandResult rejected;
        rejected.error = ConstructionCommandError::InvalidFunding;
        return rejected;
    }
    return StandaloneConstructionService::submit(
        *this, standaloneConstructionState,
        nextStandaloneConstructionProjectId, request);
}
bool LocalMarket::cancelConstructionProject(
    ConstructionProjectId projectId) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->cancelNationalConstructionProject(
            fiscalCountry->getId(), projectId);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::cancel(
        *this, standaloneConstructionState, projectId);
}

bool LocalMarket::pauseConstructionProject(
    ConstructionProjectId projectId) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->pauseConstructionProject(
            fiscalCountry->getId(), projectId);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::pause(
        *this, standaloneConstructionState, projectId);
}

bool LocalMarket::resumeConstructionProject(
    ConstructionProjectId projectId) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->resumeConstructionProject(
            fiscalCountry->getId(), projectId);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::resume(
        *this, standaloneConstructionState, projectId);
}

bool LocalMarket::setConstructionProjectPriority(
    ConstructionProjectId projectId, int priority) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->setConstructionProjectPriority(
            fiscalCountry->getId(), projectId, priority);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::setPriority(
        *this, standaloneConstructionState, projectId, priority);
}

bool LocalMarket::moveConstructionProject(
    ConstructionProjectId projectId, bool up, bool toEdge) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->moveConstructionProject(
            fiscalCountry->getId(), projectId, up, toEdge);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::move(
        *this, standaloneConstructionState, projectId, up, toEdge);
}

bool LocalMarket::addConstructionProjectBudget(
    ConstructionProjectId projectId, Money amount) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return ownerWorld->addConstructionProjectBudget(
            fiscalCountry->getId(), projectId, amount);
    if (ownerWorld != nullptr && !legacyDebugControls) return false;
    return StandaloneConstructionService::addBudget(
        *this, standaloneConstructionState, projectId, amount);
}

const std::vector<ConstructionProject>&
LocalMarket::getConstructionQueue() const {
    return fiscalCountry != nullptr
        ? fiscalCountry->getConstructionProjects()
        : standaloneConstructionState.projects;
}

const std::vector<ConstructionProject>&
LocalMarket::getConstructionHistory() const {
    return fiscalCountry != nullptr
        ? fiscalCountry->getConstructionHistory()
        : standaloneConstructionState.history;
}

const std::vector<ConstructionLedgerEntry>&
LocalMarket::getConstructionLedger() const {
    return fiscalCountry != nullptr
        ? fiscalCountry->getConstructionLedger()
        : standaloneConstructionState.ledger;
}

ConstructionCommandResult LocalMarket::playerBuildCommand(
    int typeIdx, int count) {
    if (count <= 0 || typeIdx < 0 || typeIdx >= TYPE_COUNT) {
        ConstructionCommandResult rejected;
        rejected.error = count <= 0
            ? ConstructionCommandError::InvalidCount
            : ConstructionCommandError::InvalidType;
        return rejected;
    }

    // Manual orders enter the national queue synchronously. AI profitability
    // and expansion policy are only used when generating AI candidates.
    if (ownerWorld != nullptr && fiscalCountry != nullptr) {
        return ownerWorld->queueNationalConstructionCommand(
            fiscalCountry->getId(), provinceId, typeIdx, count);
    }
    if (ownerWorld != nullptr && !legacyDebugControls) {
        ConstructionCommandResult rejected;
        rejected.error = ConstructionCommandError::InvalidFunding;
        return rejected;
    }
    ConstructionRequest request;
    request.playerInitiated = true;
    request.targetProvinceId = provinceId >= 0 ? provinceId : marketId;
    request.typeIndex = typeIdx;
    request.quantity = count;
    request.funding = {
        ConstructionFundingKind::SandboxTreasury, -1,
        request.targetProvinceId};
    request.owner = {
        OWNER_GOVERNMENT, -1, request.targetProvinceId};
    return StandaloneConstructionService::submit(
        *this, standaloneConstructionState,
        nextStandaloneConstructionProjectId, request);
}

void LocalMarket::playerBuild(int typeIdx, int count) {
    (void)playerBuildCommand(typeIdx, count);
}

bool LocalMarket::canPlayerDemolish(int typeIdx) const {
    if (ownerWorld != nullptr && fiscalCountry == nullptr &&
        !legacyDebugControls) return false;
    return bld.canDemolish(typeIdx, stepCount);
}

int LocalMarket::playerDemolish(int typeIdx, int count) {
    if (count <= 0 || !canPlayerDemolish(typeIdx)) return 0;
    const int actual = std::min({count, bld.getBuildingCounts()[typeIdx],
        bld.getOwnedBuildings()[typeIdx][OWNER_GOVERNMENT]});
    if (actual <= 0) return 0;
    settleLoansBeforeDemolish(typeIdx, actual);
    const int removed = bld.demolishBuildings(
        typeIdx, actual, stepCount, investmentPool);
    reconcileSecurities();
    syncWarehouseProducers();
    syncBuildingInputPolicies();
    return removed;
}

bool LocalMarket::performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to) {
    return bld.transferOwnership(typeIdx, count, from, to, investmentPool, classCash) != Money(0);
}

void LocalMarket::settleLoansBeforeDemolish(int typeIdx, int removeCount) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || removeCount <= 0 ||
        buildingLoanCount[typeIdx] <= 0) return;
    int currentBuildings = bld.getBuildingCounts()[typeIdx];
    if (currentBuildings <= 0) return;
    int loansToSettle = (removeCount * buildingLoanCount[typeIdx]) / currentBuildings;
    loansToSettle = std::max(loansToSettle, 0);
    loansToSettle = std::min(loansToSettle, buildingLoanCount[typeIdx]);
    if (loansToSettle <= 0) return;

    Money repayAmount = Money(loansToSettle * BANK_LOAN_UNIT_VALUE);
    Money cash = bld.getCashPools()[typeIdx];
    Money usedCash = std::min(std::max(Money(0), cash), repayAmount);
    bld.addCash(typeIdx, -usedCash);
    bld.addCash(INDUSTRIAL_BANK, usedCash);
    buildingLoanCount[typeIdx] -= loansToSettle;
    loanBalance[typeIdx] -= repayAmount;
    recalculateTotalDebt();
}
