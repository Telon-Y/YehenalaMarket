// ==================== building_manager_ownership.cpp ====================
// 产权转移、拆除、残留辅助
#include "building_manager.h"
#include <algorithm>
#include <cmath>

bool BuildingManager::canDemolish(int typeIdx, int stepCount) const {
    (void)stepCount;
    // Government (player) orders take effect immediately. Private levels are
    // never eligible for this command; autonomous bankruptcy is handled by
    // checkDecay separately.
    return typeIdx >= 0 && typeIdx < TYPE_COUNT && buildingCounts[typeIdx] > 0 &&
           !templates[typeIdx].isFinancial &&
           ownedBuildings[typeIdx][OWNER_GOVERNMENT] > 0;
}

int BuildingManager::demolishBuildings(int typeIdx, int count, int stepCount,
                                       Money& investmentPool) {
    if (!canDemolish(typeIdx, stepCount)) return 0;
    const int actual = std::min({count, buildingCounts[typeIdx],
                                ownedBuildings[typeIdx][OWNER_GOVERNMENT]});
    if (actual <= 0) return 0;

    Money totalCash = cashPools[typeIdx];
    Money cashPerLevel = Money(0);
    if (buildingCounts[typeIdx] > 0 && totalCash > Money(0)) {
        cashPerLevel = totalCash / Money(buildingCounts[typeIdx]);
    }
    const Money cashTrans = cashPerLevel * Money(actual);
    cashPools[typeIdx] -= cashTrans;
    investmentPool += cashTrans;
    ownedBuildings[typeIdx][OWNER_GOVERNMENT] -= actual;
    buildingCounts[typeIdx] -= actual;
    lastDemolishStep[typeIdx] = stepCount;

    clampCash(typeIdx);
    clampCash(FINANCE);
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);

    if (buildingCounts[typeIdx] == 0) {
        cleanupDeadBuilding(typeIdx, investmentPool);
    }
    return actual;
}

Money BuildingManager::transferOwnership(int typeIdx, int count, OwnerType from, OwnerType to,
                                         Money& investmentPool, std::array<Money, CLASS_COUNT>& classCash) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || count <= 0 || from == to) return Money(0);

    count = std::min(count, ownedBuildings[typeIdx][from]);
    if (count == 0) return Money(0);

    Money unitPrice = Money(500000.0);
    if (buildingCounts[typeIdx] > 0 && cashPools[typeIdx] > Money(0)) {
        unitPrice = cashPools[typeIdx] / Money(buildingCounts[typeIdx]);
    }
    Money totalPrice = unitPrice * Money(count);

    if (to == OWNER_FINANCE) {
        if (cashPools[FINANCE] < totalPrice) return Money(0);
        cashPools[FINANCE] -= totalPrice;
        clampCash(FINANCE);
    } else if (to == OWNER_GOVERNMENT) {
        if (investmentPool < totalPrice) return Money(0);
        investmentPool -= totalPrice;
    } else if (to == OWNER_INITIAL) {
        if (classCash[CAPITALIST] < totalPrice) return Money(0);
        classCash[CAPITALIST] -= totalPrice;
        classCash[CAPITALIST] = clamp(classCash[CAPITALIST], -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    }

    if (from == OWNER_GOVERNMENT) {
        investmentPool += totalPrice;
    } else if (from == OWNER_INITIAL) {
        classCash[CAPITALIST] += totalPrice;
        classCash[CAPITALIST] = clamp(classCash[CAPITALIST], -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    } else if (from == OWNER_FINANCE) {
        cashPools[FINANCE] += totalPrice;
        clampCash(FINANCE);
    }

    ownedBuildings[typeIdx][from] -= count;
    ownedBuildings[typeIdx][to]   += count;
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    return totalPrice;
}

void BuildingManager::setFinanceLevelFromSecurities(int level) {
    level = std::clamp(std::max(level, baselineFinanceLevel),
                       0, maxFinances);
    buildingCounts[FINANCE] = level;
    ownedBuildings[FINANCE].fill(0);
    ownedBuildings[FINANCE][OWNER_FINANCE] = level;
    if (level == 0 && !isfinite(cashPools[FINANCE])) cashPools[FINANCE] = Money(0);
}
