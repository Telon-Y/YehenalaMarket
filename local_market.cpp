// ==================== local_market.cpp ====================
// 构造、主循环、玩家操作、库存接口、证券系统
#include "local_market.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

using namespace std;

LocalMarket::LocalMarket(int id, const std::string& name)
    : marketId(id), marketName(name) {
    for (int i = 0; i < NUM_GOODS; ++i)
        goodIndex[commodityNames[i]] = i;

    initPriceState(priceState);

    latestConsumerTarget.fill(Money(0));
    latestConsumerActual.fill(Money(0));
    latestPotentialIn.fill(Money(0));
    latestRealOut.fill(Money(0));
    latestBuildingOutput.fill(Money(0));
    inventory.fill(Money(0));
    tradeBalance.fill(Money(0));
    totalLaborers = 0.0;
    totalEngineers = 0.0;
    totalCapitalists = 0.0;

    Money initMoney = Money(500000000.0);
    classCash[LABORER]    = initMoney * Money(0.4);
    classCash[ENGINEER]   = initMoney * Money(0.4);
    classCash[CAPITALIST] = initMoney * Money(0.2);
    investmentPool = Money(200000000.0);
    bld.syncBankLevels(investmentPool);
    totalDebt = Money(0);
    loanBalance.fill(Money(0));
    classLastSpending.fill(Money(0));
    buildingLoanCount.fill(0);
    loanDelinquentWeeks.fill(0);
    investmentLoanDelinquentWeeks = 0;
    playerCash = Money(0);
    buildTransferTotal = Money(0);

    initialTotalMoneySupply = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) initialTotalMoneySupply += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) initialTotalMoneySupply += classCash[c];
    initialTotalMoneySupply += investmentPool;   // 修复：投资池也是初始货币的一部分
    totalMoneySupply = initialTotalMoneySupply;

    buildingWages.fill(Money(averageWage));

    maxLabor = laborPopulation * LABOR_FORCE_PARTICIPATION;
    dependentPopulation = laborPopulation - maxLabor;

    double initialEmployed = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t)
        if (bld.getBuildingCounts()[t] > 0)
            initialEmployed += bld.getBuildingCounts()[t] * bld.getTemplates()[t].laborPerUnit;
    double initialRemaining = maxLabor - initialEmployed;
    if (initialRemaining < 0.0) initialRemaining = 0.0;
    int initialIdleFarms = 10000 - (bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]);
    if (initialIdleFarms < 0) initialIdleFarms = 0;
    subsistencePop = std::min(initialRemaining, (double)initialIdleFarms * 5000.0);
    if (subsistencePop < 0.0) subsistencePop = 0.0;

    // 注册建筑完成回调（发行金融商品）
    bld.onBuildingCompleted = [this](int typeIdx, int count, OwnerType owner) {
        this->issueSecurities(typeIdx, count, owner);
    };
    bld.onBuildingsRemoving = [this](int typeIdx, int count) {
        this->settleLoansBeforeDemolish(typeIdx, count);
    };
    for (int t = 0; t < TYPE_COUNT; ++t)
        issueSecurities(t, bld.getBuildingCounts()[t], OWNER_INITIAL);
}

void LocalMarket::setPriceForSetup(int goodIdx, Money price) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(price) || price <= Money(0))
        return;
    priceState.prices[goodIdx] = price;
}

void LocalMarket::setInvestmentLoanForSetup(Money balance, int dueStep,
                                             int delinquentWeeks) {
    investmentLoanBalance = clamp(balance, Money(0), MONEY_SUPPLY_MAX_MONEY);
    investmentLoanDueStep = investmentLoanBalance > Money(0) ? dueStep : -1;
    investmentLoanDelinquentWeeks = investmentLoanBalance > Money(0)
        ? std::max(0, delinquentWeeks) : 0;
    recalculateTotalDebt();
}

MarketSnapshot LocalMarket::getSnapshot() const {
    MarketSnapshot snap;
    snap.marketId = marketId;
    snap.marketName = marketName;
    snap.prices = priceState.prices;
    snap.inventory = inventory;
    snap.gdp = gdpHist.empty() ? Money(0) : gdpHist.back();
    snap.population = laborPopulation;
    return snap;
}

// ==================== 库存接口 ====================

void LocalMarket::addToInventory(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || amount <= Money(0)) return;
    inventory[goodIdx] += amount;
    inventory[goodIdx] = clamp(inventory[goodIdx], Money(0), Money(1e12L));
}

Money LocalMarket::takeFromInventory(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS) return Money(0);
    Money taken = std::min(inventory[goodIdx], amount);
    inventory[goodIdx] -= taken;
    if (inventory[goodIdx] < Money(0)) inventory[goodIdx] = Money(0);
    return taken;
}

void LocalMarket::setInventoryForSetup(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(amount)) return;
    inventory[goodIdx] = clamp(amount, Money(0), Money(1e12L));
}

void LocalMarket::addTradeBalance(int goodIdx, Money amount) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(amount)) return;
    tradeBalance[goodIdx] += amount;
}

