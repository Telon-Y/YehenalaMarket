#pragma once
#include "constants.h"
#include "building_template.h"
#include <vector>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>

struct ConstructionOrder {
    int typeIndex;
    Money totalCost;
    Money remainingCost;
    OwnerType owner = OWNER_INITIAL;
    std::string payerCountryTag;
    Money reservedBudget = Money(0);
    bool hasReservedBudget = false;
};

struct ConstructionSettlement {
    Money totalUsed = Money(0);
    Money privatePayment = Money(0);
    Money governmentPayment = Money(0);
    Money governmentBudgetReleased = Money(0);
};

struct AIExpansionCandidate {
    int typeIndex = -1;
    int maxUnits = 0;
    double priority = 0.0;
    Money unitConstructionCost = Money(0);
};

class BuildingManager {
public:
    BuildingManager();

    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return buildingCounts; }
    const std::vector<ConstructionOrder>& getQueue() const { return constructionQueue; }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return employmentRatio; }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return avgProfitRates; }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return smoothedProfitRate; }
    const std::array<Money, TYPE_COUNT>& getActualUnitProfits() const { return actualUnitProfits; }
    const std::array<Money, TYPE_COUNT>& getCashPools() const { return cashPools; }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return currentSupplyRatio; }
    const std::array<double, TYPE_COUNT>& getCapacityUtilization() const {
        return capacityUtilization;
    }
    const std::array<double, TYPE_COUNT>& getFundingAvailability() const {
        return fundingAvailability;
    }
    const std::array<double, TYPE_COUNT>& getMaterialAvailability() const {
        return materialAvailability;
    }
    const std::array<Money, TYPE_COUNT>& getStaffedCapacity() const {
        return staffedCapacity;
    }
    const std::array<Money, TYPE_COUNT>& getProductionTarget() const {
        return productionTarget;
    }
    const std::vector<BuildingTemplate>& getTemplates() const { return templates; }

    const std::array<std::array<int, OWNER_COUNT>, TYPE_COUNT>& getOwnedBuildings() const { return ownedBuildings; }
    Money transferOwnership(int typeIdx, int count, OwnerType from, OwnerType to,
                            Money& investmentPool, std::array<Money, CLASS_COUNT>& classCash);

    void placeOrder(int typeIdx, OwnerType owner = OWNER_INITIAL,
                    const std::string& payerCountryTag = {},
                    Money reservedBudget = Money(0));
    int placeOrders(int typeIdx, int count, OwnerType owner,
                    const std::string& payerCountryTag = {},
                    Money reservedBudgetPerOrder = Money(0));
    void placePlayerOrder(int typeIdx, int count, bool top);
    void addCompletedBuildings(int typeIdx, int count,
                               OwnerType owner = OWNER_GOVERNMENT);
    bool setBuildingCountForSetup(int typeIdx, int count,
                                  OwnerType owner = OWNER_INITIAL);
    bool canDemolish(int typeIdx, int stepCount) const;
    int demolishBuildings(int typeIdx, int count, int stepCount, Money& investmentPool);

    void aiBuild(double aiProfitThreshold,
                 const std::array<Money, NUM_GOODS>& prices,
                 const std::array<Money, TYPE_COUNT>& wages,
                 double availableLabor,
                 const std::array<double, TYPE_COUNT>& actualEmploymentRate,
                 OwnerType automaticOwner = OWNER_FINANCE,
                 Money* investmentPool = nullptr);

    std::vector<AIExpansionCandidate> collectAIExpansionCandidates(
        double aiProfitThreshold,
        const std::array<Money, NUM_GOODS>& prices,
        const std::array<Money, TYPE_COUNT>& wages,
        double availableLabor,
        const std::array<double, TYPE_COUNT>& actualEmploymentRate) const;

    void setAllowAutoConstExpansion(bool allow) { allowAutoConstExpansion = allow; }
    bool getAllowAutoConstExpansion() const { return allowAutoConstExpansion; }
    void tagGovernmentOrders(const std::string& payerCountryTag);

    std::array<double, TYPE_COUNT> calculateBaseOutputRates(double maxLabor) const;

    Money getWeeklyPrivateConstructionDemand(Money availableConstr) const;
    ConstructionSettlement processConstruction(Money availableConstr,
                                               Money constrPrice,
                                               Money& investmentPool,
                                               Money& governmentCash,
                                               bool applyCash = true,
                                               bool transferPrivatePayment = true);

    void updateActualProfitRates(const std::array<double, TYPE_COUNT>& actualRates);
    void updateActualUnitProfits(const std::array<Money, TYPE_COUNT>& actualProfits);

    void adjustEmployment();
    void checkDecay(int stepCount, Money& investmentPool, std::array<Money, CLASS_COUNT>& classCash);
    std::array<int, TYPE_COUNT> getInQueueCounts() const;

    void setCurrentSupplyRatio(const std::array<double, TYPE_COUNT>& ratio) { currentSupplyRatio = ratio; }
    void setProductionMetrics(
        const std::array<double, TYPE_COUNT>& utilization,
        const std::array<double, TYPE_COUNT>& funding,
        const std::array<double, TYPE_COUNT>& material,
        const std::array<Money, TYPE_COUNT>& staffed,
        const std::array<Money, TYPE_COUNT>& target) {
        capacityUtilization = utilization;
        fundingAvailability = funding;
        materialAvailability = material;
        staffedCapacity = staffed;
        productionTarget = target;
        currentSupplyRatio = material;
    }

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

    void setActualUnitProfit(int typeIdx, Money profit) {
        if (typeIdx >= 0 && typeIdx < TYPE_COUNT)
            actualUnitProfits[typeIdx] = isfinite(profit) ? profit : Money(0);
    }

    void syncBankLevels(Money savingsPool);
    // Development buildings are public work.  The caller supplies the
    // government's available cash and receives the amount that was actually
    // paid (which may be lower than the requested payroll when funds run out).
    // The legacy pool arguments remain in the signature for source
    // compatibility; development payroll no longer draws from them.
    Money payDevelopmentWages(int typeIdx, Money wages,
                              Money& governmentCash,
                              Money& investmentPool,
                              std::array<Money, CLASS_COUNT>& classCash);
    void setFinanceLevelFromSecurities(int level);

    // 建筑完成时的回调（用于发行证券）
    std::function<void(int typeIdx, int count, OwnerType owner)> onBuildingCompleted;
    std::function<void(int typeIdx, int count)> onBuildingsRemoving;

