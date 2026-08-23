// ==================== price_engine.cpp ====================
#include "price_engine.h"
#include <cmath>
#include <algorithm>

void initPriceState(PriceState& ps) {
    for (int i = 0; i < NUM_GOODS; ++i) {
        ps.prices[i] = Money(referencePrice[i]);
        ps.suppress[i] = Money(priceSuppressBase[i]);
        ps.baseRef[i] = Money(referencePrice[i]);
        ps.dynamicRef[i] = Money(referencePrice[i]);
    }
    // 黄金固定平价初始化
    ps.baseRef[GOLD_GOOD_INDEX] = Money(GOLD_FIXED_PRICE);
    ps.dynamicRef[GOLD_GOOD_INDEX] = Money(GOLD_FIXED_PRICE);
    ps.prices[GOLD_GOOD_INDEX] = Money(GOLD_FIXED_PRICE);

    ps.velocity.fill(Money(0));
    ps.mass.fill(Money(1));
    ps.priceLevel = Money(1.0);
    ps.targetPriceLevel = Money(1.0);
}

void updateReferencePrices(PriceState& ps, Money activeMoneyRatio) {
    if (!isfinite(activeMoneyRatio)) activeMoneyRatio = Money(1.0);
    activeMoneyRatio = clamp(activeMoneyRatio, Money(0.05), Money(20.0));

    ps.targetPriceLevel = activeMoneyRatio;
    ps.priceLevel += Money(0.005) * (ps.targetPriceLevel - ps.priceLevel);
    ps.priceLevel = clamp(ps.priceLevel, Money(0.05), Money(20.0));

    for (int i = 0; i < NUM_GOODS; ++i) {
        ps.dynamicRef[i] = ps.baseRef[i] * ps.priceLevel;
        if (!isfinite(ps.dynamicRef[i]) || ps.dynamicRef[i] <= Money(0))
            ps.dynamicRef[i] = ps.baseRef[i];
    }

    // 黄金动态参考价强制固定
    ps.dynamicRef[GOLD_GOOD_INDEX] = Money(GOLD_FIXED_PRICE);
}

void stepPrices(PriceState& ps,
                const std::array<Money, NUM_GOODS>& supply,
                const std::array<Money, NUM_GOODS>& demandInput,
                const std::array<Money, NUM_GOODS>& demandConsumer,
                double debtFactor,
                double dt,
                int excludedGood1,
                int excludedGood2,
                int excludedGood3) {
    const double oversupplyGain = 1.5;
    constexpr double PRICE_MAX_MULTIPLE = 20.0;
    constexpr double PRICE_MIN_MULTIPLE = 0.05;

    for (int i = 0; i < NUM_GOODS; ++i) {
        if (i == excludedGood1 || i == excludedGood2 || i == excludedGood3) continue;

        // 过剩需求：正值为超额需求，负值为过剩供给（负值放大惩罚）
        double rawExcess = (demandInput[i] + demandConsumer[i] - supply[i]).toDouble();
        double excessDemand = (rawExcess >= 0) ? rawExcess : rawExcess * oversupplyGain;

        // 市场总流量（用于惯性标定）
        double G = fabs(supply[i].toDouble())
                 + fabs(demandInput[i].toDouble())
                 + fabs(demandConsumer[i].toDouble()) + 1.0;
        ps.mass[i] = Money(ps.inertiaCoeff * G);

        Money dynamicB = ps.suppress[i] * Money(debtFactor);
        Money restoring = dynamicB * (ps.prices[i] - ps.dynamicRef[i]);
        Money rho = Money(ps.dampRatio * 2.0 * sqrt(ps.mass[i].toDouble() * dynamicB.toDouble()));

        Money acc = (Money(excessDemand) - rho * ps.velocity[i] - restoring) / ps.mass[i];
        ps.velocity[i] += acc * Money(dt);
        ps.prices[i] += ps.velocity[i] * Money(dt);

        Money lower = ps.dynamicRef[i] * Money(PRICE_MIN_MULTIPLE);
        Money upper = ps.dynamicRef[i] * Money(PRICE_MAX_MULTIPLE);
        Money unclamped = ps.prices[i];
        ps.prices[i] = clamp(unclamped, lower, upper);
        if ((ps.prices[i] == lower && ps.velocity[i] < Money(0)) ||
            (ps.prices[i] == upper && ps.velocity[i] > Money(0)))
            ps.velocity[i] = Money(0);
        if (!isfinite(ps.prices[i]) || ps.prices[i] <= Money(0))
            ps.prices[i] = ps.dynamicRef[i];
        if (ps.prices[i] < Money(0.1)) ps.prices[i] = Money(0.1);
    }
}