void LocalMarket::addTradePayment(Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return;
    investmentPool = clamp(investmentPool + amount,
                           Money(0), INVEST_POOL_MAX_MONEY);
    bld.syncBankLevels(investmentPool);
}

bool LocalMarket::tryTradePayment(Money amount) {
    if (!isfinite(amount) || amount <= Money(0) || investmentPool < amount)
        return false;
    investmentPool -= amount;
    bld.syncBankLevels(investmentPool);
    return true;
}

// ==================== 证券系统 ====================

void LocalMarket::issueSecurities(int typeIdx, int count, OwnerType owner) {
    if (typeIdx == BANK || typeIdx == FINANCE || typeIdx == CONST_DEPT ||
        typeIdx == INDUSTRIAL_BANK || typeIdx == SAVINGS_BANK) return;
    const auto& bt = bld.getTemplates()[typeIdx];
    if (bt.isFinancial) return;

    Money unitCost = priceState.prices[goodIndex["建造力"]] * Money(buildingCost[typeIdx]);
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

// ==================== 主循环 ====================

void LocalMarket::step() {
    stepCount++;
    const int constrIdx = goodIndex["建造力"];
    const int goldIdx = goodIndex["贵金属"];

    // ===== 全局钳制 =====
    for (auto& c : classCash) clampMoney(c);
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    bld.clampAllCash();
    bld.syncBankLevels(investmentPool);

    // ===== 价格水平更新 =====
    processPriceLevelUpdate();

    // ===== 劳动力分配 =====
    std::array<double, TYPE_COUNT> idealEmployment;
    bool laborShortage;
    processLaborAllocation(idealEmployment, laborShortage);

    // ===== 产出率与原料短缺惩罚 =====
    std::array<double, TYPE_COUNT> activityRate;
    std::array<double, TYPE_COUNT> supplyRatio;
    std::array<Money, NUM_GOODS> potentialIn;
    processSupplyRatios(activityRate, supplyRatio, potentialIn);

    // ===== 生产计算 =====
    std::array<Money, NUM_GOODS> formalOut, realOut, realIn;
    std::array<Money, TYPE_COUNT> buildingOutput;
    processProduction(activityRate, formalOut, realOut, realIn, buildingOutput);

    // ===== 消费计算 =====
    std::array<Money, NUM_GOODS> consumerTarget, consumerPlanned, consumerActual, consumerSpending;
    processConsumption(realOut, realIn, consumerTarget, consumerPlanned,
                       consumerActual, consumerSpending);
    latestPotentialIn = potentialIn;

    // ===== 工资发放 =====
    std::array<Money, TYPE_COUNT> laborCostByBuilding;
    processWagePayment(laborCostByBuilding);

    // ===== 建造力价格 =====
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        const auto& bt = bld.getTemplates()[CONST_DEPT];
        Money laborCostPU = Money(bt.laborPerUnit) * buildingWages[CONST_DEPT] /
                            Money(std::max(bt.outputRate, 1e-9));
        Money inputCostPU = Money(0);
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCostPU += priceState.prices[g] * Money(bt.inputs[g]);
        priceState.prices[constrIdx] = (laborCostPU + inputCostPU) * Money(1.10);
        if (priceState.prices[constrIdx] < Money(0.1))
            priceState.prices[constrIdx] = Money(0.1);
    }

    // ===== 银行贷款系统 =====
    Money weeklyPrivateConstrDemand =
        bld.getWeeklyPrivateConstructionDemand(formalOut[constrIdx]);
    processBankLoans(weeklyPrivateConstrDemand, priceState.prices[constrIdx]);

    // ===== 价格 ODE 更新 =====
    double laborDebt = max(0.0, (-classCash[LABORER]).toDouble());
    double debtFactor = 1.0 + laborDebt / LABOR_DEBT_SCALE;
    stepPrices(priceState, realOut, potentialIn, consumerPlanned,
               debtFactor, dt, constrIdx, goldIdx);

    // ===== 强制黄金固定平价 =====
    priceState.prices[goldIdx] = Money(GOLD_FIXED_PRICE);

    // Consumption and intermediate inputs must settle against the same
    // post-update price vector for this week.
    for (int i = 0; i < NUM_GOODS; ++i)
        consumerSpending[i] = consumerActual[i] * priceState.prices[i];

    // ===== 消费扣款 =====
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
            if (actualCost > positiveTotal) {
                Money affordability = positiveTotal / actualCost;
                for (int i = 0; i < NUM_GOODS; ++i) {
                    consumerActual[i] *= affordability;
                    consumerSpending[i] *= affordability;
                }
                actualCost = positiveTotal;
            }
            Money consume = std::min(actualCost, positiveTotal);
            Money scalePay = consume / positiveTotal;
            for (int c = 0; c < CLASS_COUNT; ++c)
                classCash[c] -= positiveCash[c] * scalePay;
        }
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        classLastSpending[c] = classBeforeSpending[c] - classCash[c];
        clampMoney(classCash[c]);
    }

    // ===== 满意度 =====
    {
        double urbanEssentialsSat = 0.0, urbanEssentialsDemand = 0.0;
        for (int good : groupGoods[GRP_SIMPLE_CLOTHES]) {
            urbanEssentialsDemand += consumerTarget[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
            urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
        }
        for (int good : groupGoods[GRP_BASIC_FOOD]) {
            urbanEssentialsDemand += consumerTarget[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
            urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
        }
        if (urbanEssentialsDemand > 1e-9)
            satisfaction = std::clamp(urbanEssentialsSat / urbanEssentialsDemand, 0.0, 1.0);
        else
            satisfaction = 1.0;
        double suppressionPenalty = 1.0 / (1.0 + (debtFactor - 1.0) * 0.5);
        satisfaction *= suppressionPenalty;
        satisfaction = std::clamp(satisfaction, 0.0, 1.0);
    }

    // ===== 建筑投入成本 =====
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
        inputCostByBuilding[t] = cost;
        if (t == CONST_DEPT) playerCash -= cost;
        else bld.addCash(t, -cost);
    }

    // ===== 建筑贷款 =====
    processBuildingBorrowing();

    // ===== 建造过程 =====
    Money soldConstr, constrRevenue;
    processConstruction(priceState.prices[constrIdx], formalOut[constrIdx], soldConstr, constrRevenue);
    lastConstrProduced = formalOut[constrIdx];
    lastConstrUsed = soldConstr;

    // ===== 记账：累计建造划转金额 =====
    buildTransferTotal += constrRevenue;

    // ===== 收入分配 =====
    std::array<Money, TYPE_COUNT> revenueByBuilding;
    revenueByBuilding.fill(Money(0));
    processRevenueAllocation(formalOut, consumerSpending, intermediatePayment,
                             constrRevenue, activityRate, revenueByBuilding);

    // ===== 银行铸币 =====
    // 中央银行铸币：收入计入 revenueByBuilding[BANK]
    // 利润分配阶段将央行的铸币利润上缴玩家账户
    const auto& cbBt = bld.getTemplates()[BANK];
    if (bld.getBuildingCounts()[BANK] > 0) {
        Money goldConsumed = Money(bld.getBuildingCounts()[BANK]) *
                             Money(cbBt.inputs[goldIdx]) * Money(activityRate[BANK]);
        Money goldBackedCapacity = goldConsumed * Money(GOLD_FIXED_PRICE) * Money(2.0);
        Money populationScale = Money(std::clamp(laborPopulation / 10000000.0, 0.25, 4.0));
        Money targetMoneySupply = initialTotalMoneySupply * populationScale;
        Money supplyGap = std::max(Money(0), targetMoneySupply - totalMoneySupply);
        Money weeklyAdjustmentLimit = initialTotalMoneySupply * Money(0.001);
        Money totalMoneyCreated = std::min({goldBackedCapacity,
                                            supplyGap * Money(0.02),
                                            weeklyAdjustmentLimit});
        revenueByBuilding[BANK] += totalMoneyCreated;
    }
    // 工商银行与储蓄银行暂不铸币（等待后续版本完善）

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        if (t == CONST_DEPT) continue;
        bld.addCash(t, revenueByBuilding[t]);
    }

    // ===== 利润分配 =====
    std::array<double, TYPE_COUNT> actualProfitRates;
    processProfitDistribution(revenueByBuilding, inputCostByBuilding, laborCostByBuilding,
                              buildingOutput, laborShortage, actualProfitRates);
    bld.updateActualProfitRates(actualProfitRates);
    reconcileSecurities();
    processSecurityMarket();

    // ===== 货币供给 =====
    processMoneySupply();
    recalculateTotalDebt();

    // ===== GDP =====
    Money gdp = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        gdp += revenueByBuilding[t] - inputCostByBuilding[t];
    }

    // ===== 库存记录 =====
    {
        std::array<Money, NUM_GOODS> totalDemand = realIn;
        for (int i = 0; i < NUM_GOODS; ++i)
            totalDemand[i] += consumerActual[i];
        recordInventoryChange(realOut, totalDemand);
    }

    // ===== 人口增长 =====
    processPopulationGrowth();

    if (stepCount % 52 == 0) bld.adjustEmployment();
    bld.checkDecay(stepCount, investmentPool, classCash);
    reconcileSecurities();

    // 最终钳制
    for (auto& c : classCash) clampMoney(c);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    bld.clampAllCash();
    bld.syncBankLevels(investmentPool);

    // ===== 历史记录 =====
    recordHistory(realOut, buildingOutput, gdp);
}

// ==================== AI 与玩家操作 ====================

void LocalMarket::aiBuild() {
    Money unusedConstr = lastConstrProduced - lastConstrUsed;
    if (unusedConstr > Money(0.5)) {
        bld.setAllowAutoConstExpansion(false);
    } else {
        bld.setAllowAutoConstExpansion(true);
    }

    bld.aiBuild(aiProfitThreshold, priceState.prices, buildingWages, maxLabor,
                actualEmploymentRate);
}

void LocalMarket::playerBuild(int typeIdx, int count) {
    // Private orders pay the government as construction is consumed;
    // government-owned construction uses capacity without an internal transfer.
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
