#pragma once
#include "constants.h"
#include "building_manager.h"
#include <unordered_map>
#include <array>
#include <vector>

class LocalMarket {
public:
    LocalMarket();

    void step();
    void aiBuild();

    // ---- 只读访问器 ----
    int getStepCount() const { return stepCount; }
    const std::array<double, NUM_GOODS>& getPrices() const { return prices; }
    const std::vector<BuildingTemplate>& getBuildingTemplates() const { return bld.getTemplates(); }
    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return bld.getBuildingCounts(); }
    const std::vector<ConstructionOrder>& getConstructionQueue() const { return bld.getQueue(); }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return bld.getEmploymentRatio(); }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return bld.getAvgProfitRates(); }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return bld.getSmoothedProfitRate(); }
    const std::array<double, TYPE_COUNT>& getCashPools() const { return bld.getCashPools(); }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return bld.getCurrentSupplyRatio(); }
    const std::vector<std::array<double, NUM_GOODS>>& getPriceHistory() const { return priceHist; }
    const std::vector<double>& getGDPHistory() const { return gdpHist; }
    const std::vector<double>& getPopulationHistory() const { return populationHist; }
    const std::array<double, NUM_GOODS>& getLatestConsumerTarget() const { return latestConsumerTarget; }
    const std::array<double, NUM_GOODS>& getLatestConsumerActual() const { return latestConsumerActual; }
    const std::array<double, NUM_GOODS>& getLatestPotentialIn() const { return latestPotentialIn; }
    int getSubsistenceFarms() const { return subsistenceFarms; }
    double getPopulation() const { return population; }
    double getSatisfaction() const { return satisfaction; }
    double getGDP() const { return gdpHist.empty() ? 0.0 : gdpHist.back(); }

    // ---- 玩家操作 ----
    void playerBuild(int typeIdx, int count);
    void playerDemolish(int typeIdx, int count);
    void setAIProfitThreshold(double v) { aiProfitThreshold = v; }
    double getAIProfitThreshold() const { return aiProfitThreshold; }

    // 用于 UI 高级查询（如预估周数）提供对 BuildingManager 的只读访问
    const BuildingManager& getBuildingManager() const { return bld; }

private:
    BuildingManager bld;

    // 市场核心状态
    std::unordered_map<std::string, int> goodIndex;
    std::array<double, NUM_GOODS> prices;
    std::array<double, NUM_GOODS> v;
    std::array<double, NUM_GOODS> m;
    std::array<double, NUM_GOODS> b;
    double averageWage = 6.75;
    double dt = 0.2;
    double population = 10'000'000.0;
    double maxLabor;
    double inertiaCoeff = 0.5;
    double dampRatio = 0.3;
    double satisfaction = 0.75;
    double aiProfitThreshold = 0.1;
    int stepCount = 0;

    double subsistencePop = 0.0;
    double totalLaborers = 0.0;
    double totalEngineers = 0.0;
    double totalCapitalists = 0.0;

    // 历史记录
    std::vector<std::array<double, NUM_GOODS>> priceHist, outputHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<double> gdpHist;
    std::vector<std::array<double, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;

    std::array<double, NUM_GOODS> latestConsumerTarget;
    std::array<double, NUM_GOODS> latestConsumerActual;
    std::array<double, NUM_GOODS> latestPotentialIn;
    int subsistenceFarms = 0;
};