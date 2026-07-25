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
    bool ignoreCash = false;
};

class MarketSimulation {
public:
    std::unordered_map<std::string, int> goodIndex;

    std::array<double, NUM_GOODS> prices;
    std::array<double, NUM_GOODS> v;
    std::array<double, NUM_GOODS> m;
    std::array<double, NUM_GOODS> b;

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

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;

    double satisfaction = 0.75;
    int demolishCooldownPeriod = 156;               // 拆除冷却 3 年
    std::array<int, TYPE_COUNT> lastDemolishStep;

    std::vector<std::array<double, NUM_GOODS>> priceHist, outputHist;
    std::vector<std::array<double, TYPE_COUNT>> profitRateHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<double> gdpHist;
    std::vector<std::array<double, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;

    std::array<double, NUM_GOODS> latestConsumerTarget;
    std::array<double, NUM_GOODS> latestConsumerActual;
    std::array<double, NUM_GOODS> latestPotentialIn;   // 中间需求
    int subsistenceFarms = 0;

    std::array<double, TYPE_COUNT> currentSupplyRatio; // ★ 原料配给率（含下限）

    MarketSimulation();
    void step();
    void aiBuild();
    void placeOrder(int typeIdx);
    void placePlayerOrder(int typeIdx, int count = 1, bool top = true);
    void removeBuilding(int typeIndex);
    void demolishBuildings(int typeIndex, int count);

    double aiProfitThreshold = 0.1;

    std::array<double, GROUP_COUNT> getGroupDemandPer100k() const;
};