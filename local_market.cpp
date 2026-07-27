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
    latestConsumerTarget.fill(0.0);
    latestConsumerActual.fill(0.0);
    latestPotentialIn.fill(0.0);
    subsistencePop = 0.0;
    totalLaborers = 0.0;
    totalEngineers = 0.0;
    totalCapitalists = 0.0;
}

void LocalMarket::step() {
    stepCount++;
    const int constrIdx = goodIndex["建造力"];

    // 1. 人口增长
    double s = satisfaction;
    if (s < 0.0) s = 0.0; else if (s > 1.0) s = 1.0;
    double r52;
    if (s <= 0.75) r52 = (0.20 / 0.75) * s - 0.20;
    else           r52 = 0.2 * (s - 0.75);
    if (r52 < -0.20) r52 = -0.20;
    if (r52 > 0.05)  r52 = 0.05;
    double r_step = pow(1.0 + r52, 1.0 / 52.0) - 1.0;
    population *= (1.0 + r_step);

    // 2. 自给农场数量
    auto inQueueCount = bld.getInQueueCounts();
    int usedFarms = bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int idleLand = max(0, 10000 - usedFarms);  // maxTotalFarms = 10000（与BuildingManager内部一致）
    subsistenceFarms = idleLand;

    // 3. 最大劳动力
    maxLabor = population;

    // 4. 建筑基础产出率
    auto estOutputPerBuilding = bld.calculateBaseOutputRates(maxLabor);

    // 5. 原料配给迭代
    array<double, TYPE_COUNT> supplyRatio;
    supplyRatio.fill(1.0);
    double subGrain = 0.0, subFabric = 0.0, subClothes = 0.0;
    for (int iter = 0; iter < 10; ++iter) {
        array<double, NUM_GOODS> tempOut{}, tempIn{};
        tempOut[goodIndex["谷物"]] += subGrain;
        tempOut[goodIndex["织物"]] += subFabric;
        tempOut[goodIndex["服装"]] += subClothes;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            double actualRate = estOutputPerBuilding[t] * supplyRatio[t];
            tempOut[bt.outputGood] += bld.getBuildingCounts()[t] * actualRate;
            for (int g = 0; g < NUM_GOODS; ++g)
                tempIn[g] += bld.getBuildingCounts()[t] * bt.inputs[g] * actualRate;
        }
        array<double, NUM_GOODS> ratio;
        for (int g = 0; g < NUM_GOODS; ++g) {
            if (tempIn[g] > 0 && tempOut[g] < tempIn[g]) ratio[g] = tempOut[g] / tempIn[g];
            else ratio[g] = 1.0;
        }
        array<double, TYPE_COUNT> newRatio;
        newRatio.fill(1.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
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
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) supplyRatio[t] = 1.0;
        else supplyRatio[t] = max(supplyRatio[t], 0.2);
    }
    bld.setCurrentSupplyRatio(supplyRatio);

    // 6. 计算真实工业产出
    array<double, NUM_GOODS> realOut{}, realIn{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double actualRate = estOutputPerBuilding[t] * supplyRatio[t];
        realOut[bt.outputGood] += bld.getBuildingCounts()[t] * actualRate;
        for (int g = 0; g < NUM_GOODS; ++g)
            realIn[g] += bld.getBuildingCounts()[t] * bt.inputs[g] * actualRate;
    }

    // 7. 工业雇佣人数
    double employedTotal = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double actualRate = estOutputPerBuilding[t] * supplyRatio[t];
        employedTotal += bld.getBuildingCounts()[t] * bt.laborPerUnit * actualRate;
    }
    totalLaborers    = employedTotal * 0.75;
    totalEngineers   = employedTotal * 0.20;
    totalCapitalists = employedTotal * 0.05;

    // 8. 自给农
    double remainingPop = population - employedTotal;
    if (remainingPop < 0.0) remainingPop = 0.0;
    subsistencePop = min(remainingPop, subsistenceFarms * 5000.0);
    double subEff = (subsistenceFarms > 0) ? subsistencePop / (subsistenceFarms * 5000.0) : 0.0;
    subGrain  = subsistenceFarms * 2.0 * subEff;
    subFabric = subsistenceFarms * 1.0 * subEff;
    subClothes = subsistenceFarms * 0.5 * subEff;
    realOut[goodIndex["谷物"]] += subGrain;
    realOut[goodIndex["织物"]] += subFabric;
    realOut[goodIndex["服装"]] += subClothes;

    // 9. 潜在中间需求
    array<double, NUM_GOODS> potentialIn{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double desiredRate = estOutputPerBuilding[t];
        for (int g = 0; g < NUM_GOODS; ++g)
            potentialIn[g] += bld.getBuildingCounts()[t] * bt.inputs[g] * desiredRate;
    }
    latestPotentialIn = potentialIn;

    // 10. 普通建筑现金池更新（CONST_DEPT稍后）
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == CONST_DEPT || bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double actualRate = estOutputPerBuilding[t] * supplyRatio[t];
        double revenue = bld.getBuildingCounts()[t] * actualRate * prices[bt.outputGood];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += bld.getBuildingCounts()[t] * bt.inputs[g] * actualRate * prices[g];
        double laborCost = bld.getBuildingCounts()[t] * bt.laborPerUnit * actualRate * averageWage;
        bld.addCash(t, revenue - inputCost - laborCost);
    }

    // 11. 消费需求
    const double popScale = 1.0 / 100000.0;
    array<double, GROUP_COUNT> groupDemand{};
    for (int g = 0; g < GROUP_COUNT; ++g)
        groupDemand[g] += demandTable[0][g] * subsistencePop * popScale;
    for (int g = 0; g < GROUP_COUNT; ++g)
        groupDemand[g] += demandTable[0][g] * totalLaborers * popScale;
    for (int g = 0; g < GROUP_COUNT; ++g)
        groupDemand[g] += demandTable[1][g] * totalEngineers * popScale;
    for (int g = 0; g < GROUP_COUNT; ++g)
        groupDemand[g] += demandTable[2][g] * totalCapitalists * popScale;

    array<double, NUM_GOODS> avail;
    for (int i = 0; i < NUM_GOODS; ++i)
        avail[i] = max(0.0, realOut[i] - realIn[i]);

    array<double, NUM_GOODS> consumerTarget{}, consumerActual{};
    for (int g = 0; g < GROUP_COUNT; ++g) {
        double remain = groupDemand[g];
        if (remain <= 1e-9) continue;
        vector<int> goods = groupGoods[g];
        sort(goods.begin(), goods.end(), [&](int a, int b) {
            double ya = (prices[a] > 1e-9) ? (valueCoeff[a][g] / prices[a]) : 0.0;
            double yb = (prices[b] > 1e-9) ? (valueCoeff[b][g] / prices[b]) : 0.0;
            return ya > yb;
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

    // 必需品满足度
    double essentialsDemand   = groupDemand[GRP_SIMPLE_CLOTHES] + groupDemand[GRP_BASIC_FOOD];
    double essentialsActual = 0.0;
    for (int good : groupGoods[GRP_SIMPLE_CLOTHES])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
    for (int good : groupGoods[GRP_BASIC_FOOD])
        essentialsActual += consumerActual[good] * valueCoeff[good][GRP_BASIC_FOOD];
    satisfaction = (essentialsDemand > 1e-9) ? (essentialsActual / essentialsDemand) : 1.0;
    satisfaction = clamp(satisfaction, 0.0, 1.0);

    // 12. 建造力分配
    double pConstr = prices[constrIdx];
    double availConstr = realOut[constrIdx];
    double soldConstr = bld.processConstruction(availConstr, pConstr);

    // 13. 建造部门现金池更新
    if (bld.getBuildingCounts()[CONST_DEPT] > 0) {
        const auto& bt = bld.getTemplates()[CONST_DEPT];
        double actualRate = estOutputPerBuilding[CONST_DEPT] * supplyRatio[CONST_DEPT];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += bld.getBuildingCounts()[CONST_DEPT] * bt.inputs[g] * actualRate * prices[g];
        double laborCost = bld.getBuildingCounts()[CONST_DEPT] * bt.laborPerUnit * actualRate * averageWage;
        bld.addCash(CONST_DEPT, soldConstr * pConstr - inputCost - laborCost);
    }

    // 14. GDP
    double gdp = subGrain  * prices[goodIndex["谷物"]]
               + subFabric * prices[goodIndex["织物"]]
               + subClothes * prices[goodIndex["服装"]];
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double actualRate = estOutputPerBuilding[t] * supplyRatio[t];
        double outputVal = (t == CONST_DEPT) ? soldConstr * prices[bt.outputGood]
                                             : bld.getBuildingCounts()[t] * actualRate * prices[bt.outputGood];
        double inputCost = 0.0;
        for (int g = 0; g < NUM_GOODS; ++g)
            inputCost += bld.getBuildingCounts()[t] * bt.inputs[g] * actualRate * prices[g];
        gdp += outputVal - inputCost;
    }

    // 15. 价格更新
    const double oversupplyGain = 1.5;
    const double dt_local = 0.2;
    for (int i = 0; i < NUM_GOODS; ++i) {
        double rawExcess = realIn[i] + consumerTarget[i] - realOut[i];
        if (i == constrIdx) {
            double constrDem = 0.0;
            for (const auto& ord : bld.getQueue())
                constrDem += ord.remainingCost;
            rawExcess += constrDem;
        }
        double excessDemand = (rawExcess >= 0) ? rawExcess : rawExcess * oversupplyGain;

        double G = fabs(realOut[i]) + fabs(realIn[i]) + fabs(consumerTarget[i]) + 1.0;
        m[i] = inertiaCoeff * G;

        double restoring = b[i] * (prices[i] - referencePrice[i]);
        double rho = dampRatio * 2.0 * sqrt(m[i] * b[i]);

        double acc = (excessDemand - rho * v[i] - restoring) / m[i];

        v[i] += acc * dt_local;
        prices[i] += v[i] * dt_local;

        if (prices[i] < 0.1) prices[i] = 0.1;
    }

    // 16. 利润率、雇佣、建筑衰退
    bld.updateProfitRates(prices, averageWage);
    if (stepCount % 52 == 0)
        bld.adjustEmployment();
    bld.checkDecay(stepCount);

    // 17. 历史记录
    gdpHist.push_back(gdp);
    buildingHist.push_back(bld.getBuildingCounts());
    outputHist.push_back(realOut);
    priceHist.push_back(prices);
    cashPoolHist.push_back(bld.getCashPools());
    populationHist.push_back(population);
}

void LocalMarket::aiBuild() {
    bld.aiBuild(aiProfitThreshold, prices, averageWage);
}

void LocalMarket::playerBuild(int typeIdx, int count) {
    bld.placePlayerOrder(typeIdx, count, true); // 置顶，忽略现金
}

void LocalMarket::playerDemolish(int typeIdx, int count) {
    bld.demolishBuildings(typeIdx, count, stepCount);
}