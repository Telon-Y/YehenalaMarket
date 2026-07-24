#include "market_simulation.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>

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
        cashPools[t] = buildingCounts[t] * 5000.0;

    maxLabor = population * laborPerCapita;
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
    constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx]});
}

void MarketSimulation::step() {
    stepCount++;

    double s = satisfaction;
    if (s < 0.0) s = 0.0; else if (s > 1.0) s = 1.0;
    double r52;
    if (s <= 0.75)
        r52 = (0.20 / 0.75) * s - 0.20;
    else
        r52 = 0.2 * (s - 0.75);
    if (r52 < -0.20) r52 = -0.20;
    if (r52 > 0.05)  r52 = 0.05;
    double r_step = pow(1.0 + r52, 1.0 / 52.0) - 1.0;
    population *= (1.0 + r_step);
    maxLabor = population * laborPerCapita;

    const int constrIdx = goodIndex["建造力"];

    // ---------- 1. 利润因子 ----------
    array<double, TYPE_COUNT> profitFactor;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) { profitFactor[t] = 0.0; continue; }
        profitFactor[t] = buildingTemplates[t].getProfitFactor(prices, averageWage);
    }

    // ---------- 2. 劳动力约束前期望产出 ----------
    array<double, TYPE_COUNT> estOutputPerBuilding;
    double totalDesiredLabor = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) { estOutputPerBuilding[t] = 0.0; continue; }
        const auto& bt = buildingTemplates[t];
        estOutputPerBuilding[t] = bt.outputRate * profitFactor[t] * employmentRatio[t];
        totalDesiredLabor += buildingCounts[t] * bt.laborPerUnit * estOutputPerBuilding[t];
    }
    double laborFactor = totalDesiredLabor > 0 ? min(1.0, maxLabor / totalDesiredLabor) : 1.0;

    array<double, TYPE_COUNT> laborOutput;
    for (int t = 0; t < TYPE_COUNT; ++t)
        laborOutput[t] = estOutputPerBuilding[t] * laborFactor;

    // ---------- 3. 自给农场 ----------
    array<int, TYPE_COUNT> inQueueCount{};
    for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;
    int usedFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int subsistCount = max(0, maxTotalFarms - usedFarms);
    double subGrain = subsistCount * 2.0;
    double subFabric = subsistCount * 1.0;
    double subClothes = subsistCount * 0.5;

    // ---------- 4. 潜在中间需求 ----------
    array<double, NUM_GOODS> potentialIn{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double potentialRate = laborOutput[t];
        for (int g = 0; g < NUM_GOODS; ++g)
            potentialIn[g] += buildingCounts[t] * bt.inputs[g] * potentialRate;
    }

    // ---------- 5. 原料可得性迭代配给 ----------
    array<double, TYPE_COUNT> supplyRatio;
    supplyRatio.fill(1.0);

    for (int iter = 0; iter < 10; ++iter) {
        array<double, NUM_GOODS> tempOut{}, tempIn{};
        int grainIdx = goodIndex["谷物"], fabIdx = goodIndex["织物"], clothIdx = goodIndex["服装"];
        tempOut[grainIdx] += subGrain;
        tempOut[fabIdx]   += subFabric;
        tempOut[clothIdx] += subClothes;

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
            if (tempIn[g] > 0 && tempOut[g] < tempIn[g])
                ratio[g] = tempOut[g] / tempIn[g];
            else
                ratio[g] = 1.0;
        }

        array<double, TYPE_COUNT> newSupplyRatio;
        newSupplyRatio.fill(1.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            const auto& bt = buildingTemplates[t];
            double minR = 1.0;
            for (int g = 0; g < NUM_GOODS; ++g) {
                if (bt.inputs[g] > 0)
                    minR = min(minR, ratio[g]);
            }
            newSupplyRatio[t] = minR;
        }

        double maxDiff = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t)
            maxDiff = max(maxDiff, fabs(newSupplyRatio[t] - supplyRatio[t]));

        supplyRatio = newSupplyRatio;
        if (maxDiff < 1e-4) break;
    }

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

    // ---------- 7. 现金池（非建造部门） ----------
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == CONST_DEPT || buildingCounts[t] == 0) continue;
        const auto& bt = buildingTemplates[t];
        double actualRate = laborOutput[t] * supplyRatio[t];
        double totalProd = buildingCounts[t] * actualRate;
        double revenue = totalProd * prices[bt.outputGood];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += buildingCounts[t] * bt.inputs[g] * actualRate * prices[g];
        double laborCost = buildingCounts[t] * bt.laborPerUnit * actualRate * averageWage;
        double profit = revenue - inputCost - laborCost;
        cashPools[t] += profit;
        if (cashPools[t] < 0) cashPools[t] = 0;
    }

    // ---------- 8. 消费分配（已按设计修正为 price * valueCoeff 排序）----------
    auto per100k = getGroupDemandPer100k();
    double popScale = population / 100000.0;
    array<double, GROUP_COUNT> groupDemand;
    for (int g = 0; g < GROUP_COUNT; ++g)
        groupDemand[g] = per100k[g] * popScale;

    array<double, NUM_GOODS> avail;
    for (int i = 0; i < NUM_GOODS; ++i)
        avail[i] = max(0.0, realOut[i] - realIn[i]);

    array<double, NUM_GOODS> consumerTarget{}, consumerActual{};

    for (int g = 0; g < GROUP_COUNT; ++g) {
        double remain = groupDemand[g];
        if (remain <= 1e-9) continue;

        vector<int> goods = groupGoods[g];
        // 按 price * valueCoeff 降序（即消费权重越高越优先）
        sort(goods.begin(), goods.end(), [&](int a, int b) {
            double wa = valueCoeff[a][g] * prices[a];
            double wb = valueCoeff[b][g] * prices[b];
            return wa > wb;
        });

        for (int good : goods) {
            double u = valueCoeff[good][g];
            if (u <= 0.0) continue;

            double wantQty = remain / u;
            consumerTarget[good] += wantQty;

            double maxBuy = min(avail[good], wantQty);
            double bought = maxBuy;
            consumerActual[good] += bought;
            avail[good] -= bought;
            remain -= bought * u;
            if (remain <= 1e-9) break;
        }
    }

    // 必需品满足度
    double essentialsDemand = groupDemand[GRP_SIMPLE_CLOTHES] + groupDemand[GRP_BASIC_FOOD];
    double essentialsActual = 0.0;
    for (int good : groupGoods[GRP_SIMPLE_CLOTHES])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
    for (int good : groupGoods[GRP_BASIC_FOOD])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_BASIC_FOOD];

    satisfaction = (essentialsDemand > 1e-9) ? (essentialsActual / essentialsDemand) : 1.0;
    satisfaction = clamp(satisfaction, 0.0, 1.0);

    // ---------- 9. 建造力分配 ----------
    double constrDem = 0.0;
    for (const auto& ord : constructionQueue) constrDem += ord.remainingCost;

    double availConstr = realOut[constrIdx];
    double pConstr = prices[constrIdx];
    double soldConstr = 0.0;

    for (auto& ord : constructionQueue) {
        if (availConstr <= 0) break;
        double maxByCash = cashPools[ord.typeIndex] / pConstr;
        double invest = min({ availConstr, 30.0, ord.remainingCost, maxByCash });
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
        double revenue = soldConstr * pConstr;
        double profit = revenue - inputCost - laborCost;
        cashPools[CONST_DEPT] += profit;
        if (cashPools[CONST_DEPT] < 0) cashPools[CONST_DEPT] = 0;
    }

    // 移除已完成订单
    constructionQueue.erase(
        remove_if(constructionQueue.begin(), constructionQueue.end(),
            [&](ConstructionOrder& o) {
                if (o.remainingCost <= 0) {
                    buildingCounts[o.typeIndex]++;
                    return true;
                }
                return false;
            }),
        constructionQueue.end());

    // ---------- 10. GDP ----------
    double totalMarketValue = 0.0;
    for (int i = 0; i < NUM_GOODS; ++i) {
        double outVal = (i == constrIdx) ? soldConstr : realOut[i];
        totalMarketValue += outVal * prices[i];
    }
    double totalCashPool = accumulate(cashPools.begin(), cashPools.end(), 0.0);
    double gdp = totalMarketValue + totalCashPool;

    // ---------- 11. 价格更新 ----------
    for (int i = 0; i < NUM_GOODS; ++i) {
        double excessDemand = potentialIn[i] + consumerTarget[i] - realOut[i];
        if (i == constrIdx) excessDemand += constrDem;

        double G = fabs(realOut[i]) + fabs(potentialIn[i]) + fabs(consumerTarget[i]) + 1.0;
        m[i] = inertiaCoeff * G;
        double restoring = b[i] * (prices[i] - referencePrice[i]);
        double rho = dampRatio * 2.0 * sqrt(m[i] * b[i]);
        double acc = (excessDemand - rho * v[i] - restoring) / m[i];
        v[i] += acc * dt;
        prices[i] += v[i] * dt;
        if (prices[i] < 0.1) prices[i] = 0.1;
    }

    // ---------- 12. 利润率计算 ----------
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

    if (stepCount == 1) {
        smoothedProfitRate = profitRates;
    } else {
        double alpha = 0.2;
        for (int t = 0; t < TYPE_COUNT; ++t)
            smoothedProfitRate[t] = smoothedProfitRate[t] * (1.0 - alpha) + profitRates[t] * alpha;
    }

    // ---------- 13. 雇佣调整 ----------
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        if (avgProfitRates[t] < 0.0)
            employmentRatio[t] *= 0.95;
        else
            employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.05;
        employmentRatio[t] = clamp(employmentRatio[t], 0.1, 1.0);
    }

    // ---------- 14. 建筑衰退 ----------
    if (stepCount > 52) {
        for (int t = 0; t < TYPE_COUNT; ++t) {
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
        for (int t = 0; t < TYPE_COUNT; ++t) consecutiveLowEmpWeeks[t] = 0;
    }

    // ---------- 15. 记录历史 ----------
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
    bool farmCapReached = (totalFarms >= maxTotalFarms);
    bool coalCapReached = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
    bool ironCapReached = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
    bool constrCapReached = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);

    // 评估建造速度，必要时插入建造部门订单
    double constrCapacity = 0.0;
    if (buildingCounts[CONST_DEPT] > 0) {
        constrCapacity = buildingCounts[CONST_DEPT] *
            buildingTemplates[CONST_DEPT].getProfitFactor(prices, averageWage) *
            employmentRatio[CONST_DEPT] * buildingTemplates[CONST_DEPT].outputRate;
    }
    double totalRemainingCost = 0.0;
    for (const auto& ord : constructionQueue) totalRemainingCost += ord.remainingCost;
    double stepsNeeded = (constrCapacity > 0) ? totalRemainingCost / constrCapacity : 1e9;
    if (stepsNeeded > 52.0 && inQueueCount[CONST_DEPT] == 0 && !constrCapReached) {
        constructionQueue.insert(constructionQueue.begin(),
                                 {CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT]});
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        // 上限检查
        if (t == FARM_GRAIN || t == COTTON) {
            if (farmSlotsRemaining <= 0) continue;
        } else if (t == COAL_MINE && coalCapReached) continue;
        else if (t == IRON_MINE && ironCapReached) continue;
        else if (t == CONST_DEPT && constrCapReached) continue;

        double smoothed = smoothedProfitRate[t];

        // 尚无建筑：若预期利润率 > aiProfitThreshold 则首次建造
        if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
            const auto& bt = buildingTemplates[t];
            double estCost = bt.getUnitCost(prices, averageWage);
            double estProfitRate = (estCost > 1e-6) ? (prices[bt.outputGood] - estCost) / estCost : 0.0;
            if (estProfitRate > aiProfitThreshold) {
                if (t == FARM_GRAIN || t == COTTON) {
                    if (farmSlotsRemaining <= 0) continue;
                    placeOrder(t);
                    farmSlotsRemaining--;
                } else {
                    placeOrder(t);
                }
            }
            continue;
        }

        // 已有建筑：利润率驱动扩建
        if (smoothed > aiProfitThreshold && buildingCounts[t] > 0) {
            int N_wanted = (int)ceil((smoothed - aiProfitThreshold) / 0.05);
            if (N_wanted < 0) N_wanted = 0;
            int N_remaining = N_wanted - inQueueCount[t];
            if (N_remaining < 0) N_remaining = 0;

            // 应用各自上限
            if (t == FARM_GRAIN || t == COTTON) {
                N_remaining = min(N_remaining, farmSlotsRemaining);
            } else if (t == COAL_MINE) {
                int slots = maxCoalMines - (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE]);
                N_remaining = min(N_remaining, slots);
            } else if (t == IRON_MINE) {
                int slots = maxIronMines - (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE]);
                N_remaining = min(N_remaining, slots);
            } else if (t == CONST_DEPT) {
                int slots = maxConstDept - (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT]);
                N_remaining = min(N_remaining, slots);
            }

            // ★ 修复：向上取整，符合文档“计算向上取整”要求
            int maxExpand = max(1, (int)ceil(buildingCounts[t] * 0.1));
            int N_final = min(N_remaining, maxExpand);

            for (int j = 0; j < N_final; ++j)
                placeOrder(t);

            if (t == FARM_GRAIN || t == COTTON)
                farmSlotsRemaining -= N_final;
        }
    }
}

// 1.1 新增：手动拆除建筑
void MarketSimulation::removeBuilding(int typeIndex) {
    if (typeIndex >= 0 && typeIndex < TYPE_COUNT) {
        if (buildingCounts[typeIndex] > 0) {
            buildingCounts[typeIndex]--;
            // 现金池不回收，保持简单
        }
    }
}