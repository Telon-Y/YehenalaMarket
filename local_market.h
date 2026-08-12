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
    const std::array<Money, NUM_GOODS>& getPrices() const { return prices; }
    const std::vector<BuildingTemplate>& getBuildingTemplates() const { return bld.getTemplates(); }
    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return bld.getBuildingCounts(); }
    const std::vector<ConstructionOrder>& getConstructionQueue() const { return bld.getQueue(); }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return bld.getEmploymentRatio(); }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return bld.getAvgProfitRates(); }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return bld.getSmoothedProfitRate(); }
    const std::array<Money, TYPE_COUNT>& getCashPools() const { return bld.getCashPools(); }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return bld.getCurrentSupplyRatio(); }
    const std::vector<std::array<Money, NUM_GOODS>>& getPriceHistory() const { return priceHist; }
    const std::vector<Money>& getGDPHistory() const { return gdpHist; }
    const std::vector<double>& getPopulationHistory() const { return populationHist; }
    const std::array<Money, NUM_GOODS>& getLatestConsumerTarget() const { return latestConsumerTarget; }
    const std::array<Money, NUM_GOODS>& getLatestConsumerActual() const { return latestConsumerActual; }
    const std::array<Money, NUM_GOODS>& getLatestPotentialIn() const { return latestPotentialIn; }
    const std::array<Money, NUM_GOODS>& getLatestRealOut() const { return latestRealOut; }
    const std::array<Money, TYPE_COUNT>& getLatestBuildingOutput() const { return latestBuildingOutput; }
    int getSubsistenceFarms() const { return subsistenceFarms; }
    double getPopulation() const { return population; }
    double getSatisfaction() const { return satisfaction; }
    Money getGDP() const { return gdpHist.empty() ? Money(0) : gdpHist.back(); }
    Money getTotalMoneySupply() const { return totalMoneySupply; }
    Money getInvestmentPool() const { return investmentPool; }
    Money getClassCash(int idx) const { return classCash[idx]; }
    Money getLoanBalance(int typeIdx) const { return loanBalance[typeIdx]; }
    const std::array<double, TYPE_COUNT>& getActualEmployment() const { return actualEmployment; }
    const std::array<double, TYPE_COUNT>& getActualEmploymentRate() const { return actualEmploymentRate; }
    const std::array<Money, TYPE_COUNT>& getWages() const { return buildingWages; }
    Money getPriceLevel() const { return priceLevel; }
    const std::array<Money, NUM_GOODS>& getBaseReferencePrice() const { return baseReferencePrice; }

    // 新增：建造力统计
    Money getLastConstrProduced() const { return lastConstrProduced; }
    Money getLastConstrUsed() const { return lastConstrUsed; }

    // ===== 新增：贷款接口 =====
    Money getBankLoanCapacity() const { return bankLoanCapacity; }
    Money getInvestmentLoanBalance() const { return investmentLoanBalance; }
    int   getInvestmentLoanDueStep() const { return investmentLoanDueStep; }

    // ===== 新增：建筑贷款笔数 =====
    int getBuildingLoanCount(int typeIdx) const {
        return (typeIdx >= 0 && typeIdx < TYPE_COUNT) ? buildingLoanCount[typeIdx] : 0;
    }

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
    std::array<Money, NUM_GOODS> prices;
    std::array<Money, NUM_GOODS> v;
    std::array<Money, NUM_GOODS> m;
    std::array<Money, NUM_GOODS> b;
    double averageWage = 6.75;
    double dt = 0.2;
    double population = 10'000'000.0;
    double maxLabor;
    double inertiaCoeff = 0.5;
    double dampRatio = 0.50;
    double satisfaction = 0.75;
    double aiProfitThreshold = 0.1;
    int stepCount = 0;

    double subsistencePop = 0.0;
    double totalLaborers = 0.0;
    double totalEngineers = 0.0;
    double totalCapitalists = 0.0;

    std::array<Money, CLASS_COUNT> classCash;
    std::array<Money, CLASS_COUNT> classLastSpending;
    Money investmentPool = Money(200000000.0);  // 初始投资池 2亿
    Money totalMoneySupply = Money(0);
    Money totalDebt = Money(0);
    std::array<Money, TYPE_COUNT> loanBalance;
    std::array<int, TYPE_COUNT> buildingLoanCount{};   // 每类建筑的未偿还贷款笔数

    std::array<Money, TYPE_COUNT> buildingWages;

    std::vector<std::array<Money, NUM_GOODS>> priceHist;
    std::vector<std::array<Money, NUM_GOODS>> outputHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<Money> gdpHist;
    std::vector<std::array<Money, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;

    std::array<Money, NUM_GOODS> latestConsumerTarget;
    std::array<Money, NUM_GOODS> latestConsumerActual;
    std::array<Money, NUM_GOODS> latestPotentialIn;
    std::array<Money, NUM_GOODS> latestRealOut;
    std::array<Money, TYPE_COUNT> latestBuildingOutput;
    int subsistenceFarms = 0;

    std::array<double, TYPE_COUNT> actualEmployment{};
    std::array<double, TYPE_COUNT> actualEmploymentRate{};

    std::array<Money, NUM_GOODS> baseReferencePrice;
    std::array<Money, NUM_GOODS> dynamicReferencePrice;
    Money priceLevel = Money(1.0);
    Money targetPriceLevel = Money(1.0);
    Money initialTotalMoneySupply = Money(0);
    double smoothedLuxuryFactor = 1.0;

    // 新增：建造力统计
    Money lastConstrProduced = Money(0);
    Money lastConstrUsed = Money(0);

    // ===== 新增：贷款变量 =====
    Money bankLoanCapacity = Money(0);
    Money investmentLoanBalance = Money(0);
    int   investmentLoanDueStep = -1;
};