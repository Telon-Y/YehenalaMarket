#include "market_simulation.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>

using namespace std;

MarketSimulation::MarketSimulation() {
    for (int i = 0; i < NUM_GOODS; ++i)
        goodIndex[commodityNames[i]] = i;

    prices = referencePrice;
    v.fill(0.0);
    m.fill(1.0);
    b = priceSuppressBase;

    buildingTemplates = createBuildingTemplates();

    buildingCounts.fill(50);
    avgProfitRates.fill(0.0);
    smoothedProfitRate.fill(0.0);
    employmentRatio.fill(1.0);
    consecutiveLowEmpWeeks.fill(0);

    for (int t = 0; t < TYPE_COUNT; ++t)
        cashPools[t] = buildingCounts[t] * 500000.0;

    maxLabor = population * laborPerCapita;

    latestConsumerTarget.fill(0.0);
    latestConsumerActual.fill(0.0);
    latestPotentialIn.fill(0.0);
    lastDemolishStep.fill(-9999);
    currentSupplyRatio.fill(1.0);
}

array<double, GROUP_COUNT> MarketSimulation::getGroupDemandPer100k() const {
    double w = averageWage;
    array<double, GROUP_COUNT> res{};
    if (w <= 5.0) {
        for (int g = 0; g < GROUP_COUNT; ++g) res[g] = demandTable[0][g];
    } else if (w >= 20.0) {
        for (int g = 0; g < GROUP_COUNT; ++g) res[g] = demandTable[2][g];
    } else if (w <= 10.0) {
        double t = (w - 5.0) / 5.0;
        for (int g = 0; g < GROUP_COUNT; ++g)
            res[g] = demandTable[0][g] * (1 - t) + demandTable[1][g] * t;
    } else {
        double t = (w - 10.0) / 10.0;
        for (int g = 0; g < GROUP_COUNT; ++g)
            res[g] = demandTable[1][g] * (1 - t) + demandTable[2][g] * t;
    }
    return res;
}

void MarketSimulation::placeOrder(int typeIdx) {
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    if (pending >= 50) return;
    constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], false});
}

void MarketSimulation::placePlayerOrder(int typeIdx, int count, bool top) {
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;

    int maxQueue = 50 - pending;
    if (maxQueue <= 0) return;
    if (count > maxQueue) count = maxQueue;

    for (int i = 0; i < count; ++i) {
        ConstructionOrder order{typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], true};
        if (top) constructionQueue.insert(constructionQueue.begin(), order);
        else     constructionQueue.push_back(order);
    }
}

void MarketSimulation::removeBuilding(int typeIndex) {
    if (typeIndex >= 0 && typeIndex < TYPE_COUNT && buildingCounts[typeIndex] > 0)
        buildingCounts[typeIndex]--;
}

void MarketSimulation::demolishBuildings(int typeIndex, int count) {
    if (typeIndex == CONST_DEPT) return;   // 禁止拆除建造部门
    if (typeIndex < 0 || typeIndex >= TYPE_COUNT || buildingCounts[typeIndex] <= 0)
        return;
    if (stepCount - lastDemolishStep[typeIndex] < demolishCooldownPeriod)
        return;
    buildingCounts[typeIndex]--;
    lastDemolishStep[typeIndex] = stepCount;
}

