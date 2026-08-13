// ==================== building_manager_decay.cpp ====================
// 建筑衰减逻辑
#include "building_manager.h"
#include <algorithm>
#include <cmath>

void BuildingManager::checkDecay(int stepCount, Money& investmentPool,
                                 std::array<Money, CLASS_COUNT>& classCash) {
    (void)classCash;
    if (stepCount <= 52) {
        resetDecayCounters();
        return;
    }
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (templates[t].isDevelopment()) {
            consecutiveLowEmpWeeks[t] = 0;
            continue;
        }
        if (buildingCounts[t] == 0) {
            consecutiveLowEmpWeeks[t] = 0;
            continue;
        }
        // A cyclical dip in one signal is not bankruptcy. Require all three
        // independent distress signals before removing productive capacity.
        bool distressed = employmentRatio[t] <= 0.50 &&
                          cashPools[t] < Money(0) &&
                          smoothedProfitRate[t] < -0.10;
        if (distressed) {
            consecutiveLowEmpWeeks[t]++;
        } else {
            consecutiveLowEmpWeeks[t] = 0;
        }

        if (consecutiveLowEmpWeeks[t] >= 260) {
            // Automated decay must not turn an operating industry into an
            // absorbing zero-capacity state. In particular, construction
            // cannot rebuild itself once its last level has disappeared.
            constexpr int MIN_AUTOMATED_LEVELS = 1;
            int reducible = buildingCounts[t] - MIN_AUTOMATED_LEVELS;
            if (reducible <= 0) {
                consecutiveLowEmpWeeks[t] = 0;
                continue;
            }
            int reduce = std::min(reducible,
                                  std::max(1, (int)ceil(buildingCounts[t] * 0.05)));
            if (onBuildingsRemoving) onBuildingsRemoving(t, reduce);
            Money totalCash = cashPools[t];
            Money cashPerLevel = Money(0);
            if (buildingCounts[t] > 0 && totalCash > Money(0)) {
                cashPerLevel = totalCash / Money(buildingCounts[t]);
            }
            std::array<int, OWNER_COUNT> local = ownedBuildings[t];
            int remaining = reduce;
            for (int o = 0; o < OWNER_COUNT && remaining > 0; ++o) {
                int take = std::min(remaining, local[o]);
                if (take == 0) continue;
                Money cashTrans = cashPerLevel * Money(take);
                cashPools[t] -= cashTrans;
                if (o == OWNER_GOVERNMENT) {
                    investmentPool += cashTrans;
                } else if (o == OWNER_INITIAL) {
                    classCash[CAPITALIST] += cashTrans;
                } else if (o == OWNER_FINANCE) {
                    cashPools[FINANCE] += cashTrans;
                }
                local[o] -= take;
                remaining -= take;
            }
            ownedBuildings[t] = local;
            buildingCounts[t] = std::max(0, buildingCounts[t] - reduce);
            if (buildingCounts[t] == 0) {
                cleanupDeadBuilding(t, investmentPool);
            }
            consecutiveLowEmpWeeks[t] = 0;
            syncFinanceCount();
            clampCash(t);
            clampCash(FINANCE);
            if (!isfinite(investmentPool)) investmentPool = Money(0);
            investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
        }
    }
    clampAllCash();
}

void BuildingManager::resetDecayCounters() {
    consecutiveLowEmpWeeks.fill(0);
}

void BuildingManager::cleanupDeadBuilding(int typeIdx, Money& investmentPool) {
    if (buildingCounts[typeIdx] > 0) return;
    if (cashPools[typeIdx] != Money(0)) {
        investmentPool += cashPools[typeIdx];
        cashPools[typeIdx] = Money(0);
    }
    for (int o = 0; o < OWNER_COUNT; ++o) ownedBuildings[typeIdx][o] = 0;
    clampCash(typeIdx);
    if (!isfinite(investmentPool)) investmentPool = Money(0);
    investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
}
