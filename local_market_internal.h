// ==================== local_market_internal.h ====================
// LocalMarket 多实现文件共享的内部辅助函数
#pragma once
#include "local_market.h"
#include <numeric>

inline void clampMoney(Money& x) {
    if (!isfinite(x)) x = Money(0);
}

inline std::array<double, CLASS_COUNT> calculateEmployedClasses(
    const std::vector<BuildingTemplate>& templates,
    const std::array<double, TYPE_COUNT>& employment) {
    std::array<double, CLASS_COUNT> classes{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        for (int c = 0; c < CLASS_COUNT; ++c)
            classes[c] += employment[t] * templates[t].workforceShares[c];
    }
    return classes;
}

inline std::array<double, TYPE_COUNT> allocateLaborByWage(
    const std::array<double, TYPE_COUNT>& idealEmployment,
    const std::array<Money, TYPE_COUNT>& wages,
    double availableLabor) {
    std::array<double, TYPE_COUNT> allocated{};
    double laborPool = std::max(0.0, availableLabor);

    for (int iteration = 0; iteration < TYPE_COUNT && laborPool > 1e-6; ++iteration) {
        double totalWeight = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            double unmet = idealEmployment[t] - allocated[t];
            if (unmet > 1e-9)
                totalWeight += unmet * std::max(0.1, wages[t].toDouble());
        }
        if (totalWeight <= 1e-12) break;

        double roundPool = laborPool;
        double assignedThisRound = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            double unmet = idealEmployment[t] - allocated[t];
            if (unmet <= 1e-9) continue;
            double weight = unmet * std::max(0.1, wages[t].toDouble());
            double assign = std::min(unmet, roundPool * weight / totalWeight);
            allocated[t] += assign;
            assignedThisRound += assign;
        }
        laborPool -= assignedThisRound;
        if (assignedThisRound <= 1e-9) break;
    }

    if (laborPool > 1e-6) {
        std::array<int, TYPE_COUNT> order{};
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            return wages[a] > wages[b];
        });
        for (int t : order) {
            double assign = std::min(laborPool, idealEmployment[t] - allocated[t]);
            if (assign <= 0.0) continue;
            allocated[t] += assign;
            laborPool -= assign;
            if (laborPool <= 1e-6) break;
        }
    }
    return allocated;
}