void MarketSimulation::step() {
    stepCount++;

    // 人口增长
    double s = satisfaction;
    if (s < 0.0) s = 0.0; else if (s > 1.0) s = 1.0;
    double r52;
    if (s <= 0.75) r52 = (0.20 / 0.75) * s - 0.20;
    else           r52 = 0.2 * (s - 0.75);
    if (r52 < -0.20) r52 = -0.20;
    if (r52 > 0.05)  r52 = 0.05;
    double r_step = pow(1.0 + r52, 1.0 / 52.0) - 1.0;
    population *= (1.0 + r_step);
    maxLabor = population * laborPerCapita;

    const int constrIdx = goodIndex["建造力"];

    // ---------- 1. 利润因子（仅用于参考，不影响产出）----------
    array<double, TYPE_COUNT> profitFactor;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) { profitFactor[t] = 0.0; continue; }
        profitFactor[t] = buildingTemplates[t].getProfitFactor(prices, averageWage);
    }

    // ★ 已移除 desiredOutputRate 计算，产出不再受利润率主动减产控制

    // ---------- 2. 劳动力约束 ----------
    // 产出 = 基础产出率 * 雇佣率 （不再乘利润率因子）
    array<double, TYPE_COUNT> estOutputPerBuilding;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) { estOutputPerBuilding[t] = 0.0; continue; }
        const auto& bt = buildingTemplates[t];
        estOutputPerBuilding[t] = bt.outputRate * employmentRatio[t];
    }

    array<double, TYPE_COUNT> laborOutput;
    for (int t = 0; t < TYPE_COUNT; ++t)
        laborOutput[t] = estOutputPerBuilding[t];   // 不乘 laborFactor

    // ---------- 3. 自给农场 ----------
    array<int, TYPE_COUNT> inQueueCount{};
    for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;
    int usedFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int subsistCount = max(0, maxTotalFarms - usedFarms);
    subsistenceFarms = subsistCount;
    double subGrain = subsistCount * 2.0;
    double subFabric = subsistCount * 1.0;
    double subClothes = subsistCount * 0.5;

    // ---------- 4. 潜在中间需求 ----------
    array<double, NUM_GOODS> potentialIn{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double desiredRate = bt.outputRate * employmentRatio[t];   // 不使用利润率因子
        for (int g = 0; g < NUM_GOODS; ++g)
            potentialIn[g] += buildingCounts[t] * bt.inputs[g] * desiredRate;
    }
    latestPotentialIn = potentialIn;   // 保存供 UI 显示全市场消费

    // ---------- 5. 原料配给（迭代计算）----------
    array<double, TYPE_COUNT> supplyRatio;
    supplyRatio.fill(1.0);
    for (int iter = 0; iter < 10; ++iter) {
        array<double, NUM_GOODS> tempOut{}, tempIn{};
        tempOut[goodIndex["谷物"]] += subGrain;
        tempOut[goodIndex["织物"]] += subFabric;
        tempOut[goodIndex["服装"]] += subClothes;

        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            const auto& bt = buildingTemplates[t];
            double actualRate = laborOutput[t] * supplyRatio[t];
            tempOut[bt.outputGood] += buildingCounts[t] * actualRate;
            for (int g = 0; g < NUM_GOODS; ++g)
                tempIn[g] += buildingCounts[t] * bt.inputs[g] * actualRate;
        }

        array<double, NUM_GOODS> ratio;
        for (int g = 0; g < NUM_GOODS; ++g) {
            if (tempIn[g] > 0 && tempOut[g] < tempIn[g]) ratio[g] = tempOut[g] / tempIn[g];
            else ratio[g] = 1.0;
        }

        array<double, TYPE_COUNT> newRatio;
        newRatio.fill(1.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            const auto& bt = buildingTemplates[t];
            double minR = 1.0;
            for (int g = 0; g < NUM_GOODS; ++g)
                if (bt.inputs[g] > 0) minR = min(minR, ratio[g]);
            newRatio[t] = minR;
        }

        double maxDiff = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t)
            maxDiff = max(maxDiff, fabs(newRatio[t] - supplyRatio[t]));
        supplyRatio = newRatio;
        if (maxDiff < 1e-4) break;
    }

    // ★ 限制最低配给率 0.2，并保存到成员变量
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) supplyRatio[t] = 1.0;
        else supplyRatio[t] = max(supplyRatio[t], 0.2);
    }
    currentSupplyRatio = supplyRatio;

    // ---------- 6. 真实产出 ----------
    array<double, NUM_GOODS> realOut{}, realIn{};
    realOut[goodIndex["谷物"]] += subGrain;
    realOut[goodIndex["织物"]] += subFabric;
    realOut[goodIndex["服装"]] += subClothes;

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double actualRate = laborOutput[t] * supplyRatio[t];
        realOut[bt.outputGood] += buildingCounts[t] * actualRate;
        for (int g = 0; g < NUM_GOODS; ++g)
            realIn[g] += buildingCounts[t] * bt.inputs[g] * actualRate;
    }

    // ---------- 7. 现金池 ----------
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == CONST_DEPT || buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double actualRate = laborOutput[t] * supplyRatio[t];
        double revenue = buildingCounts[t] * actualRate * prices[bt.outputGood];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += buildingCounts[t] * bt.inputs[g] * actualRate * prices[g];
        double laborCost = buildingCounts[t] * bt.laborPerUnit * actualRate * averageWage;
        cashPools[t] += revenue - inputCost - laborCost;
    }

    // ---------- 8. 消费分配 ----------
    auto per100k = getGroupDemandPer100k();
    double popScale = population / 100000.0;
    array<double, GROUP_COUNT> groupDemand;
    for (int g = 0; g < GROUP_COUNT; ++g) groupDemand[g] = per100k[g] * popScale;

    array<double, NUM_GOODS> avail;
    for (int i = 0; i < NUM_GOODS; ++i) avail[i] = max(0.0, realOut[i] - realIn[i]);

    array<double, NUM_GOODS> consumerTarget{}, consumerActual{};
    for (int g = 0; g < GROUP_COUNT; ++g) {
        double remain = groupDemand[g];
        if (remain <= 1e-9) continue;
        vector<int> goods = groupGoods[g];
        sort(goods.begin(), goods.end(), [&](int a, int b) {
            return valueCoeff[a][g] * prices[a] > valueCoeff[b][g] * prices[b];
        });
        for (int good : goods) {
            double u = valueCoeff[good][g];
            if (u <= 0.0) continue;
            double want = remain / u;
            consumerTarget[good] += want;
            double bought = min(avail[good], want);
            consumerActual[good] += bought;
            avail[good] -= bought;
            remain -= bought * u;
            if (remain <= 1e-9) break;
        }
    }
    latestConsumerTarget = consumerTarget;
    latestConsumerActual = consumerActual;

    double essentialsDemand = groupDemand[GRP_SIMPLE_CLOTHES] + groupDemand[GRP_BASIC_FOOD];
    double essentialsActual = 0.0;
    for (int good : groupGoods[GRP_SIMPLE_CLOTHES])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
    for (int good : groupGoods[GRP_BASIC_FOOD])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_BASIC_FOOD];
    satisfaction = (essentialsDemand > 1e-9) ? (essentialsActual / essentialsDemand) : 1.0;
    satisfaction = clamp(satisfaction, 0.0, 1.0);

    // ---------- 9. 建造力分配 ----------
    double pConstr = prices[constrIdx];
    double constrDem = 0.0;
    for (const auto& ord : constructionQueue) constrDem += ord.remainingCost;

    double availConstr = realOut[constrIdx];
    double soldConstr = 0.0;

    for (auto& ord : constructionQueue) {
        if (availConstr <= 0) break;
        double invest = min({ availConstr, 30.0, ord.remainingCost });
        if (invest <= 0) continue;
        cashPools[ord.typeIndex] -= invest * pConstr;
        soldConstr += invest;
        ord.remainingCost -= invest;
        availConstr -= invest;
    }

    // 建造部门利润
    {
        const auto& bt = buildingTemplates[CONST_DEPT];
        double actualRate = laborOutput[CONST_DEPT] * supplyRatio[CONST_DEPT];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += buildingCounts[CONST_DEPT] * bt.inputs[g] * actualRate * prices[g];
        double laborCost = buildingCounts[CONST_DEPT] * bt.laborPerUnit * actualRate * averageWage;
        cashPools[CONST_DEPT] += soldConstr * pConstr - inputCost - laborCost;
    }

    // 完成订单
    constructionQueue.erase(remove_if(constructionQueue.begin(), constructionQueue.end(),
        [&](ConstructionOrder& o) {
            if (o.remainingCost <= 0) { buildingCounts[o.typeIndex]++; return true; }
            return false;
        }), constructionQueue.end());

    // ---------- 10. GDP ----------
    double totalMarketValue = 0.0;
    for (int i = 0; i < NUM_GOODS; ++i) {
        double outVal = (i == constrIdx) ? soldConstr : realOut[i];
        totalMarketValue += outVal * prices[i];
    }
    double gdp = totalMarketValue + accumulate(cashPools.begin(), cashPools.end(), 0.0);

    // ---------- 11. 价格更新 ----------
    const double oversupplyGain = 1.5;
    for (int i = 0; i < NUM_GOODS; ++i) {
        double rawExcess = potentialIn[i] + consumerTarget[i] - realOut[i];
        if (i == constrIdx) rawExcess += constrDem;

        double excessDemand = (rawExcess >= 0) ? rawExcess : rawExcess * oversupplyGain;

        double G = fabs(realOut[i]) + fabs(potentialIn[i]) + fabs(consumerTarget[i]) + 1.0;
        m[i] = inertiaCoeff * G;
        double restoring = b[i] * (prices[i] - referencePrice[i]);
        double rho = dampRatio * 2.0 * sqrt(m[i] * b[i]);
        double acc = (excessDemand - rho * v[i] - restoring) / m[i];
        v[i] += acc * dt;
        prices[i] += v[i] * dt;
        if (prices[i] < 0.1) prices[i] = 0.1;
    }

    // ---------- 12. 利润率 ----------
    array<double, TYPE_COUNT> profitRates{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double unitCost = bt.laborPerUnit * averageWage;
        for (int g = 0; g < NUM_GOODS; ++g) unitCost += prices[g] * bt.inputs[g];
        profitRates[t] = (unitCost > 1e-6) ? (prices[bt.outputGood] - unitCost) / unitCost : 0.0;
    }
    avgProfitRates = profitRates;
    profitRateHist.push_back(profitRates);

    if (stepCount == 1) smoothedProfitRate = profitRates;
    else {
        double alpha = 0.2;
        for (int t = 0; t < TYPE_COUNT; ++t)
            smoothedProfitRate[t] = smoothedProfitRate[t] * (1.0 - alpha) + profitRates[t] * alpha;
    }

    // ---------- 13. 雇佣调整（改为每年一次，系数重新校准）----------
    if (stepCount % 52 == 0) {   // ★ 只在每年第一周执行
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            if (avgProfitRates[t] < 0.0) {
                // 每年裁员20%（原每周5%累积效应过强，调整为温和的年度收缩）
                employmentRatio[t] *= 0.80;
            } else {
                // 每年恢复缺口的30%
                employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.30;
            }
            employmentRatio[t] = clamp(employmentRatio[t], 0.0, 1.0);
        }
    }

    // ---------- 14. 建筑衰退 ----------
    if (stepCount > 52) {
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (t == CONST_DEPT) {
                consecutiveLowEmpWeeks[t] = 0;
                continue;
            }
            if (buildingCounts[t] == 0) {
                consecutiveLowEmpWeeks[t] = 0;
                continue;
            }
            if (employmentRatio[t] <= 0.75) {
                consecutiveLowEmpWeeks[t]++;
                if (consecutiveLowEmpWeeks[t] >= 156) {
                    int reduce = max(1, (int)ceil(buildingCounts[t] * 0.05));
                    buildingCounts[t] = max(1, buildingCounts[t] - reduce);
                    if (buildingCounts[t] < 1) buildingCounts[t] = 1;
                }
            } else {
                consecutiveLowEmpWeeks[t] = 0;
            }
        }
    } else {
        fill(consecutiveLowEmpWeeks.begin(), consecutiveLowEmpWeeks.end(), 0);
    }

    // ---------- 15. 历史记录 ----------
    gdpHist.push_back(gdp);
    buildingHist.push_back(buildingCounts);
    outputHist.push_back(realOut);
    priceHist.push_back(prices);
    cashPoolHist.push_back(cashPools);
    populationHist.push_back(population);
}

