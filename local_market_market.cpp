// ==================== local_market_market.cpp ====================
// 价格水平、建造过程、收入分配
#include "local_market.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>

using namespace std;

void LocalMarket::processPriceLevelUpdate() {
    if (stepCount <= 1 || initialTotalMoneySupply <= Money(0)) return;

    Money activeMoney = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t)
        activeMoney += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) activeMoney += classCash[c];
    activeMoney += investmentPool;
    activeMoney += playerCash;
    if (!isfinite(activeMoney)) activeMoney = initialTotalMoneySupply;

    Money moneyRatio = activeMoney / initialTotalMoneySupply;
    updateReferencePrices(priceState, moneyRatio);
}

void LocalMarket::processConstruction(Money constrPrice, Money availConstr,
                                      Money& soldConstr, Money& constrRevenue) {
    ConstructionSettlement settlement = bld.processConstruction(
        availConstr, constrPrice, investmentPool, playerCash);
    soldConstr = settlement.totalUsed;
    constrRevenue = settlement.privatePayment;
}

void LocalMarket::processRevenueAllocation(
    const std::array<Money, NUM_GOODS>& formalOut,
    const std::array<Money, NUM_GOODS>& consumerSpending,
    const std::array<Money, NUM_GOODS>& intermediatePayment,
    const Money& constrRevenue,
    const std::array<double, TYPE_COUNT>& activityRate,
    std::array<Money, TYPE_COUNT>& revenueByBuilding) {

    std::array<Money, NUM_GOODS> totalSalesValue;
    for (int g = 0; g < NUM_GOODS; ++g)
        totalSalesValue[g] = consumerSpending[g] + intermediatePayment[g];
    int constrIdx = goodIndex["建造力"];
    totalSalesValue[constrIdx] += constrRevenue;

    for (int g = 0; g < NUM_GOODS; ++g) {
        if (formalOut[g] <= Money(0) || totalSalesValue[g] <= Money(0)) continue;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            if (bt.outputGood != g) continue;
            Money share = (Money(bld.getBuildingCounts()[t]) * Money(activityRate[t])) / formalOut[g];
            revenueByBuilding[t] += totalSalesValue[g] * share;
        }
    }
}
