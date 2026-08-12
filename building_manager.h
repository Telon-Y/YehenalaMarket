#pragma once
#include "constants.h"
#include "building_template.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>

struct ConstructionOrder {
    int typeIndex;
    Money totalCost;
    Money remainingCost;
    bool ignoreCash = false;   // true 表示玩家订单，允许透支投资池
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
    const std::array<Money, TYPE_COUNT>& getCashPools() const { return cashPools; }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return currentSupplyRatio; }
    const std::vector<BuildingTemplate>& getTemplates() const { return templates; }

    const std::array<std::array<int, OWNER_COUNT>, TYPE_COUNT>& getOwnedBuildings() const { return ownedBuildings; }
    Money transferOwnership(int typeIdx, int count, OwnerType from, OwnerType to,
                            Money& investmentPool, std::array<Money, CLASS_COUNT>& classCash);

    void placeOrder(int typeIdx, OwnerType owner = OWNER_INITIAL);
    void placePlayerOrder(int typeIdx, int count, bool top);
    void demolishBuildings(int typeIdx, int count, int stepCount, Money& investmentPool);

    void aiBuild(double aiProfitThreshold,
                 const std::array<Money, NUM_GOODS>& prices,
                 const std::array<Money, TYPE_COUNT>& wages,
                 double availableLabor,
                 const std::array<double, TYPE_COUNT>& actualEmploymentRate);

    void setAllowAutoConstExpansion(bool allow) { allowAutoConstExpansion = allow; }
    bool getAllowAutoConstExpansion() const { return allowAutoConstExpansion; }

    std::array<double, TYPE_COUNT> calculateBaseOutputRates(double maxLabor) const;

    Money processConstruction(Money availableConstr, Money constrPrice,
                              Money& investmentPool);

    void updateActualProfitRates(const std::array<double, TYPE_COUNT>& actualRates);

    void adjustEmployment();
    void checkDecay(int stepCount, Money& investmentPool, std::array<Money, CLASS_COUNT>& classCash);
    std::array<int, TYPE_COUNT> getInQueueCounts() const;

    void setCurrentSupplyRatio(const std::array<double, TYPE_COUNT>& ratio) { currentSupplyRatio = ratio; }

    void addCash(int typeIdx, Money amount) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT) {
            cashPools[typeIdx] += amount;
            clampCash(typeIdx);
        }
    }

    void clampAllCash() {
        for (int t = 0; t < TYPE_COUNT; ++t) clampCash(t);
    }

    Money getTotalDebt() const {
        Money debt = Money(0);
        for (int t = 0; t < TYPE_COUNT; ++t)
            if (cashPools[t] < Money(0)) debt -= cashPools[t];
        return debt;
    }

    void setProfitRate(int typeIdx, double rate) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT)
            avgProfitRates[typeIdx] = rate;
    }

    // ===== 新增：获取银行上限 =====
    int getMaxBanks() const { return maxBanks; }

private:
    void clampCash(int typeIdx) {
        Money& c = cashPools[typeIdx];
        if (!isfinite(c)) c = Money(0);
        c = clamp(c, -BUILDING_CASH_MAX_MONEY, BUILDING_CASH_MAX_MONEY);
    }

    void resetDecayCounters();
    void syncFinanceCount();
    void cleanupDeadBuilding(int typeIdx, Money& investmentPool);

    std::vector<BuildingTemplate> templates;
    std::array<int, TYPE_COUNT> buildingCounts;
    std::vector<ConstructionOrder> constructionQueue;
    std::array<Money, TYPE_COUNT> cashPools;
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
    int maxGoldMines = 50;      // 修正为 50，与 1.2 文档一致
    int maxBanks = 200;
    int maxFinances = 200;
    int demolishCooldownPeriod = 156;

    bool profitInitialized = false;
    bool allowAutoConstExpansion = true;
};