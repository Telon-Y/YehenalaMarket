// local_market.cpp
#include "local_market.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

using namespace std;

LocalMarket::LocalMarket() {
    for (int i = 0; i < NUM_GOODS; ++i)
        goodIndex[commodityNames[i]] = i;

    prices = referencePrice;
    v.fill(0.0);
    m.fill(1.0);
    b = priceSuppressBase;
    maxLabor = population;

    baseReferencePrice = referencePrice;
    dynamicReferencePrice = referencePrice;

    latestConsumerTarget.fill(0.0);
    latestConsumerActual.fill(0.0);
    latestPotentialIn.fill(0.0);
    latestRealOut.fill(0.0);
    latestBuildingOutput.fill(0.0);
    totalLaborers = 0.0;
    totalEngineers = 0.0;
    totalCapitalists = 0.0;

    double initMoney = 500'000'000.0;
    classCash[LABORER]    = initMoney * 0.4;
    classCash[ENGINEER]   = initMoney * 0.4;
    classCash[CAPITALIST] = initMoney * 0.2;
    investmentPool = 0.0;
    totalDebt = 0.0;
    loanBalance.fill(0.0);
    classLastSpending.fill(0.0);

    initialTotalMoneySupply = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) initialTotalMoneySupply += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) initialTotalMoneySupply += classCash[c];
    totalMoneySupply = initialTotalMoneySupply;
    priceLevel = 1.0;
    targetPriceLevel = 1.0;

    // 初始化工资
    buildingWages.fill(averageWage);

    // 初始化自给农
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

    // ===== 清理 NaN/Inf =====
    auto clampMoney = [](double& x) {
        if (!std::isfinite(x)) x = 0.0;
        x = std::clamp(x, 0.0, 1e15);
    };

    for (auto& c : classCash) clampMoney(c);
    if (!std::isfinite(investmentPool)) investmentPool = 0.0;
    investmentPool = std::clamp(investmentPool, 0.0, 1e12);
    bld.clampAllCash();

    // ===== 价格水平更新（缓慢跟随活跃货币） =====
    if (stepCount > 1 && initialTotalMoneySupply > 0.0) {
        double activeMoney = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (t == BANK || t == FINANCE || t == CONST_DEPT) continue;
            activeMoney += bld.getCashPools()[t];
        }
        for (int c = 0; c < CLASS_COUNT; ++c) activeMoney += classCash[c];
        activeMoney += investmentPool;
        activeMoney = std::clamp(activeMoney, 0.0, 1e15);

        double moneyRatio = activeMoney / std::max(1.0, initialTotalMoneySupply);
        targetPriceLevel = moneyRatio;
        priceLevel += 0.005 * (targetPriceLevel - priceLevel);
        priceLevel = std::clamp(priceLevel, 0.01, 1e6);
        for (int i = 0; i < NUM_GOODS; ++i) {
            dynamicReferencePrice[i] = baseReferencePrice[i] * priceLevel;
            if (!std::isfinite(dynamicReferencePrice[i]) || dynamicReferencePrice[i] <= 0)
                dynamicReferencePrice[i] = baseReferencePrice[i];
        }
    }

    auto inQueueCount = bld.getInQueueCounts();
    int usedFarms = bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int idleLand = max(0, 10000 - usedFarms);
    subsistenceFarms = idleLand;

    maxLabor = population;

    auto estOutputPerBuilding = bld.calculateBaseOutputRates(maxLabor);
    array<double, TYPE_COUNT> activityRate;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) { activityRate[t] = 0.0; continue; }
        activityRate[t] = estOutputPerBuilding[t];
    }

    // 供应比率迭代（30次）
    array<double, TYPE_COUNT> supplyRatio;
    supplyRatio.fill(1.0);
    for (int iter = 0; iter < 30; ++iter) {
        array<double, NUM_GOODS> tempOut{}, tempIn{};
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            double cr = activityRate[t] * supplyRatio[t];
            if (!bt.isFinancial && bt.outputGood >= 0)
                tempOut[bt.outputGood] += bld.getBuildingCounts()[t] * cr;
            for (int g = 0; g < NUM_GOODS; ++g)
                tempIn[g] += bld.getBuildingCounts()[t] * bt.inputs[g] * cr;
        }
        array<double, NUM_GOODS> ratio;
        for (int g = 0; g < NUM_GOODS; ++g) {
            ratio[g] = (tempIn[g] > 0 && tempOut[g] < tempIn[g]) ? tempOut[g] / tempIn[g] : 1.0;
        }
        array<double, TYPE_COUNT> newRatio;
        newRatio.fill(1.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            double minR = 1.0;
            for (int g = 0; g < NUM_GOODS; ++g)
                if (bld.getTemplates()[t].inputs[g] > 0) minR = min(minR, ratio[g]);
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

    // ===== 劳动力分配：按工资高低分配 =====
    array<double, TYPE_COUNT> idealEmployment;
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

    // 判断劳动力是否短缺
    bool laborShortage = totalIdeal > population;

    // 按工资降序排列类型索引
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

    double employedTotal = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) employedTotal += actualEmployment[t];

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

    array<double, NUM_GOODS> formalOut{}, realOut{}, realIn{};
    array<double, TYPE_COUNT> buildingOutput{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        if (!bt.isFinancial && bt.outputGood >= 0) {
            double outAmt = bld.getBuildingCounts()[t] * cr;
            formalOut[bt.outputGood] += outAmt;
            realOut[bt.outputGood] += outAmt;
            buildingOutput[t] = outAmt;
        }
        for (int g = 0; g < NUM_GOODS; ++g)
            realIn[g] += bld.getBuildingCounts()[t] * bt.inputs[g] * cr;
    }

    const double popScale = 1.0 / 100000.0;
    array<double, GROUP_COUNT> subGroupDemand{}, urbanGroupDemand{};
    for (int g = 0; g < GROUP_COUNT; ++g) {
        subGroupDemand[g] = demandTable[0][g] * subsistencePop * popScale;
        urbanGroupDemand[g] = demandTable[0][g] * totalLaborers * popScale
                            + demandTable[1][g] * totalEngineers * popScale
                            + demandTable[2][g] * totalCapitalists * popScale;
    }
    double totalClassFund = classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST];
    double wealthPerCap = totalClassFund / max(population, 1.0);
    double luxuryFactor = clamp(wealthPerCap / 1500.0, 0.0, 3.0);

    // 奢侈品因子平滑
    const double alphaSmooth = 0.2;
    smoothedLuxuryFactor = smoothedLuxuryFactor + alphaSmooth * (luxuryFactor - smoothedLuxuryFactor);
    luxuryFactor = smoothedLuxuryFactor;

    urbanGroupDemand[GRP_STANDARD_CLOTHES] *= (1.0 + luxuryFactor * 0.4);
    urbanGroupDemand[GRP_HOUSING] += population * popScale * luxuryFactor * 30;

    // 消费平滑替代
    auto computeTarget = [&](const array<double, GROUP_COUNT>& gd, array<double, NUM_GOODS>& target) {
        target.fill(0.0);
        for (int g = 0; g < GROUP_COUNT; ++g) {
            double remain = gd[g];
            if (remain <= 1e-9) continue;
            vector<int> goods = groupGoods[g];

            vector<pair<int, double>> candidates;
            double totalWeight = 0.0;
            for (int good : goods) {
                double u = valueCoeff[good][g];
                if (u <= 0.0 || prices[good] <= 1e-9) continue;
                double weight = u / prices[good];
                candidates.push_back({good, weight});
                totalWeight += weight;
            }

            if (totalWeight <= 1e-12 || candidates.empty()) continue;

            for (const auto& [good, weight] : candidates) {
                double u = valueCoeff[good][g];
                double share = weight / totalWeight;
                double want = share * remain / u;
                target[good] += want;
            }
        }
    };
    array<double, NUM_GOODS> consumerTarget, urbanConsumerTarget;
    computeTarget(urbanGroupDemand, urbanConsumerTarget);
    consumerTarget = urbanConsumerTarget;
    latestConsumerTarget = consumerTarget;
    latestPotentialIn = realIn;

    // ===== 工资发放（使用各建筑类型工资） =====
    std::array<double, TYPE_COUNT> laborCostByBuilding{};
    {
        std::array<double, CLASS_COUNT> wageIncome{};
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (actualEmployment[t] <= 0) continue;
            double lc = actualEmployment[t] * buildingWages[t];
            laborCostByBuilding[t] = lc;
            bld.addCash(t, -lc);
            wageIncome[LABORER]    += lc * 0.75;
            wageIncome[ENGINEER]   += lc * 0.20;
            wageIncome[CAPITALIST] += lc * 0.05;
        }
        for (int c = 0; c < CLASS_COUNT; ++c) classCash[c] += wageIncome[c];
    }

    // 建造力价格拟合
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        const auto& bt = bld.getTemplates()[CONST_DEPT];
        double laborCostPU = bt.laborPerUnit * averageWage; // 用基准工资用于参考
        double inputCostPU = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g) inputCostPU += prices[g] * bt.inputs[g];
        double refConstr = laborCostPU + inputCostPU;
        prices[constrIdx] += 0.1 * (refConstr - prices[constrIdx]);
        if (prices[constrIdx] < 0.1) prices[constrIdx] = 0.1;
    }

    // ===== 计算实际消费（预算约束） =====
    double urbanBudget = classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST];
    double idealCost = 0.0;
    for (int i = 0; i < NUM_GOODS; ++i) idealCost += urbanConsumerTarget[i] * prices[i];
    double actualCost = min(urbanBudget, idealCost);
    double scale = (idealCost > 1e-6) ? (actualCost / idealCost) : 1.0;
    array<double, NUM_GOODS> consumerActual{}, consumerSpending{};
    for (int i = 0; i < NUM_GOODS; ++i) {
        consumerActual[i] = urbanConsumerTarget[i] * scale;
        consumerSpending[i] = consumerActual[i] * prices[i];
    }

    // ===== 价格更新：使用实际消费 =====
    const double oversupplyGain = 1.5;
    const double dt_local = 0.2;
    constexpr double PRICE_MAX_MULTIPLE = 20.0;
    constexpr double PRICE_MIN_MULTIPLE = 0.05;
    for (int i = 0; i < NUM_GOODS; ++i) {
        double rawExcess = realIn[i] + consumerActual[i] - realOut[i];
        double excessDemand = (rawExcess >= 0) ? rawExcess : rawExcess * oversupplyGain;
        double G = fabs(realOut[i]) + fabs(realIn[i]) + fabs(consumerActual[i]) + 1.0;
        m[i] = inertiaCoeff * G;
        double restoring = b[i] * (prices[i] - dynamicReferencePrice[i]);
        double rho = dampRatio * 2.0 * sqrt(m[i] * b[i]);
        double acc = (excessDemand - rho * v[i] - restoring) / m[i];
        v[i] += acc * dt_local;
        prices[i] += v[i] * dt_local;
        prices[i] = std::clamp(prices[i],
                               dynamicReferencePrice[i] * PRICE_MIN_MULTIPLE,
                               dynamicReferencePrice[i] * PRICE_MAX_MULTIPLE);
        if (!std::isfinite(prices[i]) || prices[i] <= 0)
            prices[i] = dynamicReferencePrice[i];
        if (prices[i] < 0.1) prices[i] = 0.1;
    }

    // ===== 实际消费扣款 =====
    array<double, CLASS_COUNT> classBeforeSpending = classCash;
    if (totalClassFund > 0 && actualCost > 0) {
        double ls = classBeforeSpending[LABORER] / totalClassFund;
        double es = classBeforeSpending[ENGINEER] / totalClassFund;
        double cs = classBeforeSpending[CAPITALIST] / totalClassFund;
        classCash[LABORER]    -= actualCost * ls;
        classCash[ENGINEER]   -= actualCost * es;
        classCash[CAPITALIST] -= actualCost * cs;
    }
    for (int c = 0; c < CLASS_COUNT; ++c)
        classLastSpending[c] = classBeforeSpending[c] - classCash[c];
    latestConsumerActual = consumerActual;

    // 满意度（仅城市人口）
    double urbanEssentialsSat = 0.0;
    double urbanEssentialsDemand = 0.0;
    for (int good : groupGoods[GRP_SIMPLE_CLOTHES]) {
        urbanEssentialsDemand += urbanConsumerTarget[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
        urbanEssentialsSat    += consumerActual[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
    }
    for (int good : groupGoods[GRP_BASIC_FOOD]) {
        urbanEssentialsDemand += urbanConsumerTarget[good] * valueCoeff[good][GRP_BASIC_FOOD];
        urbanEssentialsSat    += consumerActual[good] * valueCoeff[good][GRP_BASIC_FOOD];
    }
    if (urbanEssentialsDemand > 1e-9)
        satisfaction = std::clamp(urbanEssentialsSat / urbanEssentialsDemand, 0.0, 1.0);
    else
        satisfaction = 1.0;

    // ===== 建筑投入成本 =====
    array<double, TYPE_COUNT> inputCostByBuilding{};
    array<double, NUM_GOODS> intermediatePayment{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        double cost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g) {
            double amt = bld.getBuildingCounts()[t] * bt.inputs[g] * cr;
            double pay = amt * prices[g];
            cost += pay;
            intermediatePayment[g] += pay;
        }
        inputCostByBuilding[t] = cost;
        bld.addCash(t, -cost);
    }

    // ===== 建造过程 =====
    double pConstr = prices[constrIdx];
    double availConstr = formalOut[constrIdx];
    double soldConstr = bld.processConstruction(availConstr, pConstr, investmentPool);
    double constrRevenue = soldConstr * pConstr;

    // ===== 总收入 =====
    array<double, NUM_GOODS> totalSalesValue{};
    for (int g = 0; g < NUM_GOODS; ++g)
        totalSalesValue[g] = consumerSpending[g] + intermediatePayment[g];
    totalSalesValue[constrIdx] += constrRevenue;

    array<double, TYPE_COUNT> revenueByBuilding{};
    for (int g = 0; g < NUM_GOODS; ++g) {
        if (formalOut[g] <= 0 || totalSalesValue[g] <= 0) continue;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            if (bt.outputGood != g) continue;
            double share = (bld.getBuildingCounts()[t] * activityRate[t]) / formalOut[g];
            revenueByBuilding[t] += totalSalesValue[g] * share;
        }
    }

    // 银行铸币
    if (bld.getBuildingCounts()[BANK] > 0) {
        double bankRevenue = bld.getBuildingCounts()[BANK] * 200'000.0;
        revenueByBuilding[BANK] += bankRevenue;
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        bld.addCash(t, revenueByBuilding[t]);
    }

    // ===== 利润分配、工资调整、投资池 =====
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK || t == CONST_DEPT || bld.getBuildingCounts()[t] == 0) continue;
        double netProfit = revenueByBuilding[t] - inputCostByBuilding[t] - laborCostByBuilding[t];
        double profitPerLevel = (bld.getBuildingCounts()[t] > 0) ? netProfit / bld.getBuildingCounts()[t] : 0.0;

        // 工资调整：仅在劳动力短缺时用利润加薪，否则不加薪
        if (laborShortage && actualEmployment[t] > 0 && netProfit > 0) {
            double raiseFund = netProfit * 0.5;
            double perWorkerRaise = raiseFund / actualEmployment[t];
            perWorkerRaise = std::min(perWorkerRaise, buildingWages[t] * 0.05);
            buildingWages[t] += perWorkerRaise;
        } else if (netProfit < 0 && buildingWages[t] > 0.5) {
            // 亏损时工资缓慢下滑
            buildingWages[t] *= 0.98;
        }
        if (buildingWages[t] < 0.5) buildingWages[t] = 0.5;

        if (t == FINANCE) {
            if (netProfit > 0) {
                bld.addCash(FINANCE, -netProfit);
                investmentPool += netProfit;
            }
            double curCash = bld.getCashPools()[FINANCE];
            double targetCash = bld.getBuildingCounts()[FINANCE] * 500000.0;
            if (curCash > targetCash) {
                double excess = curCash - targetCash;
                bld.addCash(FINANCE, -excess);
                investmentPool += excess;
            }
            continue;
        }

        const auto& owned = bld.getOwnedBuildings()[t];
        for (int o = 0; o < OWNER_COUNT; ++o) {
            double shareProfit = profitPerLevel * owned[o];
            if (shareProfit <= 0 && o != OWNER_FINANCE) continue;
            if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) { /* 利润留存在企业 */ }
            else if (o == OWNER_FINANCE) {
                bld.addCash(t, -shareProfit);
                bld.addCash(FINANCE, shareProfit);
            }
        }

        double curCash = bld.getCashPools()[t];
        double targetCash = bld.getBuildingCounts()[t] * 500000.0;
        if (curCash > targetCash) {
            double excess = curCash - targetCash;
            double excessPerLevel = excess / bld.getBuildingCounts()[t];
            bld.addCash(t, -excess);
            for (int o = 0; o < OWNER_COUNT; ++o) {
                double share = excessPerLevel * owned[o];
                if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) {
                    investmentPool += share;
                } else if (o == OWNER_FINANCE) {
                    bld.addCash(FINANCE, share);
                }
            }
        }

        double privateProfit = profitPerLevel * owned[OWNER_INITIAL];
        if (privateProfit > 0) {
            double dividend = privateProfit * 0.1;
            bld.addCash(t, -dividend);
            classCash[CAPITALIST] += dividend;
        }
    }

    // 银行现金池硬上限
    if (bld.getBuildingCounts()[BANK] > 0 && bld.getCashPools()[BANK] > 1e12) {
        double excess = bld.getCashPools()[BANK] - 1e12;
        bld.addCash(BANK, -excess);
        investmentPool += excess;
        if (!std::isfinite(investmentPool)) investmentPool = 0.0;
        investmentPool = std::clamp(investmentPool, 0.0, 1e12);
    }

    {
        double constCash = bld.getCashPools()[CONST_DEPT];
        double constTarget = bld.getBuildingCounts()[CONST_DEPT] * 500000.0;
        if (constCash > constTarget) {
            double excess = constCash - constTarget;
            bld.addCash(CONST_DEPT, -excess);
            investmentPool += excess;
        }
    }

    // 实际利润率
    std::array<double, TYPE_COUNT> actualProfitRates;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) {
            actualProfitRates[t] = 0.0;
        } else {
            double totalCost = inputCostByBuilding[t] + laborCostByBuilding[t];
            double totalRevenue = revenueByBuilding[t];
            if (buildingOutput[t] < 1e-6 && !bld.getTemplates()[t].isFinancial) {
                actualProfitRates[t] = 0.0;
            } else if (fabs(totalCost) > 1e-6) {
                actualProfitRates[t] = (totalRevenue - totalCost) / totalCost;
            } else {
                actualProfitRates[t] = 0.0;
            }
        }
    }
    bld.updateActualProfitRates(actualProfitRates);

    // ===== 总货币供给 =====
    totalMoneySupply = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        double val = bld.getCashPools()[t];
        if (std::isfinite(val)) totalMoneySupply += val;
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        double val = classCash[c];
        if (std::isfinite(val)) totalMoneySupply += val;
    }
    if (std::isfinite(investmentPool)) totalMoneySupply += investmentPool;
    totalMoneySupply = std::clamp(totalMoneySupply, -1e15, 1e15);
    if (!std::isfinite(totalMoneySupply)) totalMoneySupply = 0.0;

    // ===== GDP =====
    double gdp = subGrain * prices[goodIndex["谷物"]] + subFabric * prices[goodIndex["织物"]] + subClothes * prices[goodIndex["服装"]];
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        double rev = revenueByBuilding[t];
        double cost = inputCostByBuilding[t];
        gdp += rev - cost;
    }

    // ===== 人口增长（只影响城市人口） =====
    {
        constexpr double BASE_ANNUAL_GROWTH = 0.01;
        constexpr double MAX_ANNUAL_GROWTH = 0.10;
        constexpr double MIN_ANNUAL_GROWTH = -0.05;
        constexpr double SATISFACTION_NEUTRAL = 0.60;
        constexpr double WEEKS_PER_YEAR = 52.0;

        double effect = (satisfaction - SATISFACTION_NEUTRAL) / (1.0 - SATISFACTION_NEUTRAL);
        effect = std::clamp(effect, -1.0, 1.0);
        double annualGrowth = BASE_ANNUAL_GROWTH;
        if (effect >= 0) annualGrowth += effect * (MAX_ANNUAL_GROWTH - BASE_ANNUAL_GROWTH);
        else annualGrowth += effect * (BASE_ANNUAL_GROWTH - MIN_ANNUAL_GROWTH);
        annualGrowth = std::clamp(annualGrowth, MIN_ANNUAL_GROWTH, MAX_ANNUAL_GROWTH);
        double weeklyGrowth = annualGrowth / WEEKS_PER_YEAR;

        double urbanPop = population - subsistencePop;
        if (urbanPop < 0.0) urbanPop = 0.0;
        urbanPop *= (1.0 + weeklyGrowth);
        if (urbanPop < 0.0) urbanPop = 0.0;
        population = urbanPop + subsistencePop;
        if (population < 1000.0) population = 1000.0;
        maxLabor = population;
    }

    if (stepCount % 52 == 0) bld.adjustEmployment();
    bld.checkDecay(stepCount, investmentPool, classCash);

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
    bld.aiBuild(aiProfitThreshold, prices, averageWage, maxLabor, actualEmploymentRate);
}
void LocalMarket::playerBuild(int typeIdx, int count) {
    bld.placePlayerOrder(typeIdx, count, true);
}

void LocalMarket::playerDemolish(int typeIdx, int count) {
    bld.demolishBuildings(typeIdx, count, stepCount, investmentPool);
}

bool LocalMarket::performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to) {
    return bld.transferOwnership(typeIdx, count, from, to, investmentPool, classCash) != 0.0;
}