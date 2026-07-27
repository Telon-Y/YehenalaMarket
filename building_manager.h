#pragma once
#include "constants.h"
#include "building_template.h"
#include <vector>
#include <array>

struct ConstructionOrder {
    int typeIndex;
    double totalCost;
    double remainingCost;
    bool ignoreCash = false;
};

class BuildingManager {
public:
    BuildingManager();

    // ---- 只读访问器 ----
    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return buildingCounts; }
    const std::vector<ConstructionOrder>& getQueue() const { return constructionQueue; }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return employmentRatio; }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return avgProfitRates; }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return smoothedProfitRate; }
    const std::array<double, TYPE_COUNT>& getCashPools() const { return cashPools; }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return currentSupplyRatio; }
    const std::vector<BuildingTemplate>& getTemplates() const { return templates; }

    // ---- 操作接口 ----
    void placeOrder(int typeIdx);
    void placePlayerOrder(int typeIdx, int count, bool top);
    void demolishBuildings(int typeIdx, int count, int stepCount);

    void aiBuild(double aiProfitThreshold, const std::array<double, NUM_GOODS>& prices, double averageWage);

    std::array<double, TYPE_COUNT> calculateBaseOutputRates(double maxLabor) const;
    double processConstruction(double availableConstr, double constrPrice);
    void updateProfitRates(const std::array<double, NUM_GOODS>& prices, double averageWage);
    void adjustEmployment();
    void checkDecay(int stepCount);
    std::array<int, TYPE_COUNT> getInQueueCounts() const;

    void setCurrentSupplyRatio(const std::array<double, TYPE_COUNT>& ratio) { currentSupplyRatio = ratio; }

    // 用于 LocalMarket 更新现金池（公开，但仅由 LocalMarket 调用）
    void addCash(int typeIdx, double amount) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT)
            cashPools[typeIdx] += amount;
    }

private:
    void resetDecayCounters();

    std::vector<BuildingTemplate> templates;

    std::array<int, TYPE_COUNT> buildingCounts;
    std::vector<ConstructionOrder> constructionQueue;
    std::array<double, TYPE_COUNT> cashPools;
    std::array<double, TYPE_COUNT> employmentRatio;
    std::array<double, TYPE_COUNT> avgProfitRates;
    std::array<double, TYPE_COUNT> smoothedProfitRate;
    std::array<int, TYPE_COUNT> consecutiveLowEmpWeeks;
    std::array<double, TYPE_COUNT> currentSupplyRatio;
    std::array<int, TYPE_COUNT> lastDemolishStep;

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;
    int demolishCooldownPeriod = 156;

    bool profitInitialized = false;
};