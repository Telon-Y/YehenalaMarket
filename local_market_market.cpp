// ==================== local_market_market.cpp ====================
// Price level, construction settlement, and income distribution.
#include "local_market.h"
#include "construction_service.h"
#include "country.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace std;

Money LocalMarket::fiscalTreasuryShare(bool initial) const {
    if (fiscalCountry == nullptr) return Money(0);
    const std::size_t memberCount = std::max<std::size_t>(
        1, fiscalCountry->getProvinceIds().size());
    const Money treasury = initial ? fiscalCountry->getInitialTreasury()
                                   : fiscalCountry->getTreasury();
    return treasury / Money(static_cast<int>(memberCount));
}

Money LocalMarket::moneySupplyBaseline() const {
    return std::max(Money(0), initialTotalMoneySupply +
        fiscalTreasuryShare(true));
}

void LocalMarket::processPriceLevelUpdate() {
    const Money baseline = moneySupplyBaseline();
    if (stepCount <= 1 || baseline <= Money(0)) return;

    Money activeMoney = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t)
        activeMoney += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) activeMoney += classCash[c];
    activeMoney += investmentPool;
    if (fiscalCountry == nullptr) activeMoney += playerCash;
    activeMoney += fiscalTreasuryShare(false);
    if (!isfinite(activeMoney)) activeMoney = baseline;

    Money moneyRatio = activeMoney / baseline;
    updateReferencePrices(priceState, moneyRatio);
}

void LocalMarket::processConstruction(Money constrPrice, Money availConstr,
                                      Money& soldConstr, Money& constrRevenue) {
    if (ownerWorld != nullptr && fiscalCountry != nullptr) {
        soldConstr = Money(0);
        constrRevenue = Money(0);
        return;
    }
    StandaloneConstructionService::processCycle(
        *this, standaloneConstructionState, availConstr, constrPrice,
        soldConstr, constrRevenue);
}

void LocalMarket::processRevenueAllocation(
    const std::array<Money, NUM_GOODS>& formalOut,
    const std::array<Money, NUM_GOODS>& consumerSpending,
    const std::array<Money, NUM_GOODS>& intermediatePayment,
    const Money& constrRevenue,
    const std::array<double, TYPE_COUNT>& activityRate,
    std::array<Money, TYPE_COUNT>& revenueByBuilding) {
    (void)formalOut;
    (void)activityRate;

    std::array<Money, NUM_GOODS> totalSalesValue;
    for (int g = 0; g < NUM_GOODS; ++g) {
        totalSalesValue[g] = consumerSpending[g] +
            intermediatePayment[g] + pendingTradeRevenue[g];
        pendingTradeRevenue[g] = Money(0);
    }
    const int constrIdx = CONSTR_GOOD_INDEX;
    totalSalesValue[constrIdx] += constrRevenue;

    if (pendingRailwayRevenue > Money(0) &&
        bld.getBuildingCounts()[RAILWAY] > 0) {
        revenueByBuilding[RAILWAY] += pendingRailwayRevenue;
    }
    pendingRailwayRevenue = Money(0);

    Money merchantRevenue = pendingWarehouseProfit;
    pendingWarehouseProfit = Money(0);
    for (int g = 0; g < NUM_GOODS; ++g) {
        if (totalSalesValue[g] <= Money(0)) continue;
        if (g == constrIdx) {
            // Construction power settles before this point: the buyer's payment
            // was already credited to the construction department's cash in
            // recordNationalConstructionSale/Base. Posting it to the statement
            // here only feeds GDP and profit reporting - the credit loop below
            // skips the department - so it must never be re-routed to merchant
            // revenue, which would credit the same money twice.
            revenueByBuilding[CONST_DEPT] += totalSalesValue[g];
            continue;
        }

        int producerLevels = 0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            const auto& bt = bld.getTemplates()[t];
            if (bt.outputGood == g)
                producerLevels += bld.getBuildingCounts()[t];
        }
        if (producerLevels <= 0) {
            merchantRevenue += totalSalesValue[g];
            continue;
        }
        for (int t = 0; t < TYPE_COUNT; ++t) {
            const int levels = bld.getBuildingCounts()[t];
            if (levels <= 0 || bld.getTemplates()[t].outputGood != g)
                continue;
            revenueByBuilding[t] += totalSalesValue[g] *
                Money(levels) / Money(producerLevels);
        }
    }
    if (merchantRevenue > Money(0)) {
        investmentPool = clamp(investmentPool + merchantRevenue,
                               Money(0), INVEST_POOL_MAX_MONEY);
        bld.syncBankLevels(investmentPool);
    }
}
