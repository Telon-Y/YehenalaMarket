// ==================== local_market.cpp ====================
// 完整文件，包含银行贷款、人口死亡率修正等新增逻辑

#include "local_market.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

using namespace std;

LocalMarket::LocalMarket() {
    for (int i = 0; i < NUM_GOODS; ++i)
        goodIndex[commodityNames[i]] = i;

    // 逐元素将 double 转换为 Money（Decimal）
    for (int i = 0; i < NUM_GOODS; ++i) {
        prices[i] = Money(referencePrice[i]);
        b[i] = Money(priceSuppressBase[i]);
        baseReferencePrice[i] = Money(referencePrice[i]);
        dynamicReferencePrice[i] = Money(referencePrice[i]);
    }
    v.fill(Money(0));
    m.fill(Money(1));
    maxLabor = population;

    latestConsumerTarget.fill(Money(0));
    latestConsumerActual.fill(Money(0));
    latestPotentialIn.fill(Money(0));
    latestRealOut.fill(Money(0));
    latestBuildingOutput.fill(Money(0));
    totalLaborers = 0.0;
    totalEngineers = 0.0;
    totalCapitalists = 0.0;

    Money initMoney = Money(500000000.0);
    classCash[LABORER]    = initMoney * Money(0.4);
    classCash[ENGINEER]   = initMoney * Money(0.4);
    classCash[CAPITALIST] = initMoney * Money(0.2);
    investmentPool = Money(200000000.0);   // 初始投资池
    totalDebt = Money(0);
    loanBalance.fill(Money(0));
    classLastSpending.fill(Money(0));
    buildingLoanCount.fill(0);  // 新增：初始化建筑贷款笔数

    initialTotalMoneySupply = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) initialTotalMoneySupply += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) initialTotalMoneySupply += classCash[c];
    totalMoneySupply = initialTotalMoneySupply;
    priceLevel = Money(1);
    targetPriceLevel = Money(1);

    buildingWages.fill(Money(averageWage));

    double initialEmployed = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t)
        if (bld.getBuildingCounts()[t] > 0)
            initialEmployed += bld.getBuildingCounts()[t] * bld.getTemplates()[t].laborPerUnit;
    double initialRemaining = population - initialEmployed;
    if (initialRemaining < 0.0) initialRemaining = 0.0;
    int initialIdleFarms = 10000 - (bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]);
    if (initialIdleFarms < 0) initialIdleFarms = 0;
    subsistencePop = std::min(initialRemaining, (double)initialIdleFarms * 5000.0);
    if (subsistencePop < 0.0) subsistencePop = 0.0;
}

