// ============================================================
// 叶赫那拉市场模拟 - 最终版（无完工输出）
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

constexpr int TOTAL_STEPS = 10000;
constexpr int AI_INTERVAL = 1;
constexpr int NUM_GOODS = 11;

const vector<string> commodityNames = {
    "谷物", "加工食品", "织物", "服装", "高档服装",
    "煤", "铁", "钢", "工具", "住房", "建造力"
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
    double averageWage = 6.75;               // 0.75*5 + 0.2*10 + 0.05*20
    double dt = 0.5;
    double population = 10000000.0;
    double popGrowthRate = 0.00096;
    double laborPerCapita = 1.0, maxLabor;
    double inertiaCoeff = 0.5, dampRatio = 0.3;

    vector<string> buildingTypeNames;
    vector<BuildingTemplate> buildingTemplates;
    vector<double> buildingCost;

    array<int, TYPE_COUNT> buildingCounts;
    vector<ConstructionOrder> constructionQueue;
    array<double, TYPE_COUNT> avgProfitRates;
    array<double, TYPE_COUNT> employmentRatio;
    array<double, TYPE_COUNT> cashPools;

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;

    // 消费组需求表（每10万人）
    const array<array<double, GROUP_COUNT>, 3> demandTable = {{
        {39.0, 106.0, 0.0, 20.0},   // 财富5
        {41.0, 137.0, 7.0, 74.0},   // 财富10
        {0.0,  163.0, 122.0, 130.0} // 财富20
    }};
    vector<vector<int>> groupGoods;

    // 历史记录
    vector<array<double, NUM_GOODS>> priceHist, outputHist;
    vector<array<double, TYPE_COUNT>> profitRateHist;
    vector<array<int, TYPE_COUNT>> buildingHist;
    vector<double> gdpHist;
    vector<array<double, TYPE_COUNT>> cashPoolHist;   // 现金池历史

    MarketSimulation() {
        for (int i = 0; i < NUM_GOODS; ++i)
            goodIndex[commodityNames[i]] = i;

        prices.fill(1.0); v.fill(0.0); m.fill(1.0); b.fill(0.15);
        referencePrice = {5.0,10.0,5.0,12.0,25.0,8.0,8.0,15.0,20.0,30.0,15.0};
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

        setTemplate(FARM_GRAIN, "谷物农场", goodIndex["谷物"], 20, {});
        setTemplate(FOOD_PROC, "加工食品厂", goodIndex["加工食品"], 45, {{goodIndex["谷物"], 40.0/45}});
        setTemplate(COTTON, "棉花种植园", goodIndex["织物"], 45, {});
        setTemplate(CLOTHES, "服装厂", goodIndex["服装"], 60, {{goodIndex["织物"], 40.0/60}});
        setTemplate(LUXURY_CLOTHES, "高档服装厂", goodIndex["高档服装"], 30, {{goodIndex["织物"], 25.0/30}});
        setTemplate(COAL_MINE, "煤矿", goodIndex["煤"], 40, {{goodIndex["工具"], 10.0/40}});
        setTemplate(IRON_MINE, "铁矿", goodIndex["铁"], 40, {{goodIndex["工具"], 10.0/40}});
        setTemplate(STEEL_MILL, "炼钢厂", goodIndex["钢"], 90, {{goodIndex["铁"],60.0/90},{goodIndex["煤"],30.0/90}});
        setTemplate(TOOL_FACT, "工具厂", goodIndex["工具"], 80, {{goodIndex["钢"],20.0/80}});
        setTemplate(HOUSING, "住房", goodIndex["住房"], 60, {{goodIndex["钢"],5.0/60},{goodIndex["工具"],5.0/60}});
        setTemplate(CONST_DEPT, "建造部门", goodIndex["建造力"], 15, {{goodIndex["钢"],50.0/15},{goodIndex["工具"],20.0/15}});

        maxLabor = population * laborPerCapita;
        buildingCounts.fill(10);
        avgProfitRates.fill(0.0);
        employmentRatio.fill(1.0);

        for (int t = 0; t < TYPE_COUNT; ++t)
            cashPools[t] = buildingCounts[t] * 5000.0;

        // 配置消费组
        groupGoods.resize(GROUP_COUNT);
        groupGoods[GRP_SIMPLE_CLOTHES]   = {goodIndex["织物"]};
        groupGoods[GRP_BASIC_FOOD]       = {goodIndex["谷物"], goodIndex["加工食品"]};
        groupGoods[GRP_STANDARD_CLOTHES] = {goodIndex["服装"], goodIndex["高档服装"]};
        groupGoods[GRP_HOUSING]          = {goodIndex["住房"]};
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
        population *= 1.0 + popGrowthRate;
        maxLabor = population * laborPerCapita;
        const int constrIdx = goodIndex["建造力"];

        // 1. 利润因子
        array<double,TYPE_COUNT> profitFactor;
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) { profitFactor[t]=0.0; continue; }
            profitFactor[t] = buildingTemplates[t].getProfitFactor(prices, averageWage);
        }

        // 2. 期望产出与劳动力需求
        array<double,TYPE_COUNT> estOutputPerBuilding;
        double totalDesiredLabor = 0.0;
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) { estOutputPerBuilding[t]=0.0; continue; }
            const auto& bt = buildingTemplates[t];
            estOutputPerBuilding[t] = bt.outputRate * profitFactor[t] * employmentRatio[t];
            totalDesiredLabor += buildingCounts[t] * bt.laborPerUnit * estOutputPerBuilding[t];
        }
        double laborFactor = totalDesiredLabor > 0 ? min(1.0, maxLabor / totalDesiredLabor) : 1.0;

        // 3. 实际产出、中间投入
        array<double,NUM_GOODS> totalOut{}, totalIn{};
        double actualLabor = 0.0;
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) continue;
            const auto& bt = buildingTemplates[t];
            double actualPerBuilding = estOutputPerBuilding[t] * laborFactor;
            double typeTotalProd = buildingCounts[t] * actualPerBuilding;
            totalOut[bt.outputGood] += typeTotalProd;
            for (int g=0; g<NUM_GOODS; ++g)
                totalIn[g] += buildingCounts[t] * bt.inputs[g] * actualPerBuilding;
            actualLabor += buildingCounts[t] * bt.laborPerUnit * actualPerBuilding;
        }

        // 4. 利润入现金池
        double wageIncome = actualLabor * averageWage;
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) continue;
            const auto& bt = buildingTemplates[t];
            double actualPerBuilding = estOutputPerBuilding[t] * laborFactor;
            double totalProd = buildingCounts[t] * actualPerBuilding;
            double revenue = totalProd * prices[bt.outputGood];
            double inputCost = 0.0;
            for (int g=0; g<NUM_GOODS; ++g)
                inputCost += buildingCounts[t] * bt.inputs[g] * actualPerBuilding * prices[g];
            double laborCost = buildingCounts[t] * bt.laborPerUnit * actualPerBuilding * averageWage;
            double profit = revenue - inputCost - laborCost;
            cashPools[t] += profit;
            if (cashPools[t] < 0) cashPools[t] = 0;
        }

        // 5. 消费组分配
        array<double,NUM_GOODS> netSupply;
        for (int i=0; i<NUM_GOODS; ++i) netSupply[i] = max(0.0, totalOut[i] - totalIn[i]);

        array<double,NUM_GOODS> weight;
        for (int i=0; i<NUM_GOODS; ++i)
            weight[i] = (totalOut[i] - totalIn[i]) / (2.0 * prices[i] + 1e-9);

        auto per100k = getGroupDemandPer100k();
        double scale = population / 100000.0;
        array<double,GROUP_COUNT> groupDemand;
        for (int g=0; g<GROUP_COUNT; ++g) groupDemand[g] = per100k[g] * scale;

        array<double,NUM_GOODS> avail = netSupply;
        array<double,NUM_GOODS> consumerTarget{}, consumerActual{};

        for (int g=0; g<GROUP_COUNT; ++g) {
            double remain = groupDemand[g];
            if (remain <= 0) continue;

            vector<int> goods = groupGoods[g];
            sort(goods.begin(), goods.end(),
                 [&](int a, int b) { return weight[a] > weight[b]; });

            for (int good : goods) {
                if (remain <= 0) break;
                double want = remain;
                consumerTarget[good] += want;
                double sold = min(want, avail[good]);
                consumerActual[good] += sold;
                avail[good] -= sold;
                remain -= sold;
            }
        }

        // 6. 建造力需求与支付
        double constrDem = 0.0;
        for (const auto& ord : constructionQueue) constrDem += ord.remainingCost;

        array<double,NUM_GOODS> E;
        for (int i=0; i<NUM_GOODS; ++i) {
            double totalDemand = totalIn[i] + consumerTarget[i];
            E[i] = totalDemand - totalOut[i];
        }
        E[constrIdx] += constrDem;

        double availConstr = totalOut[constrIdx];
        double pConstr = prices[constrIdx];
        for (auto& ord : constructionQueue) {
            if (availConstr <= 0) break;
            double maxByCash = cashPools[ord.typeIndex] / pConstr;
            double invest = min({availConstr, 30.0, ord.remainingCost, maxByCash});
            if (invest <= 0) continue;
            cashPools[ord.typeIndex] -= invest * pConstr;
            ord.remainingCost -= invest;
            availConstr -= invest;
        }

        // 移除完成的订单（不再记录完工数量）
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

        // ============ GDP 计算 ============
        double consumerExpenditure = 0.0;
        for (int i=0; i<NUM_GOODS; ++i)
            consumerExpenditure += consumerActual[i] * prices[i];

        double totalCashPool = accumulate(cashPools.begin(), cashPools.end(), 0.0);
        double gdp = consumerExpenditure + totalCashPool;

        // 价格更新
        for (int i=0; i<NUM_GOODS; ++i) {
            double G = fabs(totalOut[i]) + fabs(totalIn[i]) + fabs(consumerTarget[i]) + 1.0;
            m[i] = inertiaCoeff * G;
            double restoring = b[i] * (prices[i] - referencePrice[i]);
            double rho = dampRatio * 2.0 * sqrt(m[i] * b[i]);
            double acc = (E[i] - rho * v[i] - restoring) / m[i];
            v[i] += acc * dt;
            prices[i] += v[i] * dt;
            if (prices[i] < 0.1) prices[i] = 0.1;
        }

        // 利润率
        array<double,TYPE_COUNT> profitRates{};
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) continue;
            const auto& bt = buildingTemplates[t];
            double unitCost = bt.laborPerUnit * averageWage;
            for (int g=0; g<NUM_GOODS; ++g) unitCost += prices[g] * bt.inputs[g];
            profitRates[t] = (unitCost>1e-6) ? (prices[bt.outputGood] - unitCost)/unitCost : 0.0;
        }
        avgProfitRates = profitRates;
        profitRateHist.push_back(profitRates);

        // 雇佣调整
        for (int t=0; t<TYPE_COUNT; ++t) {
            if (buildingCounts[t]==0) continue;
            if (avgProfitRates[t] < 0.0)
                employmentRatio[t] *= 0.95;
            else
                employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.05;
            employmentRatio[t] = max(0.1, min(1.0, employmentRatio[t]));
        }

        // 记录历史
        gdpHist.push_back(gdp);
        buildingHist.push_back(buildingCounts);
        outputHist.push_back(totalOut);
        priceHist.push_back(prices);
        cashPoolHist.push_back(cashPools);
    }

    void aiBuild() {
        array<int,TYPE_COUNT> inQueueCount{};
        for (const auto& ord : constructionQueue) inQueueCount[ord.typeIndex]++;

        int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                       + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
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

        if (stepsNeeded > 84.0 && inQueueCount[CONST_DEPT] == 0 && !constrCapReached) {
            constructionQueue.insert(constructionQueue.begin(),
                                     {CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT]});
        }

        for (int t = 0; t < TYPE_COUNT; ++t) {
            if ((t == FARM_GRAIN || t == COTTON) && farmCapReached) continue;
            if (t == COAL_MINE && coalCapReached) continue;
            if (t == IRON_MINE && ironCapReached) continue;
            if (t == CONST_DEPT && constrCapReached) continue;

            double rate = avgProfitRates[t];
            if (rate > 0.1 && buildingCounts[t] > 0) {
                int N_wanted = (int)ceil((rate - 0.1) / 0.05);
                if (N_wanted < 0) N_wanted = 0;
                int N_remaining = N_wanted - inQueueCount[t];
                if (N_remaining < 0) N_remaining = 0;

                if (t == FARM_GRAIN || t == COTTON) {
                    int slots = maxTotalFarms - totalFarms;
                    if (slots <= 0) continue;
                    N_remaining = min(N_remaining, slots);
                } else if (t == COAL_MINE) {
                    int slots = maxCoalMines - (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE]);
                    if (slots <= 0) continue;
                    N_remaining = min(N_remaining, slots);
                } else if (t == IRON_MINE) {
                    int slots = maxIronMines - (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE]);
                    if (slots <= 0) continue;
                    N_remaining = min(N_remaining, slots);
                } else if (t == CONST_DEPT) {
                    int slots = maxConstDept - (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT]);
                    if (slots <= 0) continue;
                    N_remaining = min(N_remaining, slots);
                }

                int maxExpand = (int)ceil(buildingCounts[t] * 0.1);
                int N_final = min(N_remaining, maxExpand);

                for (int j = 0; j < N_final; ++j)
                    placeOrder(t);
            }
        }
    }
};

int main() {
    SetConsoleOutputCP(CP_UTF8);
    MarketSimulation sim;

    cout << "模拟开始，共 " << TOTAL_STEPS << " 步...\n" << flush;

    for (int t = 0; t < TOTAL_STEPS; ++t) {
        sim.step();
        if (t % AI_INTERVAL == 0) sim.aiBuild();
        if (t % 84 == 0) cout << "周期: " << t << "/" << TOTAL_STEPS << "     \r" << flush;
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

    cout << "CSV文件写入完毕。\n";
    return 0;
}