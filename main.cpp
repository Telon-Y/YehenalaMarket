// ============================================================
// 叶赫那拉市场模拟 - v1.1 草案 (修复耕地上限)
// 编译: g++ -O3 -march=native -flto -o sim main.cpp
// ============================================================

#include <iostream>
#include <array>
#include <vector>
#include <string>
#include <unordered_map>
#include <numeric>
#include <algorithm>
#include <fstream>
#include <cmath>
#include <windows.h>

using namespace std;

constexpr int TOTAL_STEPS = 6000;
constexpr int AI_INTERVAL = 1;
constexpr int NUM_GOODS = 11;

const vector<string> commodityNames = {
    "谷物", "加工食品", "织物", "服装", "高档服装",
    "煤炭", "铁", "钢", "工具", "住房", "建造力"
};

enum BuildingType {
    FARM_GRAIN, FOOD_PROC, COTTON, CLOTHES, LUXURY_CLOTHES,
    COAL_MINE, IRON_MINE, STEEL_MILL, TOOL_FACT, HOUSING, CONST_DEPT,
    TYPE_COUNT
};

enum ConsGroup {
    GRP_SIMPLE_CLOTHES = 0,
    GRP_BASIC_FOOD,
    GRP_STANDARD_CLOTHES,
    GRP_HOUSING,
    GROUP_COUNT
};

struct BuildingTemplate {
    string name;
    int outputGood;
    double outputRate;
    array<double, NUM_GOODS> inputs;
    double laborPerUnit;

    double getUnitCost(const array<double, NUM_GOODS>& prices, double wageRate) const {
        double cost = laborPerUnit * wageRate;
        for (int g = 0; g < NUM_GOODS; ++g)
            cost += prices[g] * inputs[g];
        return cost;
    }

    double getProfitFactor(const array<double, NUM_GOODS>& prices, double wageRate) const {
        double unitCost = getUnitCost(prices, wageRate);
        double outputPrice = prices[outputGood];
        if (unitCost < 1e-6) return 1.0;
        double margin = outputPrice - unitCost;
        double ratio = margin / unitCost;
        double factor = max(0.2, 1.0 + ratio * 2.0);
        factor = min(factor, 1.0);
        return factor;
    }
};

struct ConstructionOrder {
    int typeIndex;
    double totalCost;
    double remainingCost;
};

class MarketSimulation {
public:
    unordered_map<string, int> goodIndex;
    array<double, NUM_GOODS> prices, v, m, b, referencePrice;
    double averageWage = 6.75;
    double dt = 0.5;
    double population = 10000000.0;
    double laborPerCapita = 1.0, maxLabor;
    double inertiaCoeff = 0.5, dampRatio = 0.3;

    vector<string> buildingTypeNames;
    vector<BuildingTemplate> buildingTemplates;
    vector<double> buildingCost;

    array<int, TYPE_COUNT> buildingCounts;
    vector<ConstructionOrder> constructionQueue;
    array<double, TYPE_COUNT> avgProfitRates;
    array<double, TYPE_COUNT> smoothedProfitRate;
    array<double, TYPE_COUNT> employmentRatio;
    array<double, TYPE_COUNT> cashPools;
    array<int, TYPE_COUNT> consecutiveLowEmpWeeks;
    int stepCount = 0;

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;

    double satisfaction = 0.75;

    // 基础食物统一为210（新文档）
    const array<array<double, GROUP_COUNT>, 3> demandTable = {{
        {39.0, 210.0, 0.0, 20.0},
        {41.0, 210.0, 7.0, 74.0},
        {0.0,  210.0, 122.0, 130.0}
    }};
    vector<vector<int>> groupGoods;

    // 使用价值系数表 [商品][消费组]
    array<array<double, GROUP_COUNT>, NUM_GOODS> valueCoeff;

    // 历史记录
    vector<array<double, NUM_GOODS>> priceHist, outputHist;
    vector<array<double, TYPE_COUNT>> profitRateHist;
    vector<array<int, TYPE_COUNT>> buildingHist;
    vector<double> gdpHist;
    vector<array<double, TYPE_COUNT>> cashPoolHist;
    vector<double> populationHist;

