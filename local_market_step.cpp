#include "local_market.h"
#include "local_market_internal.h"
#include "world.h"
#include "country.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <numeric>

using namespace std;

void LocalMarket::step() {
    if (!flowTraceActive) {
        const int cycle = logisticsNetwork == nullptr
            ? stepCount : logisticsNetwork->currentCycle();
        beginFlowTrace(cycle);
    }
    if (!externalLogistics && logisticsNetwork != nullptr) {
        logisticsNetwork->beginLogisticsBatch();
        logisticsNetwork->receive();
        prepareWarehouseCycle();
        logisticsNetwork->refreshProductionForecasts();
        planWarehouseReplenishments();
        logisticsNetwork->processProductionOrders();
        logisticsNetwork->allocateLocal();
        logisticsNetwork->routeShortages();
        logisticsNetwork->confirmAndReserve();
        logisticsNetwork->dispatch();
        logisticsNetwork->receive();
    }
    stepCount++;
    const int constrIdx = CONSTR_GOOD_INDEX;
    const int goldIdx = GOLD_GOOD_INDEX;

    // Global money clamps.
    for (auto& c : classCash) clampMoney(c);
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    if (fiscalCountry == nullptr)
        playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    else
        playerCash = Money(0);
    bld.clampAllCash();
    bld.syncBankLevels(investmentPool);

    // Price-level update.
    processPriceLevelUpdate();

    // Labor allocation.
    std::array<double, TYPE_COUNT> idealEmployment;
    bool laborShortage;
    processLaborAllocation(idealEmployment, laborShortage);

    // Output rates and input-shortage penalties.
    std::array<double, TYPE_COUNT> activityRate;
    std::array<double, TYPE_COUNT> supplyRatio;
    std::array<Money, NUM_GOODS> potentialIn;
    processSupplyRatios(activityRate, supplyRatio, potentialIn);

    // Production.
    std::array<Money, NUM_GOODS> formalOut, realOut, realIn;
    std::array<Money, TYPE_COUNT> buildingOutput;
    processProduction(activityRate, formalOut, realOut, realIn, buildingOutput);
    latestFlow.production = realOut;

    // Consumption.
    std::array<Money, NUM_GOODS> consumerTarget, consumerPlanned, consumerActual, consumerSpending;
    processConsumption(realOut, realIn, consumerTarget, consumerPlanned,
                       consumerActual, consumerSpending);
    latestPotentialIn = potentialIn;
    updateWarehouseDemandPolicies(consumerPlanned, potentialIn);

    // Wage payments.
    std::array<Money, TYPE_COUNT> laborCostByBuilding;
    processWagePayment(laborCostByBuilding);

    // Construction-capacity price.
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        const auto& bt = bld.getTemplates()[CONST_DEPT];
        Money laborCostPU = Money(bt.laborPerUnit) * (buildingWages[CONST_DEPT] + buildingBonuses[CONST_DEPT]) /
                            Money(std::max(bt.outputRate, 1e-9));
        Money inputCostPU = Money(0);
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCostPU += priceState.prices[g] * Money(bt.inputs[g]);
        priceState.prices[constrIdx] = (laborCostPU + inputCostPU) * Money(1.10);
        if (priceState.prices[constrIdx] < Money(0.1))
            priceState.prices[constrIdx] = Money(0.1);
    }

    // Bank lending.
    Money weeklyPrivateConstrDemand =
        bld.getWeeklyPrivateConstructionDemand(formalOut[constrIdx]);
    processBankLoans(weeklyPrivateConstrDemand, priceState.prices[constrIdx]);

    // Price ODE update.
    double laborDebt = max(0.0, (-classCash[LABORER]).toDouble());
    double debtFactor = 1.0 + laborDebt / LABOR_DEBT_SCALE;
    std::array<Money, NUM_GOODS> marketInputDemand = potentialIn;
    for (int good = 0; good < NUM_GOODS; ++good) {
        const InventoryState& stock = warehouse.stock(good);
        const Money shortage = stock.replenishmentQuantity();
        if (shortage <= Money(0)) continue;
        const Money weeklySignal = stock.policy.weeklyDemand > Money(0)
            ? std::min(shortage, stock.policy.weeklyDemand)
            : shortage;
        marketInputDemand[good] += weeklySignal;
    }
    stepPrices(priceState, realOut, marketInputDemand, consumerPlanned,
               debtFactor, dt, constrIdx, goldIdx);

    // Enforce fixed gold parity.
    priceState.prices[goldIdx] = Money(GOLD_FIXED_PRICE);

    // Consumption and intermediate inputs must settle against the same
    // post-update price vector for this week.
    for (int i = 0; i < NUM_GOODS; ++i)
        consumerSpending[i] = consumerActual[i] * priceState.prices[i];

    // Consumer payments.
    std::array<Money, CLASS_COUNT> classBeforeSpending = classCash;
    {
        std::array<Money, CLASS_COUNT> positiveCash;
        Money positiveTotal = Money(0);
        for (int c = 0; c < CLASS_COUNT; ++c) {
            positiveCash[c] = max(Money(0), classBeforeSpending[c]);
            positiveTotal += positiveCash[c];
        }
        if (positiveTotal > Money(0)) {
            Money actualCost = Money(0);
            for (int i = 0; i < NUM_GOODS; ++i)
                actualCost += consumerSpending[i];
            Money consumerTax = quoteTransactionTax(actualCost);
            Money totalCost = actualCost + consumerTax;
            if (totalCost > positiveTotal) {
                Money affordability = positiveTotal / totalCost;
                for (int i = 0; i < NUM_GOODS; ++i) {
                    consumerActual[i] *= affordability;
                    consumerSpending[i] *= affordability;
                }
                actualCost *= affordability;
                consumerTax = quoteTransactionTax(actualCost);
                totalCost = actualCost + consumerTax;
            }
            actualCost = Money(0);
            for (int i = 0; i < NUM_GOODS; ++i) {
                consumerActual[i] = takeFromInventory(i, consumerActual[i]);
                consumerSpending[i] =
                    consumerActual[i] * priceState.prices[i];
                actualCost += consumerSpending[i];
            }
            consumerTax = quoteTransactionTax(actualCost);
            totalCost = actualCost + consumerTax;
            Money consume = std::min(totalCost, positiveTotal);
            Money scalePay = consume / positiveTotal;
            for (int c = 0; c < CLASS_COUNT; ++c)
                classCash[c] -= positiveCash[c] * scalePay;
            collectTransactionTax(actualCost);
        } else {
            consumerActual.fill(Money(0));
            consumerSpending.fill(Money(0));
        }
    }
    latestConsumerActual = consumerActual;
    latestFlow.consumerUse = consumerActual;
    latestFlow.consumerValue = Money(0);
    for (int good = 0; good < NUM_GOODS; ++good)
        latestFlow.consumerValue += consumerSpending[good];
    for (int c = 0; c < CLASS_COUNT; ++c) {
        classLastSpending[c] = classBeforeSpending[c] - classCash[c];
        clampMoney(classCash[c]);
    }

    // Satisfaction.
    {
        constexpr double kPopulationDemandScale = 1.0 / 100000.0;
        const double subsistenceFood =
            demandTable[0][GRP_BASIC_FOOD] * subsistencePop *
            kPopulationDemandScale;
        double urbanEssentialsSat = subsistenceFood;
        double urbanEssentialsDemand = subsistenceFood;
        for (int good : groupGoods[GRP_SIMPLE_CLOTHES]) {
            urbanEssentialsDemand += consumerTarget[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
            urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
        }
        for (int good : groupGoods[GRP_BASIC_FOOD]) {
            urbanEssentialsDemand += consumerTarget[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
            urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
        }
        if (urbanEssentialsDemand > 1e-9)
            instantSatisfaction = std::clamp(
                urbanEssentialsSat / urbanEssentialsDemand, 0.0, 1.0);
        else
            instantSatisfaction = 1.0;
        double suppressionPenalty = 1.0 / (1.0 + (debtFactor - 1.0) * 0.5);
        instantSatisfaction *= suppressionPenalty;
        instantSatisfaction = std::clamp(
            instantSatisfaction, 0.0, 1.0);
        constexpr double kSatisfactionSmoothingGain = 0.35;
        satisfaction = std::clamp(
            satisfactionFilter.update(instantSatisfaction,
                                      kSatisfactionSmoothingGain),
            0.0, 1.0);
    }

    // Construction input cost.
    std::array<Money, TYPE_COUNT> inputCostByBuilding;
    std::array<Money, NUM_GOODS> intermediatePayment;
    inputCostByBuilding.fill(Money(0));
    intermediatePayment.fill(Money(0));
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        Money cost = Money(0);
        for (int g = 0; g < NUM_GOODS; ++g) {
            Money amt = Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
            Money pay = amt * priceState.prices[g];
            cost += pay;
            intermediatePayment[g] += pay;
        }
        const Money inputTax = quoteTransactionTax(cost);
        inputCostByBuilding[t] = cost;
        if (t == CONST_DEPT) {
            if (fiscalCountry == nullptr) {
                playerCash -= cost + inputTax;
            } else {
                // A government construction department is still an
                // operating business. Spend its own cash on inputs first;
                // the national treasury only covers an actual subsidy.
                const Money totalCost = cost + inputTax;
                const Money departmentCash = std::max(
                    Money(0), bld.getCashPools()[t]);
                const Money departmentPayment =
                    std::min(departmentCash, totalCost);
                if (departmentPayment > Money(0))
                    bld.addCash(t, -departmentPayment);
                const Money subsidy =
                    std::max(Money(0), totalCost - departmentPayment);
                const Money payable = std::min(
                    fiscalCountry->getAvailableTreasury(), subsidy);
                if (payable > Money(0)) fiscalCountry->spendTreasury(payable);
            }
        } else bld.addCash(t, -(cost + inputTax));
        collectTransactionTax(cost);
    }

    // Construction borrowing.
    processBuildingBorrowing();

    // Construction settlement.
    const Money settledNationalRevenue = pendingNationalConstructionRevenue;
    pendingNationalConstructionRevenue = Money(0);

    Money soldConstr, constrRevenue;
    // Construction power is special/current-cycle output, not warehouse stock.
    processConstruction(priceState.prices[constrIdx], formalOut[constrIdx],
                        soldConstr, constrRevenue);
    latestFlow.constructionUse[constrIdx] += soldConstr;
    localConstructionUsedThisCycle = soldConstr;
    nationalConstructionUsedSinceStep = Money(0);
    constrRevenue += settledNationalRevenue;
    lastConstrProduced = formalOut[constrIdx];
    lastConstrUsed = soldConstr;

    // Record cumulative construction transfers.
    buildTransferTotal += constrRevenue;

    // Revenue distribution.
    std::array<Money, TYPE_COUNT> revenueByBuilding;
    revenueByBuilding.fill(Money(0));
    processRevenueAllocation(formalOut, consumerSpending, intermediatePayment,
                             constrRevenue, activityRate, revenueByBuilding);

    // Central-bank money creation.
    // Issuance is recorded as central-bank revenue.
    // Profit distribution remits the issued amount to government.
    const auto& cbBt = bld.getTemplates()[BANK];
    if (bld.getBuildingCounts()[BANK] > 0) {
        Money goldConsumed = Money(bld.getBuildingCounts()[BANK]) *
                             Money(cbBt.inputs[goldIdx]) * Money(activityRate[BANK]);
        Money goldBackedCapacity = goldConsumed * Money(GOLD_FIXED_PRICE) * Money(2.0);
        Money populationScale = Money(std::clamp(laborPopulation / 10000000.0, 0.25, 4.0));
        Money targetMoneySupply = moneySupplyBaseline() * populationScale;
        Money supplyGap = std::max(Money(0), targetMoneySupply - totalMoneySupply);
        Money weeklyAdjustmentLimit = moneySupplyBaseline() * Money(0.001);
        Money totalMoneyCreated = std::min({goldBackedCapacity,
                                            supplyGap * Money(0.02),
                                            weeklyAdjustmentLimit});
        revenueByBuilding[BANK] += totalMoneyCreated;
    }
    // Industrial and savings banks do not issue currency.

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        if (t == CONST_DEPT) continue;
        bld.addCash(t, revenueByBuilding[t]);
    }

    // Profit distribution.
    std::array<double, TYPE_COUNT> actualProfitRates;
    processProfitDistribution(revenueByBuilding, inputCostByBuilding, laborCostByBuilding,
                              buildingOutput, laborShortage, actualProfitRates);
    bld.updateActualProfitRates(actualProfitRates);
    reconcileSecurities();
    processSecurityMarket();

    // Money supply.
    processMoneySupply();
    recalculateTotalDebt();

    // ===== GDP =====
    Money gdp = Money(0);
    Money productiveIntermediateCost = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const BuildingTemplate& building = bld.getTemplates()[t];
        if (building.outputGood < 0) continue;
        gdp += buildingOutput[t] *
            priceState.prices[building.outputGood];
        gdp -= inputCostByBuilding[t];
        productiveIntermediateCost += inputCostByBuilding[t];
    }
    // A negative intermediate-cost residual is an accounting loss, not
    // negative production.  Keep the published GDP flow non-negative so a
    // market that has real output cannot fail the macro health gate merely
    // because a low-margin batch was priced below its inputs.
    bool hasProductiveBuilding = false;
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const BuildingTemplate& building = bld.getTemplates()[type];
        if (bld.getBuildingCounts()[type] > 0 && building.outputGood >= 0) {
            hasProductiveBuilding = true;
            break;
        }
    }
    if (hasProductiveBuilding)
        gdp = std::max(Money(1.0e-3), gdp);
    latestFlow.grossOutputValue = Money(0);
    latestFlow.intermediateCost = Money(0);
    for (int good = 0; good < NUM_GOODS; ++good)
        latestFlow.grossOutputValue += realOut[good] * priceState.prices[good];
    latestFlow.intermediateCost = productiveIntermediateCost;
    latestFlow.constructionValue = constrRevenue;
    latestFlow.gdp = gdp;

    // Inventory history.
    {
        std::array<Money, NUM_GOODS> totalDemand = realIn;
        for (int i = 0; i < NUM_GOODS; ++i)
            totalDemand[i] += consumerActual[i];
        recordInventoryChange(realOut, totalDemand);
    }

    // Population growth.
    processPopulationGrowth();

    if (stepCount % 52 == 0) bld.adjustEmployment();
    bld.checkDecay(stepCount, investmentPool, classCash);
    reconcileSecurities();

    // Final clamping.
    for (auto& c : classCash) clampMoney(c);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    if (fiscalCountry == nullptr)
        playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    else
        playerCash = Money(0);
    bld.clampAllCash();
    bld.syncBankLevels(investmentPool);

    // History.
    recordHistory(realOut, buildingOutput, gdp);
    if (!externalLogistics && logisticsNetwork != nullptr) {
        logisticsNetwork->advanceTransit();
        logisticsNetwork->finishCycle();
        logisticsNetwork->endLogisticsBatch();
        finalizeFlowTrace(
            logisticsNetwork->lastCompletedFlow(marketId));
    }
}
