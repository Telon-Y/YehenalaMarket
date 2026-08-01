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
    const std::array<double, NUM_GOODS>& getLatestRealOut() const { return latestRealOut; }
    const std::array<double, TYPE_COUNT>& getLatestBuildingOutput() const { return latestBuildingOutput; }
    int getSubsistenceFarms() const { return subsistenceFarms; }
    double getPopulation() const { return population; }
    double getSatisfaction() const { return satisfaction; }
    double getGDP() const { return gdpHist.empty() ? 0.0 : gdpHist.back(); }
    double getTotalMoneySupply() const { return totalMoneySupply; }
    double getInvestmentPool() const { return investmentPool; }
    double getClassCash(int idx) const { return classCash[idx]; }
    double getLoanBalance(int typeIdx) const { return loanBalance[typeIdx]; }
    const std::array<double, TYPE_COUNT>& getActualEmployment() const { return actualEmployment; }
    const std::array<double, TYPE_COUNT>& getActualEmploymentRate() const { return actualEmploymentRate; }
    const std::array<double, TYPE_COUNT>& getWages() const { return buildingWages; }  // 新增
    double getPriceLevel() const { return priceLevel; }
    const std::array<double, NUM_GOODS>& getBaseReferencePrice() const { return baseReferencePrice; }

    void playerBuild(int typeIdx, int count);
    void playerDemolish(int typeIdx, int count);
    void setAIProfitThreshold(double v) { aiProfitThreshold = v; }
    double getAIProfitThreshold() const { return aiProfitThreshold; }
    const BuildingManager& getBuildingManager() const { return bld; }
    bool performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to);
    double getSubsistencePop() const { return subsistencePop; }

private:
    BuildingManager bld;

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
    double dampRatio = 0.85;
    double satisfaction = 0.75;
    double aiProfitThreshold = 0.1;
    int stepCount = 0;

    double subsistencePop = 0.0;
    double totalLaborers = 0.0;
    double totalEngineers = 0.0;
    double totalCapitalists = 0.0;

    std::array<double, CLASS_COUNT> classCash;
    std::array<double, CLASS_COUNT> classLastSpending;
    double investmentPool = 0.0;
    double totalMoneySupply = 0.0;
    double totalDebt = 0.0;
    std::array<double, TYPE_COUNT> loanBalance;

    // 新增：各类建筑实际工资
    std::array<double, TYPE_COUNT> buildingWages;

    std::vector<std::array<double, NUM_GOODS>> priceHist, outputHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<double> gdpHist;
    std::vector<std::array<double, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;

    std::array<double, NUM_GOODS> latestConsumerTarget;
    std::array<double, NUM_GOODS> latestConsumerActual;
    std::array<double, NUM_GOODS> latestPotentialIn;
    std::array<double, NUM_GOODS> latestRealOut;
    std::array<double, TYPE_COUNT> latestBuildingOutput;
    int subsistenceFarms = 0;

    std::array<double, TYPE_COUNT> actualEmployment{};
    std::array<double, TYPE_COUNT> actualEmploymentRate{};

    std::array<double, NUM_GOODS> baseReferencePrice;
    std::array<double, NUM_GOODS> dynamicReferencePrice;
    double priceLevel = 1.0;
    double targetPriceLevel = 1.0;
    double initialTotalMoneySupply = 0.0;
    double smoothedLuxuryFactor = 1.0;
};