    MarketSimulation() {
        for (int i = 0; i < NUM_GOODS; ++i)
            goodIndex[commodityNames[i]] = i;

        prices.fill(1.0); v.fill(0.0); m.fill(1.0); b.fill(0.15);
        // 新价格体系（v1.1 草案）
        referencePrice = {
            2400.0,   // 谷物
            4000.0,   // 加工食品
            5000.0,   // 织物
            12000.0,  // 服装
            40000.0,  // 高档服装
            4000.0,   // 煤炭
            4000.0,   // 铁
            8000.0,   // 钢
            4000.0,   // 工具
            1600.0,   // 住房
            24000.0   // 建造力
        };
        // 微调部分价格抑制系数（保留原设计）
        b[goodIndex["加工食品"]] = 0.18;
        b[goodIndex["高档服装"]] = 0.12;
        b[goodIndex["建造力"]] = 0.25;
        prices = referencePrice;

        buildingTypeNames = {
            "谷物农场", "加工食品厂", "棉花种植园", "服装厂", "高档服装厂",
            "煤矿", "铁矿", "炼钢厂", "工具厂", "住房", "建造部门"
        };

        buildingCost = {200,600,200,600,600,600,600,800,800,800,100};

        buildingTemplates.resize(TYPE_COUNT);
        auto setTemplate = [&](BuildingType t, const string& name, int outIdx, double rate,
                               initializer_list<pair<int,double>> in) {
            BuildingTemplate& bt = buildingTemplates[t];
            bt.name = name; bt.outputGood = outIdx; bt.outputRate = rate;
            bt.inputs.fill(0.0);
            for (auto [gi, amt] : in) bt.inputs[gi] = amt;
            bt.laborPerUnit = 5000.0 / rate;
        };

        // 新投入产出表，商品名已统一为"煤炭"
        setTemplate(FARM_GRAIN, "谷物农场", goodIndex["谷物"], 50, {});
        setTemplate(FOOD_PROC, "加工食品厂", goodIndex["加工食品"], 45, {{goodIndex["谷物"], 40.0/45}});
        setTemplate(COTTON, "棉花种植园", goodIndex["织物"], 45, {});
        setTemplate(CLOTHES, "服装厂", goodIndex["服装"], 100, {{goodIndex["织物"], 60.0/100}});
        setTemplate(LUXURY_CLOTHES, "高档服装厂", goodIndex["高档服装"], 30, {{goodIndex["织物"], 25.0/30}});

        setTemplate(COAL_MINE, "煤矿", goodIndex["煤炭"], 60,
                    {{goodIndex["工具"], 15.0/60}, {goodIndex["煤炭"], 15.0/60}});
        setTemplate(IRON_MINE, "铁矿", goodIndex["铁"], 60,
                    {{goodIndex["工具"], 15.0/60}, {goodIndex["煤炭"], 15.0/60}});
        setTemplate(STEEL_MILL, "炼钢厂", goodIndex["钢"], 90,
                    {{goodIndex["铁"], 60.0/90}, {goodIndex["煤炭"], 30.0/90}});
        setTemplate(TOOL_FACT, "工具厂", goodIndex["工具"], 80,
                    {{goodIndex["钢"], 20.0/80}});
        setTemplate(HOUSING, "住房", goodIndex["住房"], 60,
                    {{goodIndex["钢"], 5.0/60}, {goodIndex["工具"], 5.0/60}});
        setTemplate(CONST_DEPT, "建造部门", goodIndex["建造力"], 15,
                    {{goodIndex["钢"], 25.0/15}, {goodIndex["铁"], 25.0/15}, {goodIndex["工具"], 20.0/15}});

        maxLabor = population * laborPerCapita;
        buildingCounts.fill(50);   // 初始建筑等级改为 50
        avgProfitRates.fill(0.0);
        smoothedProfitRate.fill(0.0);
        employmentRatio.fill(1.0);
        consecutiveLowEmpWeeks.fill(0);

        // 现金池初始值：每级建筑5000元（v1.1 草案）
        for (int t = 0; t < TYPE_COUNT; ++t)
            cashPools[t] = buildingCounts[t] * 5000.0;

        // 消费组商品映射
        groupGoods.resize(GROUP_COUNT);
        groupGoods[GRP_SIMPLE_CLOTHES]   = {goodIndex["服装"]};
        groupGoods[GRP_BASIC_FOOD]       = {goodIndex["谷物"], goodIndex["加工食品"]};
        groupGoods[GRP_STANDARD_CLOTHES] = {goodIndex["高档服装"]};
        groupGoods[GRP_HOUSING]          = {goodIndex["住房"]};

        // 使用价值系数初始化
        for (int i = 0; i < NUM_GOODS; ++i)
            for (int g = 0; g < GROUP_COUNT; ++g)
                valueCoeff[i][g] = 0.0;

        valueCoeff[goodIndex["谷物"]][GRP_BASIC_FOOD] = 1.0;
        valueCoeff[goodIndex["加工食品"]][GRP_BASIC_FOOD] = 1.5;
        valueCoeff[goodIndex["服装"]][GRP_SIMPLE_CLOTHES] = 1.0;
        valueCoeff[goodIndex["高档服装"]][GRP_STANDARD_CLOTHES] = 1.0;
        valueCoeff[goodIndex["住房"]][GRP_HOUSING] = 1.0;
    }

