// ==================== building_manager.cpp ====================
// 核心：构造初始化
#include "building_manager.h"
#include <algorithm>
#include <cmath>
#include <numeric>

BuildingManager::BuildingManager() {
    templates = createBuildingTemplates();
    buildingCounts.fill(10);
    buildingCounts[GOLD_MINE] = 0;       // 金矿初始为 0
    for (int t = 0; t < TYPE_COUNT; ++t) {
        ownedBuildings[t][OWNER_INITIAL] = buildingCounts[t];
        ownedBuildings[t][OWNER_GOVERNMENT] = 0;
        ownedBuildings[t][OWNER_FINANCE] = 0;
    }
    ownedBuildings[CONST_DEPT].fill(0);
    ownedBuildings[CONST_DEPT][OWNER_GOVERNMENT] = buildingCounts[CONST_DEPT];
    buildingCounts[FINANCE] = 0;
    buildingCounts[BANK] = 1;
    buildingCounts[INDUSTRIAL_BANK] = 1;
    buildingCounts[SAVINGS_BANK] = 1;
    buildingCounts[RAILWAY] = INITIAL_RAILWAY_LEVELS;
    ownedBuildings[BANK].fill(0);
    ownedBuildings[FINANCE].fill(0);
    ownedBuildings[INDUSTRIAL_BANK].fill(0);
    ownedBuildings[SAVINGS_BANK].fill(0);
    ownedBuildings[RAILWAY].fill(0);
    ownedBuildings[RAILWAY][OWNER_INITIAL] = buildingCounts[RAILWAY];

    avgProfitRates.fill(0.0);
    smoothedProfitRate.fill(0.0);
    actualUnitProfits.fill(Money(0));
    employmentRatio.fill(1.0);
    consecutiveLowEmpWeeks.fill(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK)
            cashPools[t] = Money(100000000.0);
        else if (t == FINANCE)
            cashPools[t] = Money(50000000.0);
        else if (t == CONST_DEPT || t == SAVINGS_BANK)
            cashPools[t] = Money(0);
        else if (t == INDUSTRIAL_BANK)
            cashPools[t] = Money(BANK_MAX_LOAN_PER_LEVEL * BANK_LOAN_UNIT_VALUE);
        else
            cashPools[t] = Money(buildingCounts[t]) * Money(500000.0);
        clampCash(t);
    }
    syncBankLevels(Money(0));
    lastDemolishStep.fill(-9999);
    currentSupplyRatio.fill(1.0);
    capacityUtilization.fill(0.0);
    fundingAvailability.fill(1.0);
    materialAvailability.fill(1.0);
    staffedCapacity.fill(Money(0));
    productionTarget.fill(Money(0));
}

bool BuildingManager::setBuildingCountForSetup(int typeIdx, int count,
                                                OwnerType owner) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || count < 0 ||
        owner < OWNER_GOVERNMENT || owner >= OWNER_COUNT) {
        return false;
    }

    buildingCounts[typeIdx] = count;
    ownedBuildings[typeIdx].fill(0);
    ownedBuildings[typeIdx][owner] = count;
    employmentRatio[typeIdx] = 1.0;
    avgProfitRates[typeIdx] = 0.0;
    smoothedProfitRate[typeIdx] = 0.0;
    actualUnitProfits[typeIdx] = Money(0);
    currentSupplyRatio[typeIdx] = 1.0;
    capacityUtilization[typeIdx] = 0.0;
    fundingAvailability[typeIdx] = 1.0;
    materialAvailability[typeIdx] = 1.0;
    staffedCapacity[typeIdx] = Money(0);
    productionTarget[typeIdx] = Money(0);
    consecutiveLowEmpWeeks[typeIdx] = 0;

    if (!templates[typeIdx].isFinancial) {
        cashPools[typeIdx] = Money(count) * Money(500000.0);
        clampCash(typeIdx);
    }
    return true;
}

void BuildingManager::setBankLevelFromPool(int typeIdx, Money pool) {
    pool = std::max(Money(0), pool);
    int level = std::max(1, static_cast<int>(std::ceil(
        (pool / Money(BANK_LOAN_CAPACITY_PER_LEVEL)).toDouble())));
    buildingCounts[typeIdx] = level;
    ownedBuildings[typeIdx].fill(0);
}

void BuildingManager::syncBankLevels(Money savingsPool) {
    setBankLevelFromPool(BANK, cashPools[BANK]);
    setBankLevelFromPool(INDUSTRIAL_BANK, cashPools[INDUSTRIAL_BANK]);
    setBankLevelFromPool(SAVINGS_BANK, savingsPool);
}

Money BuildingManager::payDevelopmentWages(
    int typeIdx, Money wages, Money& governmentCash, Money& investmentPool,
    std::array<Money, CLASS_COUNT>& classCash) {
    (void)investmentPool;
    (void)classCash;
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || wages <= Money(0) ||
        !templates[typeIdx].isDevelopment()) return Money(0);

    // Keep the government pool solvent. In particular, do not let a
    // negative standalone compatibility balance turn into an implicit wage
    // loan, and do not charge a financial building's own cash or investment
    // pool for public-development payroll.
    const Money availableCash =
        isfinite(governmentCash) ? std::max(Money(0), governmentCash)
                                 : Money(0);
    const Money requestedWages =
        isfinite(wages) ? std::max(Money(0), wages) : Money(0);
    const Money paid = std::min(availableCash, requestedWages);
    governmentCash = availableCash - paid;
    return paid;
}
