#include "local_market.h"
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

void LocalMarket::aiBuild() {
    if (ownerWorld != nullptr && fiscalCountry != nullptr)
        return;
    Money unusedConstr = lastConstrProduced - lastConstrUsed;
    if (unusedConstr > Money(0.5)) {
        bld.setAllowAutoConstExpansion(false);
    } else {
        bld.setAllowAutoConstExpansion(true);
    }

    bld.aiBuild(aiProfitThreshold, priceState.prices, buildingWages, maxLabor,
                actualEmploymentRate,
                fiscalCountry != nullptr ? OWNER_GOVERNMENT : OWNER_FINANCE,
                &investmentPool);
    if (fiscalCountry != nullptr) bld.tagGovernmentOrders(fiscalCountry->getTag());
}

bool LocalMarket::payAIExpansionStartup(int typeIndex) {
    const Money startup = expansionStartupCapital(typeIndex);
    if (startup <= Money(0)) return true;
    if (!isfinite(investmentPool) || investmentPool < startup) return false;
    investmentPool -= startup;
    investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
    bld.syncBankLevels(investmentPool);
    return true;
}

std::vector<AIExpansionCandidate>
LocalMarket::getAIExpansionCandidates() const {
    return bld.collectAIExpansionCandidates(
        aiProfitThreshold, priceState.prices, buildingWages, maxLabor,
        actualEmploymentRate);
}

int LocalMarket::placeGovernmentExpansion(int typeIndex, int count,
                                          Money reservedBudgetPerOrder) {
    if (fiscalCountry == nullptr) return 0;
    if (ownerWorld != nullptr) {
        return ownerWorld->queueNationalConstruction(
            fiscalCountry->getId(), provinceId, typeIndex, count, nullptr,
            reservedBudgetPerOrder * Money(count));
    }
    return bld.placeOrders(typeIndex, count, OWNER_GOVERNMENT,
                           fiscalCountry->getTag(), reservedBudgetPerOrder);
}

void LocalMarket::playerBuild(int typeIdx, int count) {
    if (count <= 0 || typeIdx < 0 || typeIdx >= TYPE_COUNT)
        return;

    // A country-owned market submits manual construction through the same
    // national approval path as AI expansion. Reserve the money before the
    if (ownerWorld != nullptr && fiscalCountry != nullptr) {
        ownerWorld->queueNationalConstruction(fiscalCountry->getId(),
                                              provinceId, typeIdx, count, nullptr);
        return;
    }
    // order enters the province queue so two provinces cannot overspend the
    // same treasury in one simulation step.
    if (fiscalCountry != nullptr) {
        if (bld.getTemplates()[typeIdx].isFinancial)
            return;
        const Money constructionPrice = priceState.prices[CONSTR_GOOD_INDEX];
        Money unitBudget = constructionPrice * Money(buildingCost[typeIdx]);
        if (!isfinite(unitBudget) || unitBudget <= Money(0))
            return;
        const Money available = fiscalCountry->getAvailableTreasury();
        const double affordableDouble =
            (available / unitBudget).toDouble();
        if (!std::isfinite(affordableDouble) || affordableDouble < 1.0)
            return;
        count = std::min(count, std::max(0, static_cast<int>(std::min(
            std::floor(affordableDouble + 1.0e-9),
            static_cast<double>(std::numeric_limits<int>::max())))));
        if (count <= 0)
            return;

        const Money reservation = unitBudget * Money(count);
        if (!fiscalCountry->reserveConstructionBudget(reservation))
            return;
        const int actual = bld.placeOrders(
            typeIdx, count, OWNER_GOVERNMENT, fiscalCountry->getTag(),
            unitBudget);
        if (actual < count) {
            fiscalCountry->releaseConstructionBudget(
                unitBudget * Money(count - actual));
        }
        return;
    }

    // A world-owned but politically unassigned province has no payer country.
    // Only standalone LocalMarket instances retain the legacy player path.
    if (ownerWorld != nullptr && !legacyDebugControls) return;
    bld.placePlayerOrder(typeIdx, count, true);
}

void LocalMarket::playerDemolish(int typeIdx, int count) {
    if (!bld.canDemolish(typeIdx, stepCount)) return;
    int actual = std::min(count, bld.getBuildingCounts()[typeIdx]);
    if (actual <= 0) return;
    settleLoansBeforeDemolish(typeIdx, actual);
    bld.demolishBuildings(typeIdx, actual, stepCount, investmentPool);
    reconcileSecurities();
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