    array<double, GROUP_COUNT> getGroupDemandPer100k() const {
        double w = averageWage;
        array<double, GROUP_COUNT> res{};
        if (w <= 5.0) {
            for (int g=0; g<GROUP_COUNT; ++g) res[g] = demandTable[0][g];
        } else if (w >= 20.0) {
            for (int g=0; g<GROUP_COUNT; ++g) res[g] = demandTable[2][g];
        } else if (w <= 10.0) {
            double t = (w - 5.0) / 5.0;
            for (int g=0; g<GROUP_COUNT; ++g)
                res[g] = demandTable[0][g] * (1-t) + demandTable[1][g] * t;
        } else {
            double t = (w - 10.0) / 10.0;
            for (int g=0; g<GROUP_COUNT; ++g)
                res[g] = demandTable[1][g] * (1-t) + demandTable[2][g] * t;
        }
        return res;
    }

    void placeOrder(int typeIdx) {
        constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx]});
    }

    void step() {
        stepCount++;

        // 人口动态
        double s = (satisfaction < 0.0) ? 0.0 : (satisfaction > 1.0 ? 1.0 : satisfaction);
        double r52;
        if (s <= 0.75)
            r52 = (0.20 / 0.75) * s - 0.20;
        else
            r52 = 0.2 * (s - 0.75);
        if (r52 < -0.20) r52 = -0.20;
        if (r52 > 0.05)  r52 = 0.05;
        double r_step = pow(1.0 + r52, 1.0/52.0) - 1.0;
        population *= (1.0 + r_step);

        maxLabor = population * laborPerCapita;
        const int constrIdx = goodIndex["建造力"];

        // 1. 利润因子
        array<double, TYPE_COUNT> profitFactor;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) { profitFactor[t] = 0.0; continue; }
            profitFactor[t] = buildingTemplates[t].getProfitFactor(prices, averageWage);
        }

        // 2. 劳动力约束前的期望产出率
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

        // 3. 自给农场
        array<int, TYPE_COUNT> inQueueCount{};
        for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;
        int usedFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                        + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
        int subsistCount = max(0, maxTotalFarms - usedFarms);
        double subGrain = subsistCount * 2.0;
        double subFabric = subsistCount * 1.0;
        double subClothes = subsistCount * 0.5;

        // 4. 潜在中间需求（用于价格信号）
        array<double, NUM_GOODS> potentialIn;
        potentialIn.fill(0.0);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            const auto& bt = buildingTemplates[t];
            double potentialRate = laborOutput[t];
            for (int g = 0; g < NUM_GOODS; ++g)
                potentialIn[g] += buildingCounts[t] * bt.inputs[g] * potentialRate;
        }

        // 5. 原料可得性迭代配给（已实现短缺惩罚）
        array<double, TYPE_COUNT> supplyRatio;
        supplyRatio.fill(1.0);

        for (int iter = 0; iter < 10; ++iter) {
            array<double, NUM_GOODS> tempOut, tempIn;
            tempOut.fill(0.0); tempIn.fill(0.0);

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

        // 6. 真实产出
        array<double, NUM_GOODS> realOut, realIn;
        realOut.fill(0.0); realIn.fill(0.0);
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

        double actualLabor = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            actualLabor += buildingCounts[t] * buildingTemplates[t].laborPerUnit *
                           laborOutput[t] * supplyRatio[t];
        }

        // 7. 现金池（非建造部门）
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (t == CONST_DEPT) continue;
            if (buildingCounts[t] == 0) continue;
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

        // 8. 消费分配（消费权重排序，v1.1 新增）
        auto per100k = getGroupDemandPer100k();
        double popScale = population / 100000.0;
        array<double, GROUP_COUNT> groupDemand;
        for (int g = 0; g < GROUP_COUNT; ++g)
            groupDemand[g] = per100k[g] * popScale;

        array<double, NUM_GOODS> avail;
        for (int i = 0; i < NUM_GOODS; ++i)
            avail[i] = max(0.0, realOut[i] - realIn[i]);

        // 计算每个商品的消费权重基础（基于周期初净产出，整个分配过程不变）
        array<double, NUM_GOODS> consumerWeightBase;
        for (int i = 0; i < NUM_GOODS; ++i) {
            double net = max(0.0, realOut[i] - realIn[i]);
            consumerWeightBase[i] = (net / 2.0) * prices[i];
        }

        array<double, NUM_GOODS> consumerTarget{}, consumerActual{};

        for (int g = 0; g < GROUP_COUNT; ++g) {
            double remain = groupDemand[g];
            if (remain <= 1e-9) continue;

            vector<int> goods = groupGoods[g];
            // 按消费权重降序排列（权重高者优先购买）
            sort(goods.begin(), goods.end(), [&](int a, int b) {
                return consumerWeightBase[a] > consumerWeightBase[b];
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

        // 必需品满足度（使用价值口径）
        double essentialsDemand = groupDemand[GRP_SIMPLE_CLOTHES] + groupDemand[GRP_BASIC_FOOD];
        double essentialsActual = 0.0;
        for (int good : groupGoods[GRP_SIMPLE_CLOTHES])
            essentialsActual += consumerActual[good] * valueCoeff[good][GRP_SIMPLE_CLOTHES];
        for (int good : groupGoods[GRP_BASIC_FOOD])
            essentialsActual += consumerActual[good] * valueCoeff[good][GRP_BASIC_FOOD];

        satisfaction = (essentialsDemand > 1e-9) ? (essentialsActual / essentialsDemand) : 1.0;
        if (satisfaction > 1.0) satisfaction = 1.0;
        if (satisfaction < 0.0) satisfaction = 0.0;

        // 9. 建造力分配
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

        // 建造部门利润结算
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

        // 完成订单移除
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

        // 10. GDP（市场商品流通总价值 + 建筑现金池）
        double totalMarketValue = 0.0;
        for (int i = 0; i < NUM_GOODS; ++i) {
            double outVal = (i == constrIdx) ? soldConstr : realOut[i];
            totalMarketValue += outVal * prices[i];
        }
        double totalCashPool = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t)
            totalCashPool += cashPools[t];
        double gdp = totalMarketValue + totalCashPool;

        // 11. 价格更新
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

        // 12. 利润率
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

        // 13. 雇佣调整
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (buildingCounts[t] == 0) continue;
            if (avgProfitRates[t] < 0.0)
                employmentRatio[t] *= 0.95;
            else
                employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.05;
            employmentRatio[t] = max(0.1, min(1.0, employmentRatio[t]));
        }

        // 14. 建筑衰退（连续156周低雇佣）
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

        // 15. 记录历史
        gdpHist.push_back(gdp);
        buildingHist.push_back(buildingCounts);
        outputHist.push_back(realOut);
        priceHist.push_back(prices);
        cashPoolHist.push_back(cashPools);
        populationHist.push_back(population);
    }

    void aiBuild() {
        array<int,TYPE_COUNT> inQueueCount{};
        for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;

        int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                       + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
        // 耕地上限动态剩余配额 (修复并发下单超限问题)
        int farmSlotsRemaining = maxTotalFarms - totalFarms;
        bool farmCapReached = (totalFarms >= maxTotalFarms);

        bool coalCapReached = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
        bool ironCapReached = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
        bool constrCapReached = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);

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
            // 处理耕地上限（共享上限）
            if (t == FARM_GRAIN || t == COTTON) {
                if (farmSlotsRemaining <= 0) continue;
            } else if (t == COAL_MINE && coalCapReached) {
                continue;
            } else if (t == IRON_MINE && ironCapReached) {
                continue;
            } else if (t == CONST_DEPT && constrCapReached) {
                continue;
            }

            double smoothed = smoothedProfitRate[t];

            if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
                const auto& bt = buildingTemplates[t];
                double estCost = bt.getUnitCost(prices, averageWage);
                double estProfitRate = (estCost > 1e-6) ? (prices[bt.outputGood] - estCost) / estCost : 0.0;
                if (estProfitRate > 0.1) {
                    // 对于耕地，首次进入也要消耗配额
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

            if (smoothed > 0.1 && buildingCounts[t] > 0) {
                int N_wanted = (int)ceil((smoothed - 0.1) / 0.05);
                if (N_wanted < 0) N_wanted = 0;
                int N_remaining = N_wanted - inQueueCount[t];
                if (N_remaining < 0) N_remaining = 0;

                // 根据建筑类型应用上限
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

                int maxExpand = max(1, (int)floor(buildingCounts[t] * 0.1));
                int N_final = min(N_remaining, maxExpand);

                for (int j = 0; j < N_final; ++j)
                    placeOrder(t);

                // 扣减对应配额
                if (t == FARM_GRAIN || t == COTTON)
                    farmSlotsRemaining -= N_final;
            }
        }
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    MarketSimulation sim;

    cout << "模拟开始，共 " << TOTAL_STEPS << " 周...\n" << flush;

    for (int t = 0; t < TOTAL_STEPS; ++t) {
        sim.step();
        if (t % AI_INTERVAL == 0) sim.aiBuild();
        if (t % 52 == 0) cout << "周数: " << t << "/" << TOTAL_STEPS << "     \r" << flush;
    }
    cout << "\n写入CSV...\n" << flush;

    auto writeCsv = [](const string& fname, const string& header, auto& data) {
        ofstream f(fname);
        f << "\xEF\xBB\xBF" << header << "\n";
        for (size_t row = 0; row < data.size(); ++row) {
            f << row;
            for (auto val : data[row]) f << "," << val;
            f << "\n";
        }
    };

    {
        string header = "Step";
        for (const auto& name : commodityNames) header += "," + name;
        writeCsv("prices.csv", header, sim.priceHist);
    }
    {
        string header = "Step";
        for (const auto& name : sim.buildingTypeNames) header += "," + name;
        writeCsv("buildings.csv", header, sim.buildingHist);
    }
    {
        string header = "Step";
        for (const auto& name : commodityNames) header += "," + name + "产量";
        writeCsv("outputs.csv", header, sim.outputHist);
    }
    {
        string header = "Step";
        for (const auto& name : sim.buildingTypeNames) header += "," + name + "利润率";
        writeCsv("profitRate.csv", header, sim.profitRateHist);
    }
    {
        ofstream f("gdp.csv");
        f << "\xEF\xBB\xBFStep,GDP\n";
        for (size_t t = 0; t < sim.gdpHist.size(); ++t)
            f << t << "," << sim.gdpHist[t] << "\n";
    }
    {
        string header = "Step";
        for (const auto& name : sim.buildingTypeNames) header += "," + name;
        writeCsv("cashPool.csv", header, sim.cashPoolHist);
    }
    {
        ofstream f("population.csv");
        f << "\xEF\xBB\xBFStep,Population\n";
        for (size_t t = 0; t < sim.populationHist.size(); ++t)
            f << t << "," << sim.populationHist[t] << "\n";
    }

    cout << "CSV文件写入完毕。\n";
    return 0;
}