private:
    void clampCash(int typeIdx) {
        Money& c = cashPools[typeIdx];
        if (!isfinite(c)) c = Money(0);
        c = clamp(c, -BUILDING_CASH_MAX_MONEY, BUILDING_CASH_MAX_MONEY);
    }

    void resetDecayCounters();
    void setBankLevelFromPool(int typeIdx, Money pool);
    void syncFinanceCount();   // 已废弃，保留空实现
    void cleanupDeadBuilding(int typeIdx, Money& investmentPool);

    std::vector<BuildingTemplate> templates;
    std::array<int, TYPE_COUNT> buildingCounts;
    std::vector<ConstructionOrder> constructionQueue;
    std::array<Money, TYPE_COUNT> cashPools;
    std::array<double, TYPE_COUNT> employmentRatio;
    std::array<double, TYPE_COUNT> avgProfitRates;
    std::array<double, TYPE_COUNT> smoothedProfitRate;
    std::array<Money, TYPE_COUNT> actualUnitProfits;
    std::array<int, TYPE_COUNT> consecutiveLowEmpWeeks;
    std::array<double, TYPE_COUNT> currentSupplyRatio;
    std::array<double, TYPE_COUNT> capacityUtilization;
    std::array<double, TYPE_COUNT> fundingAvailability;
    std::array<double, TYPE_COUNT> materialAvailability;
    std::array<Money, TYPE_COUNT> staffedCapacity;
    std::array<Money, TYPE_COUNT> productionTarget;
    std::array<int, TYPE_COUNT> lastDemolishStep;

    std::array<std::array<int, OWNER_COUNT>, TYPE_COUNT> ownedBuildings{};

    int maxTotalFarms = 10000;
    int maxCoalMines = 500;
    int maxIronMines = 500;
    int maxConstDept = 1000;
    int maxGoldMines = 50;
    int maxFinances = 200;
    int demolishCooldownPeriod = 156;

    bool profitInitialized = false;
    bool allowAutoConstExpansion = true;
};