void MarketSimulation::aiBuild() {
    array<int, TYPE_COUNT> inQueueCount{};
    for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;

    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                     + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    bool farmCap = (totalFarms >= maxTotalFarms);
    bool coalCap = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
    bool ironCap = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
    bool constrCap = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);

    double constrCapacity = 0.0;
    if (buildingCounts[CONST_DEPT] > 0)
        constrCapacity = buildingCounts[CONST_DEPT] *
            buildingTemplates[CONST_DEPT].getProfitFactor(prices, averageWage) *
            employmentRatio[CONST_DEPT] * buildingTemplates[CONST_DEPT].outputRate;

    double totalRemaining = 0.0;
    for (const auto& ord : constructionQueue) totalRemaining += ord.remainingCost;
    if (totalRemaining / (constrCapacity + 0.001) > 52.0 && inQueueCount[CONST_DEPT] == 0 && !constrCap) {
        placeOrder(CONST_DEPT);
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (inQueueCount[t] >= 50) continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0) continue;
        if (t == COAL_MINE && coalCap) continue;
        if (t == IRON_MINE && ironCap) continue;
        if (t == CONST_DEPT && constrCap) continue;

        double smoothed = smoothedProfitRate[t];

        if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
            const auto& bt = buildingTemplates[t];
            double estCost = bt.getUnitCost(prices, averageWage);
            double estProfit = (estCost > 1e-6) ? (prices[bt.outputGood] - estCost) / estCost : 0.0;
            if (estProfit > aiProfitThreshold) {
                if (t == FARM_GRAIN || t == COTTON) {
                    if (farmSlotsRemaining > 0) { placeOrder(t); farmSlotsRemaining--; }
                } else {
                    placeOrder(t);
                }
            }
            continue;
        }

        if (smoothed > aiProfitThreshold && buildingCounts[t] > 0) {
            int N_wanted = (int)ceil((smoothed - aiProfitThreshold) / 0.05);
            N_wanted = max(0, N_wanted);
            int N_remaining = N_wanted - inQueueCount[t];
            if (N_remaining <= 0) continue;

            if (t == FARM_GRAIN || t == COTTON) N_remaining = min(N_remaining, farmSlotsRemaining);
            else if (t == COAL_MINE) N_remaining = min(N_remaining, maxCoalMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == IRON_MINE) N_remaining = min(N_remaining, maxIronMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == CONST_DEPT) N_remaining = min(N_remaining, maxConstDept - (buildingCounts[t] + inQueueCount[t]));

            int maxQueue = 50 - inQueueCount[t];
            if (N_remaining > maxQueue) N_remaining = maxQueue;

            int maxExpand = max(1, (int)ceil(buildingCounts[t] * 0.1));
            int N_final = min(N_remaining, maxExpand);

            for (int j = 0; j < N_final; ++j) placeOrder(t);
            if (t == FARM_GRAIN || t == COTTON) farmSlotsRemaining -= N_final;
        }
    }
}