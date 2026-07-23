#pragma once
#include "constants.h"
#include "building_template.h"
#include <unordered_map>
#include <array>
#include <vector>

struct ConstructionOrder {
    int typeIndex;
    double totalCost;
    double remainingCost;
};

class MarketSimulation {
public:
    // 内部状态（保留货币与劳动力部分）
    std::unordered_map<std::string, int> goodIndex;

    std::array<double, NUM_GOODS> prices;
    std::array<double, NUM_GOODS> v;          // 价格速度
    std::array<double, NUM_GOODS> m;          // 市场惯性（动态）
    std::array<double, NUM_GOODS> b;          // 价格抑制系数

    double averageWage = 6.75;
    double dt = 0.5;
    double population = 10'000'000.0;
    double laborPerCapita = 1.0;
    double maxLabor;
    double inertiaCoeff = 0.5;
    double dampRatio = 0.3;

    std::vector<BuildingTemplate> buildingTemplates;

    std::array<int, TYPE_COUNT> buildingCounts;
    std::vector<ConstructionOrder> constructionQueue;

    std::array<double, TYPE_COUNT> avgProfitRates;
    std::array<double, TYPE_COUNT> smoothedProfitRate;
    std::array<double, TYPE_COUNT> employmentRatio;
    std::array<double, TYPE_COUNT> cashPools;
    std::array<int, TYPE_COUNT> consecutiveLowEmpWeeks;
    int stepCount = 0;

    // 资源上限
    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;

    double satisfaction = 0.75;

    // 历史记录
    std::vector<std::array<double, NUM_GOODS>> priceHist, outputHist;
    std::vector<std::array<double, TYPE_COUNT>> profitRateHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<double> gdpHist;
    std::vector<std::array<double, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;

    MarketSimulation();
    void step();
    void aiBuild();
    void placeOrder(int typeIdx);

    // 辅助函数
    std::array<double, GROUP_COUNT> getGroupDemandPer100k() const;
};