void LocalMarket::step() {
    stepCount++;
    const int constrIdx = goodIndex["建造力"];
    const int goldIdx = goodIndex["贵金属"];

    auto clampMoney = [](Money& x) {
        if (!isfinite(x)) x = Money(0);
        x = clamp(x, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    };

    for (auto& c : classCash) clampMoney(c);
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
    bld.clampAllCash();

    // ===== 价格水平更新（缓慢跟随活跃货币） =====
    if (stepCount > 1 && initialTotalMoneySupply > Money(0)) {
        Money activeMoney = Money(0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (t == BANK || t == FINANCE || t == CONST_DEPT) continue;
            activeMoney += bld.getCashPools()[t];
        }
        for (int c = 0; c < CLASS_COUNT; ++c) activeMoney += classCash[c];
        activeMoney += investmentPool;
        activeMoney = clamp(activeMoney, Money(0), Money(1e15L));

        Money moneyRatio = activeMoney / initialTotalMoneySupply;
        targetPriceLevel = moneyRatio;
        priceLevel += Money(0.005) * (targetPriceLevel - priceLevel);
        priceLevel = clamp(priceLevel, Money(0.01), Money(1e6L));
        for (int i = 0; i < NUM_GOODS; ++i) {
            dynamicReferencePrice[i] = baseReferencePrice[i] * priceLevel;
            if (!isfinite(dynamicReferencePrice[i]) || dynamicReferencePrice[i] <= Money(0))
                dynamicReferencePrice[i] = baseReferencePrice[i];
        }
    }

    auto inQueueCount = bld.getInQueueCounts();
    int usedFarms = bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int idleLand = max(0, 10000 - usedFarms);
    subsistenceFarms = idleLand;

    maxLabor = population;

    // ========== 劳动力需求与分配 ==========
    std::array<double, TYPE_COUNT> idealEmployment;
    double totalIdeal = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) {
            idealEmployment[t] = 0.0;
            continue;
        }
        const auto& bt = bld.getTemplates()[t];
        double empRatio = (bt.isFinancial) ? 1.0 : bld.getEmploymentRatio()[t];
        idealEmployment[t] = bld.getBuildingCounts()[t] * bt.laborPerUnit * empRatio;
        totalIdeal += idealEmployment[t];
    }
    bool laborShortage = totalIdeal > population;

    // 按工资降序分配劳动力
    vector<int> order(TYPE_COUNT);
    iota(order.begin(), order.end(), 0);
    sort(order.begin(), order.end(), [&](int a, int b) {
        return buildingWages[a] > buildingWages[b];
    });

    double laborPool = population;
    for (int t : order) {
        if (idealEmployment[t] <= 0) {
            actualEmployment[t] = 0.0;
            actualEmploymentRate[t] = 0.0;
            continue;
        }
        double assign = std::min(idealEmployment[t], laborPool);
        actualEmployment[t] = assign;
        actualEmploymentRate[t] = assign / idealEmployment[t];
        laborPool -= assign;
    }

    // 计算每个建筑的实际产出率（基于实际雇佣比例）
    std::array<double, TYPE_COUNT> activityRate;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) { activityRate[t] = 0.0; continue; }
        const auto& bt = bld.getTemplates()[t];
        if (bt.isFinancial) {
            activityRate[t] = 1.0;
        } else {
            activityRate[t] = bt.outputRate * actualEmploymentRate[t];
        }
    }

    // 供应比率迭代（原料短缺惩罚）
    array<double, TYPE_COUNT> supplyRatio;
    supplyRatio.fill(1.0);
    for (int iter = 0; iter < 30; ++iter) {
        array<Money, NUM_GOODS> tempOut{}, tempIn{};
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            double cr = activityRate[t] * supplyRatio[t];
            if (!bt.isFinancial && bt.outputGood >= 0)
                tempOut[bt.outputGood] += Money(bld.getBuildingCounts()[t]) * Money(cr);
            for (int g = 0; g < NUM_GOODS; ++g)
                tempIn[g] += Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
        }
        array<Money, NUM_GOODS> ratio;
        for (int g = 0; g < NUM_GOODS; ++g) {
            ratio[g] = (tempIn[g] > Money(0) && tempOut[g] < tempIn[g]) ? tempOut[g] / tempIn[g] : Money(1);
        }
        array<double, TYPE_COUNT> newRatio;
        newRatio.fill(1.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            double minR = 1.0;
            for (int g = 0; g < NUM_GOODS; ++g)
                if (bld.getTemplates()[t].inputs[g] > 0) {
                    double r = ratio[g].toDouble();
                    minR = min(minR, r);
                }
            newRatio[t] = minR;
        }
        double maxDiff = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) maxDiff = max(maxDiff, fabs(newRatio[t] - supplyRatio[t]));
        supplyRatio = newRatio;
        if (maxDiff < 1e-6) break;
    }
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) supplyRatio[t] = 1.0;
        else supplyRatio[t] = max(supplyRatio[t], 0.2);
    }
    bld.setCurrentSupplyRatio(supplyRatio);
    for (int t = 0; t < TYPE_COUNT; ++t) activityRate[t] *= supplyRatio[t];

    double employedTotal = population - laborPool;
    totalLaborers    = employedTotal * 0.75;
    totalEngineers   = employedTotal * 0.20;
    totalCapitalists = employedTotal * 0.05;

    // 自给农 = 剩余人口（受耕地容量限制）
    double remainingPop = population - employedTotal;
    if (remainingPop < 0.0) remainingPop = 0.0;
    double maxSubsistence = (double)subsistenceFarms * 5000.0;
    subsistencePop = std::min(remainingPop, maxSubsistence);
    if (subsistencePop < 0.0) subsistencePop = 0.0;

    double subEff = (subsistenceFarms > 0) ? subsistencePop / (subsistenceFarms * 5000.0) : 0.0;
    double subGrain  = subsistenceFarms * 2.0 * subEff;
    double subFabric = subsistenceFarms * 1.0 * subEff;
    double subClothes = subsistenceFarms * 0.5 * subEff;

    array<Money, NUM_GOODS> formalOut{}, realOut{}, realIn{};
    array<Money, TYPE_COUNT> buildingOutput{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        if (!bt.isFinancial && bt.outputGood >= 0) {
            Money outAmt = Money(bld.getBuildingCounts()[t]) * Money(cr);
            formalOut[bt.outputGood] += outAmt;
            realOut[bt.outputGood] += outAmt;
            buildingOutput[t] = outAmt;
        }
        for (int g = 0; g < NUM_GOODS; ++g)
            realIn[g] += Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
    }

    const double popScale = 1.0 / 100000.0;
    array<double, GROUP_COUNT> subGroupDemand{}, urbanGroupDemand{};
    for (int g = 0; g < GROUP_COUNT; ++g) {
        subGroupDemand[g] = demandTable[0][g] * subsistencePop * popScale;
        urbanGroupDemand[g] = demandTable[0][g] * totalLaborers * popScale
                            + demandTable[1][g] * totalEngineers * popScale
                            + demandTable[2][g] * totalCapitalists * popScale;
    }
    Money totalClassFund = classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST];
    double wealthPerCap = totalClassFund.toDouble() / max(population, 1.0);
    double luxuryFactor = clamp(wealthPerCap / 1500.0, 0.0, 3.0);

    // 奢侈品因子平滑
    const double alphaSmooth = 0.2;
    smoothedLuxuryFactor = smoothedLuxuryFactor + alphaSmooth * (luxuryFactor - smoothedLuxuryFactor);
    luxuryFactor = smoothedLuxuryFactor;

    urbanGroupDemand[GRP_STANDARD_CLOTHES] *= (1.0 + luxuryFactor * 0.4);
    urbanGroupDemand[GRP_HOUSING] += population * popScale * luxuryFactor * 30;

    // 消费平滑替代
    auto computeTarget = [&](const array<double, GROUP_COUNT>& gd, array<Money, NUM_GOODS>& target) {
        target.fill(Money(0));
        for (int g = 0; g < GROUP_COUNT; ++g) {
            double remain = gd[g];
            if (remain <= 1e-9) continue;
            vector<int> goods = groupGoods[g];

            vector<pair<int, double>> candidates;
            double totalWeight = 0.0;
            for (int good : goods) {
                double u = valueCoeff[good][g];
                if (u <= 0.0 || prices[good] <= Money(1e-9)) continue;
                double weight = u / prices[good].toDouble();
                candidates.push_back({good, weight});
                totalWeight += weight;
            }

            if (totalWeight <= 1e-12 || candidates.empty()) continue;

            for (const auto& [good, weight] : candidates) {
                double u = valueCoeff[good][g];
                double share = weight / totalWeight;
                double want = share * remain / u;
                target[good] += Money(want);
            }
        }
    };
    array<Money, NUM_GOODS> consumerTarget, urbanConsumerTarget;
    computeTarget(urbanGroupDemand, urbanConsumerTarget);
    consumerTarget = urbanConsumerTarget;
    latestConsumerTarget = consumerTarget;
    latestPotentialIn = realIn;

    // ===== 工资发放 =====
    std::array<Money, TYPE_COUNT> laborCostByBuilding{};
    {
        std::array<Money, CLASS_COUNT> wageIncome{};
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (actualEmployment[t] <= 0) continue;
            Money lc = Money(actualEmployment[t]) * buildingWages[t];
            laborCostByBuilding[t] = lc;
            bld.addCash(t, -lc);
            wageIncome[LABORER]    += lc * Money(0.75);
            wageIncome[ENGINEER]   += lc * Money(0.20);
            wageIncome[CAPITALIST] += lc * Money(0.05);
        }
        for (int c = 0; c < CLASS_COUNT; ++c) {
            classCash[c] += wageIncome[c];
            clampMoney(classCash[c]);
        }
    }

    // ===== 建造力价格：直接设为成本+10%（使用实际工资） =====
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        const auto& bt = bld.getTemplates()[CONST_DEPT];
        Money laborCostPU = Money(bt.laborPerUnit) * buildingWages[CONST_DEPT];
        Money inputCostPU = Money(0);
        for (int g = 0; g < NUM_GOODS; ++g) inputCostPU += prices[g] * Money(bt.inputs[g]);
        Money refConstr = (laborCostPU + inputCostPU) * Money(1.10);
        prices[constrIdx] = refConstr;
        if (prices[constrIdx] < Money(0.1)) prices[constrIdx] = Money(0.1);
    }

    // ==========================================================
    // ===== 新增：银行贷款额度 + 投资池紧急贷款 + 银行自动扩建 =====
    // ==========================================================
    {
        const int bankLevels = bld.getBuildingCounts()[BANK];
        Money bankCash = bld.getCashPools()[BANK];

        // 1) 当前可贷款额度（每100万现金池 → 1单位，每级最多50单位）
        double cashUnits = std::floor(bankCash.toDouble() / BANK_LOAN_UNIT_VALUE);
        double maxUnits  = bankLevels * BANK_MAX_LOAN_PER_LEVEL;
        double availUnits = std::max(0.0, std::min(cashUnits, maxUnits));
        bankLoanCapacity = Money(availUnits * BANK_LOAN_UNIT_VALUE);

        // 2) 投资池到期还款（5年=260周）
        if (investmentLoanBalance > Money(0) && stepCount >= investmentLoanDueStep) {
            Money repay = std::min(investmentLoanBalance, investmentPool);
            investmentPool -= repay;
            investmentLoanBalance -= repay;
            bld.addCash(BANK, repay);

            // 若投资池不足以偿还，剩余贷款作为坏账清零（简化）
            investmentLoanBalance = Money(0);
            investmentLoanDueStep = -1;
        }

        // 3) 投资池紧急贷款（当投资池 < 建造队列剩余成本×200%）
        Money totalRemainingConstr = Money(0);
        for (const auto& ord : bld.getQueue())
            totalRemainingConstr += ord.remainingCost;

        Money neededMoney = totalRemainingConstr * prices[constrIdx] * Money(INVEST_LOAN_TRIGGER_RATIO);

        if (investmentPool < neededMoney && investmentLoanBalance < bankLoanCapacity) {
            Money gap = neededMoney - investmentPool;
            Money availLoan = bankLoanCapacity - investmentLoanBalance;
            Money borrow = std::min(gap, availLoan);

            if (borrow > Money(0)) {
                investmentPool += borrow;
                investmentLoanBalance += borrow;
                bld.addCash(BANK, -borrow);

                if (investmentLoanDueStep < 0)
                    investmentLoanDueStep = stepCount + INVEST_LOAN_TERM_WEEKS;

                // 更新贷款额度（银行现金池已减少）
                bankCash = bld.getCashPools()[BANK];
                cashUnits = std::floor(bankCash.toDouble() / BANK_LOAN_UNIT_VALUE);
                maxUnits  = bankLevels * BANK_MAX_LOAN_PER_LEVEL;
                availUnits = std::max(0.0, std::min(cashUnits, maxUnits));
                bankLoanCapacity = Money(availUnits * BANK_LOAN_UNIT_VALUE);
            }
        }

        // 4) 银行自动扩建：现金池 > 银行数×50×100万 时扩建一级
        if (bankLevels > 0 &&
            bankLevels < bld.getMaxBanks() &&
            inQueueCount[BANK] == 0 &&
            bankCash > Money(bankLevels * BANK_MAX_LOAN_PER_LEVEL * BANK_LOAN_UNIT_VALUE)) {
            bld.placeOrder(BANK, OWNER_INITIAL);
        }
    }
    // ==========================================================

    // ===== 计算实际消费（预算约束） =====
    Money urbanBudget = clamp(classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST], Money(0), CLASS_CASH_MAX_MONEY);
    Money idealCost = Money(0);
    for (int i = 0; i < NUM_GOODS; ++i) idealCost += urbanConsumerTarget[i] * prices[i];
    Money actualCost = std::min(urbanBudget, idealCost);
    Money scale = (idealCost > Money(1e-6)) ? (actualCost / idealCost) : Money(1);
    array<Money, NUM_GOODS> consumerActual{}, consumerSpending{};
    for (int i = 0; i < NUM_GOODS; ++i) {
        consumerActual[i] = urbanConsumerTarget[i] * scale;
        consumerSpending[i] = consumerActual[i] * prices[i];
    }

    // ===== 价格更新 =====
    double laborDebt = max(0.0, (-classCash[LABORER]).toDouble());
    double debtFactor = 1.0 + laborDebt / LABOR_DEBT_SCALE;

    const double oversupplyGain = 1.5;
    const double dt_local = 0.2;
    constexpr double PRICE_MAX_MULTIPLE = 20.0;
    constexpr double PRICE_MIN_MULTIPLE = 0.05;
    for (int i = 0; i < NUM_GOODS; ++i) {
        if (i == constrIdx) continue;
        double rawExcess = (realIn[i] + consumerActual[i] - realOut[i]).toDouble();
        double excessDemand = (rawExcess >= 0) ? rawExcess : rawExcess * oversupplyGain;
        double G = fabs(realOut[i].toDouble()) + fabs(realIn[i].toDouble()) + fabs(consumerActual[i].toDouble()) + 1.0;
        m[i] = Money(inertiaCoeff * G);
        Money dynamicB = b[i] * Money(debtFactor);
        Money restoring = dynamicB * (prices[i] - dynamicReferencePrice[i]);
        Money rho = Money(dampRatio * 2.0 * sqrt(m[i].toDouble() * dynamicB.toDouble()));
        Money acc = (Money(excessDemand) - rho * v[i] - restoring) / m[i];
        v[i] += acc * Money(dt_local);
        prices[i] += v[i] * Money(dt_local);
        prices[i] = clamp(prices[i],
                          dynamicReferencePrice[i] * Money(PRICE_MIN_MULTIPLE),
                          dynamicReferencePrice[i] * Money(PRICE_MAX_MULTIPLE));
        if (!isfinite(prices[i]) || prices[i] <= Money(0))
            prices[i] = dynamicReferencePrice[i];
        if (prices[i] < Money(0.1)) prices[i] = Money(0.1);
    }

    // ===== 实际消费扣款（仅从正现金池扣） =====
    array<Money, CLASS_COUNT> classBeforeSpending = classCash;
    if (actualCost > Money(0)) {
        // 计算每个阶级的正现金及其总和
        array<Money, CLASS_COUNT> positiveCash;
        Money positiveTotal = Money(0);
        for (int c = 0; c < CLASS_COUNT; ++c) {
            positiveCash[c] = max(Money(0), classBeforeSpending[c]);
            positiveTotal += positiveCash[c];
        }
        if (positiveTotal > Money(0)) {
            Money consume = std::min(actualCost, positiveTotal);
            Money scalePay = consume / positiveTotal;
            for (int c = 0; c < CLASS_COUNT; ++c) {
                classCash[c] -= positiveCash[c] * scalePay;
            }
        }
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        classLastSpending[c] = classBeforeSpending[c] - classCash[c];
        clampMoney(classCash[c]);
    }
    latestConsumerActual = consumerActual;

    // 满意度（仅城市人口）
    double urbanEssentialsSat = 0.0;
    double urbanEssentialsDemand = 0.0;
    for (int good : groupGoods[GRP_SIMPLE_CLOTHES]) {
        urbanEssentialsDemand += urbanConsumerTarget[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
        urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_SIMPLE_CLOTHES];
    }
    for (int good : groupGoods[GRP_BASIC_FOOD]) {
        urbanEssentialsDemand += urbanConsumerTarget[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
        urbanEssentialsSat    += consumerActual[good].toDouble() * valueCoeff[good][GRP_BASIC_FOOD];
    }
    if (urbanEssentialsDemand > 1e-9)
        satisfaction = std::clamp(urbanEssentialsSat / urbanEssentialsDemand, 0.0, 1.0);
    else
        satisfaction = 1.0;

    // 幸福度与购买抑制系数挂钩
    double suppressionPenalty = 1.0 / (1.0 + (debtFactor - 1.0) * 0.5);
    satisfaction *= suppressionPenalty;
    satisfaction = std::clamp(satisfaction, 0.0, 1.0);

    // ===== 建筑投入成本 =====
    array<Money, TYPE_COUNT> inputCostByBuilding{};
    array<Money, NUM_GOODS> intermediatePayment{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        Money cost = Money(0);
        for (int g = 0; g < NUM_GOODS; ++g) {
            Money amt = Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
            Money pay = amt * prices[g];
            cost += pay;
            intermediatePayment[g] += pay;
        }
        inputCostByBuilding[t] = cost;
        bld.addCash(t, -cost);
    }

    // ==================== 新增：建筑贷款：现金池为负时向银行借款 ====================
    if (bld.getBuildingCounts()[BANK] > 0) {
        Money bankCash = bld.getCashPools()[BANK];
        int bankLevels = bld.getBuildingCounts()[BANK];

        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (t == BANK || bld.getBuildingCounts()[t] == 0) continue;
            Money cash = bld.getCashPools()[t];
            if (cash >= Money(0)) continue;

            // 每级建筑最多贷一笔（100万）
            int maxLoans = bld.getBuildingCounts()[t];
            int currentLoans = buildingLoanCount[t];
            int canBorrow = maxLoans - currentLoans;
            if (canBorrow <= 0) continue;

            Money needed = -cash;
            int neededUnits = (int)std::ceil(needed.toDouble() / BANK_LOAN_UNIT_VALUE);
            int borrowUnits = std::min(neededUnits, canBorrow);

            int bankAffordableUnits = (int)std::floor(bankCash.toDouble() / BANK_LOAN_UNIT_VALUE);
            int actualBorrow = std::min(borrowUnits, bankAffordableUnits);
            if (actualBorrow <= 0) continue;

            Money borrowAmount = Money(actualBorrow * BANK_LOAN_UNIT_VALUE);
            bld.addCash(t, borrowAmount);          // 建筑获得现金
            bld.addCash(BANK, -borrowAmount);      // 银行现金减少
            buildingLoanCount[t] += actualBorrow;

            bankCash = bld.getCashPools()[BANK];   // 更新本地变量
        }
    }

    // ===== 建造过程 =====
    Money pConstr = prices[constrIdx];
    Money availConstr = formalOut[constrIdx];
    lastConstrProduced = availConstr;                                    // 记录本周期建造力产出
    Money soldConstr = bld.processConstruction(availConstr, pConstr, investmentPool);
    lastConstrUsed = soldConstr;                                         // 记录实际使用
    Money constrRevenue = soldConstr * pConstr;

    // ===== 总收入 =====
    array<Money, NUM_GOODS> totalSalesValue{};
    for (int g = 0; g < NUM_GOODS; ++g)
        totalSalesValue[g] = consumerSpending[g] + intermediatePayment[g];
    totalSalesValue[constrIdx] += constrRevenue;

    array<Money, TYPE_COUNT> revenueByBuilding{};
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

    // 银行铸币
    const auto& bankBt = bld.getTemplates()[BANK];
    Money goldPrice = prices[goldIdx];
    if (bld.getBuildingCounts()[BANK] > 0 && goldPrice > Money(0)) {
        Money goldConsumed = Money(bld.getBuildingCounts()[BANK]) * Money(bankBt.inputs[goldIdx]) * Money(activityRate[BANK]);
        Money bankRevenue = goldConsumed * goldPrice * Money(BANK_MONEY_MULTIPLIER);
        revenueByBuilding[BANK] += bankRevenue;
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        bld.addCash(t, revenueByBuilding[t]);
    }

    // ===== 利润分配、工资调整、奖金、投资池 =====
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK || t == CONST_DEPT || bld.getBuildingCounts()[t] == 0) continue;
        Money netProfit = revenueByBuilding[t] - inputCostByBuilding[t] - laborCostByBuilding[t];
        Money profitPerLevel = (bld.getBuildingCounts()[t] > 0) ? netProfit / Money(bld.getBuildingCounts()[t]) : Money(0);

        // ===== 工资调整：仅在劳动力短缺时用净利的50%加薪 =====
        if (laborShortage && actualEmployment[t] > 0 && netProfit > Money(0)) {
            Money raiseFund = netProfit * Money(0.5);
            Money perWorkerRaise = raiseFund / Money(actualEmployment[t]);
            buildingWages[t] += perWorkerRaise;
            netProfit -= raiseFund;
        } else if (netProfit < Money(0) && buildingWages[t] > Money(0.5)) {
            buildingWages[t] *= Money(0.98);
        }
        if (buildingWages[t] < Money(0.5)) buildingWages[t] = Money(0.5);

        // ===== 股东利润分配（金融区所有者） =====
        const auto& owned = bld.getOwnedBuildings()[t];
        for (int o = 0; o < OWNER_COUNT; ++o) {
            Money shareProfit = profitPerLevel * Money(owned[o]);
            if (shareProfit <= Money(0) && o != OWNER_FINANCE) continue;
            if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) { /* 利润留存在企业 */ }
            else if (o == OWNER_FINANCE) {
                bld.addCash(t, -shareProfit);
                bld.addCash(FINANCE, shareProfit);
            }
        }

        // ===== 奖金分配（股东分配后，从净利提取30%） =====
        if (netProfit > Money(0)) {
            Money desiredBonus = netProfit * Money(0.3);
            Money cashNow = bld.getCashPools()[t];
            Money actualBonus = std::min(desiredBonus, cashNow);
            if (actualBonus > Money(0)) {
                bld.addCash(t, -actualBonus);
                classCash[LABORER]    += actualBonus * Money(0.75);
                classCash[ENGINEER]   += actualBonus * Money(0.20);
                classCash[CAPITALIST] += actualBonus * Money(0.05);
                for (int c = 0; c < CLASS_COUNT; ++c) clampMoney(classCash[c]);
                netProfit -= actualBonus;
                profitPerLevel = (bld.getBuildingCounts()[t] > 0) ? netProfit / Money(bld.getBuildingCounts()[t]) : Money(0);
            }
        }

        // ===== 企业留存与投资池 =====
        Money curCash = bld.getCashPools()[t];
        Money targetCash = Money(bld.getBuildingCounts()[t]) * Money(500000.0);

        // ===== 新增：建筑贷款还款（优先于留存利润） =====
        if (buildingLoanCount[t] > 0 && curCash > targetCash) {
            Money excessForRepay = curCash - targetCash;
            int repayUnits = std::min(buildingLoanCount[t],
                                      (int)std::floor(excessForRepay.toDouble() / BANK_LOAN_UNIT_VALUE));
            if (repayUnits > 0) {
                Money repayAmount = Money(repayUnits * BANK_LOAN_UNIT_VALUE);
                bld.addCash(t, -repayAmount);
                bld.addCash(BANK, repayAmount);
                buildingLoanCount[t] -= repayUnits;
                curCash = bld.getCashPools()[t];   // 更新用于后续判断
            }
        }

        if (curCash > targetCash) {
            Money excess = curCash - targetCash;
            Money excessPerLevel = excess / Money(bld.getBuildingCounts()[t]);
            bld.addCash(t, -excess);
            for (int o = 0; o < OWNER_COUNT; ++o) {
                Money share = excessPerLevel * Money(owned[o]);
                if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) {
                    investmentPool += share;
                } else if (o == OWNER_FINANCE) {
                    bld.addCash(FINANCE, share);
                }
            }
            investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
        }

        // ===== 股息（初始私有者） =====
        Money privateProfit = profitPerLevel * Money(owned[OWNER_INITIAL]);
        if (privateProfit > Money(0)) {
            Money dividend = privateProfit * Money(0.1);
            bld.addCash(t, -dividend);
            classCash[CAPITALIST] += dividend;
            clampMoney(classCash[CAPITALIST]);
        }
    }

    // ===== 金融区特殊处理（净利全转投资池） =====
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == FINANCE && bld.getBuildingCounts()[t] > 0) {
            Money revenue = revenueByBuilding[FINANCE];
            Money cost = inputCostByBuilding[FINANCE] + laborCostByBuilding[FINANCE];
            Money net = revenue - cost;
            if (net > Money(0)) {
                bld.addCash(FINANCE, -net);
                investmentPool += net;
            }
            Money curCash = bld.getCashPools()[FINANCE];
            Money targetCash = Money(bld.getBuildingCounts()[FINANCE]) * Money(500000.0);
            if (curCash > targetCash) {
                Money excess = curCash - targetCash;
                bld.addCash(FINANCE, -excess);
                investmentPool += excess;
            }
            investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
        }
    }

    // ===== 建造部门特殊处理（按固定利润率定价，收入应覆盖成本，若有剩余转投资池） =====
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        Money revenue = revenueByBuilding[CONST_DEPT];
        Money cost = inputCostByBuilding[CONST_DEPT] + laborCostByBuilding[CONST_DEPT];
        Money net = revenue - cost;
        if (net > Money(0)) {
            bld.addCash(CONST_DEPT, -net);
            investmentPool += net;
            investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
        }
    }

    // 银行现金池硬上限
    if (bld.getBuildingCounts()[BANK] > 0 && bld.getCashPools()[BANK] > Money(1e12L)) {
        Money excess = bld.getCashPools()[BANK] - Money(1e12L);
        bld.addCash(BANK, -excess);
        investmentPool += excess;
        investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
    }

    {
        Money constCash = bld.getCashPools()[CONST_DEPT];
        Money constTarget = Money(bld.getBuildingCounts()[CONST_DEPT]) * Money(500000.0);
        if (constCash > constTarget) {
            Money excess = constCash - constTarget;
            bld.addCash(CONST_DEPT, -excess);
            investmentPool += excess;
            investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
        }
    }

    // 实际利润率
    std::array<double, TYPE_COUNT> actualProfitRates;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) {
            actualProfitRates[t] = 0.0;
        } else {
            Money totalCost = inputCostByBuilding[t] + laborCostByBuilding[t];
            Money totalRevenue = revenueByBuilding[t];
            if (buildingOutput[t] < Money(1e-6) && !bld.getTemplates()[t].isFinancial) {
                actualProfitRates[t] = 0.0;
            } else if (totalCost.abs() > Money(1e-6)) {
                actualProfitRates[t] = ((totalRevenue - totalCost) / totalCost).toDouble();
            } else {
                actualProfitRates[t] = 0.0;
            }
        }
    }
    bld.updateActualProfitRates(actualProfitRates);

    // ===== 总货币供给 =====
    totalMoneySupply = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        Money val = bld.getCashPools()[t];
        if (isfinite(val) && val > Money(0))   // 新增：仅计入正现金池
            totalMoneySupply += val;
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        Money val = classCash[c];
        if (isfinite(val) && val > Money(0))   // 新增：仅计入正现金
            totalMoneySupply += val;
    }
    if (isfinite(investmentPool) && investmentPool > Money(0))
        totalMoneySupply += investmentPool;
    totalMoneySupply = clamp(totalMoneySupply, -MONEY_SUPPLY_MAX_MONEY, MONEY_SUPPLY_MAX_MONEY);
    if (!isfinite(totalMoneySupply)) totalMoneySupply = Money(0);

    // ===== GDP =====
    Money gdp = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        Money rev = revenueByBuilding[t];
        Money cost = inputCostByBuilding[t];
        gdp += rev - cost;
    }

    // ===== 人口增长（含固定死亡率） =====
    {
        constexpr double BASE_ANNUAL_GROWTH = 0.01;
        constexpr double MAX_ANNUAL_GROWTH = 0.10;
        constexpr double MIN_ANNUAL_GROWTH = -0.05;
        constexpr double SATISFACTION_NEUTRAL = 0.60;
        constexpr double WEEKS_PER_YEAR = 52.0;
        constexpr double FIXED_DEATH_RATE_ANNUAL = 0.03;   // 新增：固定死亡率 3%/年

        double effect = (satisfaction - SATISFACTION_NEUTRAL) / (1.0 - SATISFACTION_NEUTRAL);
        effect = std::clamp(effect, -1.0, 1.0);

        double annualGrowth = BASE_ANNUAL_GROWTH;
        if (effect >= 0) annualGrowth += effect * (MAX_ANNUAL_GROWTH - BASE_ANNUAL_GROWTH);
        else annualGrowth += effect * (BASE_ANNUAL_GROWTH - MIN_ANNUAL_GROWTH);
        annualGrowth = std::clamp(annualGrowth, MIN_ANNUAL_GROWTH, MAX_ANNUAL_GROWTH);

        double weeklyGrowth = annualGrowth / WEEKS_PER_YEAR;
        weeklyGrowth -= FIXED_DEATH_RATE_ANNUAL / WEEKS_PER_YEAR;  // 新增：扣除周死亡率

        double urbanPop = population - subsistencePop;
        if (urbanPop < 0.0) urbanPop = 0.0;
        double subsPop = subsistencePop;

        urbanPop *= (1.0 + weeklyGrowth);
        subsPop  *= (1.0 + weeklyGrowth);

        if (urbanPop < 0.0) urbanPop = 0.0;
        if (subsPop  < 0.0) subsPop  = 0.0;

        population = urbanPop + subsPop;
        if (population < 1000.0) population = 1000.0;
        maxLabor = population;
    }

    if (stepCount % 52 == 0) bld.adjustEmployment();
    bld.checkDecay(stepCount, investmentPool, classCash);

    // 最终钳制
    for (auto& c : classCash) clampMoney(c);
    investmentPool = clamp(investmentPool, Money(0), INVEST_POOL_MAX_MONEY);
    bld.clampAllCash();

    gdpHist.push_back(gdp);
    buildingHist.push_back(bld.getBuildingCounts());
    outputHist.push_back(realOut);
    priceHist.push_back(prices);
    cashPoolHist.push_back(bld.getCashPools());
    populationHist.push_back(population);
    latestRealOut = realOut;
    latestBuildingOutput = buildingOutput;
}

void LocalMarket::aiBuild() {
    // 如果本周期建造力有剩余（产出 > 实际使用），则禁止自动扩建建造部门
    Money unusedConstr = lastConstrProduced - lastConstrUsed;
    if (unusedConstr > Money(0.5)) {
        bld.setAllowAutoConstExpansion(false);
    } else {
        bld.setAllowAutoConstExpansion(true);
    }
    bld.aiBuild(aiProfitThreshold, prices, buildingWages, maxLabor, actualEmploymentRate);
}

void LocalMarket::playerBuild(int typeIdx, int count) {
    bld.placePlayerOrder(typeIdx, count, true);
}

void LocalMarket::playerDemolish(int typeIdx, int count) {
    bld.demolishBuildings(typeIdx, count, stepCount, investmentPool);
}

bool LocalMarket::performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to) {
    return bld.transferOwnership(typeIdx, count, from, to, investmentPool, classCash) != Money(0);
}