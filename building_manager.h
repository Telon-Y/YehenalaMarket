#pragma once
#include "constants.h"
#include "building_template.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

struct ConstructionOrder {
    int typeIndex;
    double totalCost;
    double remainingCost;
    bool ignoreCash = false;
    OwnerType owner = OWNER_INITIAL;
};

class BuildingManager {
public:
    BuildingManager();

    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return buildingCounts; }
    const std::vector<ConstructionOrder>& getQueue() const { return constructionQueue; }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return employmentRatio; }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return avgProfitRates; }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return smoothedProfitRate; }
    const std::array<double, TYPE_COUNT>& getCashPools() const { return cashPools; }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return currentSupplyRatio; }
    const std::vector<BuildingTemplate>& getTemplates() const { return templates; }

    const std::array<std::array<int, OWNER_COUNT>, TYPE_COUNT>& getOwnedBuildings() const { return ownedBuildings; }
    double transferOwnership(int typeIdx, int count, OwnerType from, OwnerType to,
                             double& investmentPool, std::array<double, CLASS_COUNT>& classCash);

    void placeOrder(int typeIdx, OwnerType owner = OWNER_INITIAL);
    void placePlayerOrder(int typeIdx, int count, bool top);
    void demolishBuildings(int typeIdx, int count, int stepCount, double& investmentPool);

    // 修改：增加 actualEmploymentRate 参数
    void aiBuild(double aiProfitThreshold,
                 const std::array<double, NUM_GOODS>& prices,
                 double averageWage,
                 double availableLabor,
                 const std::array<double, TYPE_COUNT>& actualEmploymentRate);

    std::array<double, TYPE_COUNT> calculateBaseOutputRates(double maxLabor) const;
    
    double processConstruction(double availableConstr, double constrPrice,
                               double& investmentPool);
    
    void updateActualProfitRates(const std::array<double, TYPE_COUNT>& actualRates);
    
    void adjustEmployment();
    void checkDecay(int stepCount, double& investmentPool, std::array<double, CLASS_COUNT>& classCash);
    std::array<int, TYPE_COUNT> getInQueueCounts() const;

    void setCurrentSupplyRatio(const std::array<double, TYPE_COUNT>& ratio) { currentSupplyRatio = ratio; }

    void addCash(int typeIdx, double amount) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT) {
            cashPools[typeIdx] += amount;
            clampCash(typeIdx);
        }
    }

    void clampAllCash() {
        for (int t = 0; t < TYPE_COUNT; ++t) clampCash(t);
    }

    double getTotalDebt() const {
        double debt = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t)
            if (cashPools[t] < 0.0) debt -= cashPools[t];
        return debt;
    }

    void setProfitRate(int typeIdx, double rate) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT)
            avgProfitRates[typeIdx] = rate;
    }

private:
    void clampCash(int typeIdx) {
        double& c = cashPools[typeIdx];
        if (!std::isfinite(c)) c = 0.0;
        c = std::clamp(c, -1e15, 1e15);
    }

    void resetDecayCounters();
    void syncFinanceCount();
    void cleanupDeadBuilding(int typeIdx, double& investmentPool);

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

    std::array<std::array<int, OWNER_COUNT>, TYPE_COUNT> ownedBuildings{};

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;
    int maxGoldMines = 500;
    int maxBanks = 200;
    int maxFinances = 200;
    int demolishCooldownPeriod = 156;

    bool profitInitialized = false;
};