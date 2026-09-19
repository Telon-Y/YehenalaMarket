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

bool BuildingManager::setFinancialBuildingLevelsForSetup(
    int centralBank, int finance, int industrialBank, int savingsBank,
    Money savingsPool) {
    if (centralBank < 0 || finance < 0 || industrialBank < 0 ||
        savingsBank < 0 || finance > maxFinances ||
        !isfinite(savingsPool) || savingsPool < Money(0)) {
        return false;
    }

    const Money levelCapacity(BANK_LOAN_CAPACITY_PER_LEVEL);
    const int derivedSavings = savingsPool <= Money(0)
        ? 0
        : static_cast<int>(std::ceil(
              (savingsPool / levelCapacity).toDouble()));
    if (derivedSavings != savingsBank) return false;

    cashPools[BANK] = Money(centralBank) * levelCapacity;
    cashPools[INDUSTRIAL_BANK] = Money(industrialBank) * levelCapacity;
    cashPools[SAVINGS_BANK] = Money(0);
    // Explicit capital is the equity that backs each institution and drives its
    // level. It is seeded here and afterwards only moves through retained profit
    // and loan losses; it is never derived from the cash the institution holds.
    // It must be set before syncBankLevels(), which reads it.
    financialCapital[BANK] = Money(centralBank) * levelCapacity;
    financialCapital[INDUSTRIAL_BANK] = Money(industrialBank) * levelCapacity;
    financialCapital[SAVINGS_BANK] = Money(savingsBank) * levelCapacity;
    financialCapital[FINANCE] = Money(finance) * levelCapacity;
    baselineFinanceLevel = finance;
    setFinanceLevelFromSecurities(0);
    syncBankLevels(savingsPool);
    // The central and commercial banks and the savings bank are public
    // institutions held by the government. The financial district keeps its own
    // ownership, which setFinanceLevelFromSecurities() established above.
    for (const int type : {BANK, INDUSTRIAL_BANK, SAVINGS_BANK}) {
        ownedBuildings[type].fill(0);
        ownedBuildings[type][OWNER_GOVERNMENT] = buildingCounts[type];
    }
    return buildingCounts[BANK] == centralBank &&
           buildingCounts[FINANCE] == finance &&
           buildingCounts[INDUSTRIAL_BANK] == industrialBank &&
           buildingCounts[SAVINGS_BANK] == savingsBank;
}

void BuildingManager::setBankLevelFromPool(int typeIdx, Money pool) {
    pool = std::max(Money(0), pool);
    const int level = pool <= Money(0)
        ? 0
        : static_cast<int>(std::ceil(
              (pool / Money(BANK_LOAN_CAPACITY_PER_LEVEL)).toDouble()));
    buildingCounts[typeIdx] = level;
    // Ownership is deliberately NOT reset here. Resynchronising a level every
    // cycle used to clear the owner list, which left the institutions unowned
    // and made their profit distribution a no-op.
}

void BuildingManager::syncBankLevels(Money savingsPool) {
    // The central bank and the commercial bank scale with their own capital, so
    // the level-based credit limit is an independent constraint on lending.
    setBankLevelFromPool(BANK, financialCapital[BANK]);
    setBankLevelFromPool(INDUSTRIAL_BANK, financialCapital[INDUSTRIAL_BANK]);
    // The savings bank's scale follows the savings it intermediates.
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
