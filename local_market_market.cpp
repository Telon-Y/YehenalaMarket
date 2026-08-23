// ==================== local_market_market.cpp ====================
// Price level, construction settlement, and income distribution.
#include "local_market.h"
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
    const Money taxRate = quoteTransactionTax(Money(1));
    Money privateCash = std::max(Money(0), investmentPool);
    if (taxRate > Money(0))
        privateCash /= Money(1) + taxRate;
    // BuildingManager mutates these local ledgers while it walks the queue.
    // They are shadow accounts: the real investment pool/treasury is updated
    // exactly once from the returned settlement below. For country-owned
    // markets, limit legacy government orders to the amount that is actually
    // covered by country construction reservations so several orders cannot
    // oversubscribe the same treasury in one pass.
    Money governmentCash = fiscalCountry == nullptr
        ? playerCash
        : std::min(fiscalCountry->getTreasury(),
                   fiscalCountry->getReservedConstructionBudget());
    const bool transferPrivatePayment = fiscalCountry == nullptr;
    ConstructionSettlement settlement = bld.processConstruction(
        availConstr, constrPrice, privateCash, governmentCash, true,
        transferPrivatePayment);
    // Private construction is a market purchase: the investor pays the base
    // amount from the investment pool and the owning country collects the
    // temporary transaction tax. Government construction is an internal
    // transfer and remains tax-exempt.
    const Money privateTax = quoteTransactionTax(settlement.privatePayment);
    const Money privateDebit = settlement.privatePayment + privateTax;
    if (privateDebit > Money(0) &&
        privateDebit > investmentPool + Money(1e-6))
        throw std::logic_error("construction settlement exceeded private funds");
    investmentPool -= privateDebit;
    if (fiscalCountry != nullptr) {
        if (settlement.privatePayment > Money(0)) {
            fiscalCountry->creditTreasury(settlement.privatePayment);
            collectTransactionTax(settlement.privatePayment);
        }
        if (settlement.governmentPayment > Money(0) &&
            !fiscalCountry->settleConstructionPayment(
                settlement.governmentPayment)) {
            throw std::logic_error("country construction settlement failed");
        }
        if (settlement.governmentBudgetReleased > Money(0))
            fiscalCountry->releaseConstructionBudget(
                settlement.governmentBudgetReleased);
    } else {
        playerCash += settlement.privatePayment - settlement.governmentPayment;
    }
    const Money constructionRevenue = settlement.privatePayment +
                                      settlement.governmentPayment;
    if (constructionRevenue > Money(0))
        bld.addCash(CONST_DEPT, constructionRevenue);
    soldConstr = settlement.totalUsed;
    constrRevenue = constructionRevenue;
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
            if (bld.getBuildingCounts()[CONST_DEPT] > 0)
                revenueByBuilding[CONST_DEPT] += totalSalesValue[g];
            else
                merchantRevenue += totalSalesValue[g];
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
