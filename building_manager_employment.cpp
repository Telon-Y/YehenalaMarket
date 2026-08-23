// ==================== building_manager_employment.cpp ====================
// 就业率与利润率平滑
#include "building_manager.h"
#include <algorithm>
#include <cmath>

void BuildingManager::updateActualProfitRates(const std::array<double, TYPE_COUNT>& actualRates) {
    std::array<double, TYPE_COUNT> safeRates = actualRates;
    for (int t = 0; t < TYPE_COUNT; ++t)
        if (!std::isfinite(safeRates[t])) safeRates[t] = 0.0;

    avgProfitRates = safeRates;
    if (!profitInitialized) {
        smoothedProfitRate = safeRates;
        profitInitialized = true;
    } else {
        double alpha = 0.2;
        for (int t = 0; t < TYPE_COUNT; ++t)
            smoothedProfitRate[t] = smoothedProfitRate[t] * (1.0 - alpha) + safeRates[t] * alpha;
    }
}

void BuildingManager::updateActualUnitProfits(
    const std::array<Money, TYPE_COUNT>& actualProfits) {
    actualUnitProfits = actualProfits;
    for (Money& profit : actualUnitProfits)
        if (!isfinite(profit)) profit = Money(0);
}

void BuildingManager::adjustEmployment() {
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        if (templates[t].isDevelopment()) {
            employmentRatio[t] = 1.0;
            continue;
        }
        if (!std::isfinite(smoothedProfitRate[t])) smoothedProfitRate[t] = 0.0;
        if (!std::isfinite(employmentRatio[t])) employmentRatio[t] = 1.0;

        if (smoothedProfitRate[t] < 0.0) {
            employmentRatio[t] *= 0.80;
        } else {
            employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.30;
        }
        double minimumOperatingRatio = buildingCounts[t] == 1 ? 0.25 : 0.0;
        employmentRatio[t] = std::clamp(employmentRatio[t], minimumOperatingRatio, 1.0);
    }
}
