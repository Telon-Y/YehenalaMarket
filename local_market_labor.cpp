// ==================== local_market_labor.cpp ====================
// 劳动力分配与人口增长
#include "local_market.h"
#include "local_market_internal.h"
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace std;

void LocalMarket::processLaborAllocation(std::array<double, TYPE_COUNT>& idealEmployment,
                                         bool& laborShortage) {
    auto inQueueCount = bld.getInQueueCounts();
    int usedFarms = bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]
                    + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int idleLand = max(0, 10000 - usedFarms);
    subsistenceFarms = idleLand;

    maxLabor = laborPopulation * LABOR_FORCE_PARTICIPATION;
    dependentPopulation = laborPopulation - maxLabor;

    double totalRampedTarget = 0.0;
    std::array<double, TYPE_COUNT> developmentIdeal{};
    std::array<double, TYPE_COUNT> ordinaryIdeal{};
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) {
            idealEmployment[t] = 0.0;
            targetEmployment[t] = 0.0;
            actualEmployment[t] = 0.0;
            continue;
        }
        const auto& bt = bld.getTemplates()[t];
        // Development is a public staffing obligation.  Its target is always
        // the full workforce, independent of the ordinary sector's adaptive
        // employment ratio.
        const bool isDevelopment = bt.isDevelopment();
        const double empRatio = isDevelopment
            ? 1.0
            : bld.getEmploymentRatio()[t];
        // Ordinary production targets are demand-driven.  Labor is hired
        // against the current production command so wage cost and staffing do
        // not remain pinned to full installed capacity when output is low.
        double orderRatio = 1.0;
        if (!isDevelopment && !bt.isFinancial && bt.outputGood >= 0 &&
            logisticsNetwork != nullptr) {
            const Money command = logisticsNetwork->productionCommand(
                marketId, t, bt.outputGood);
            const Money fullOutput = Money(bld.getBuildingCounts()[t]) *
                Money(bt.outputRate) * Money(empRatio);
            if (fullOutput > Money(0)) {
                orderRatio = std::clamp(
                    (command / fullOutput).toDouble(), 0.0, 1.0);
            } else {
                orderRatio = 0.0;
            }
        }
        const double fullEmployment =
            bld.getBuildingCounts()[t] * bt.laborPerUnit;
        idealEmployment[t] =
            fullEmployment * empRatio * orderRatio;
        targetEmployment[t] = idealEmployment[t];
        const double current = std::clamp(
            actualEmployment[t], 0.0, fullEmployment);
        double ramped = current;
        if (idealEmployment[t] > current) {
            ramped = std::min(
                idealEmployment[t],
                current + fullEmployment *
                    EMPLOYMENT_WEEKLY_HIRE_CAPACITY_SHARE);
        } else if (idealEmployment[t] < current) {
            ramped = std::max(
                idealEmployment[t],
                current * (1.0 -
                    EMPLOYMENT_WEEKLY_LAYOFF_CURRENT_SHARE));
        }
        totalRampedTarget += ramped;
        if (isDevelopment)
            developmentIdeal[t] = ramped;
        else
            ordinaryIdeal[t] = ramped;
    }
    std::array<Money, TYPE_COUNT> effectiveWages = buildingWages;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] <= 0) {
            buildingBonuses[t] = Money(0);
            continue;
        }
        const double desired = std::max(0.0, idealEmployment[t]);
        const double current = std::max(0.0, actualEmployment[t]);
        const double gap = desired > 1.0e-9
            ? std::clamp((desired - current) / desired, 0.0, 1.0)
            : 0.0;
        const double targetBonus = buildingWages[t].toDouble() *
            std::min(MAX_BONUS_TO_BASE_WAGE,
                     gap * (1.0 + PRODUCTION_BONUS_GAP_PREMIUM));
        buildingBonuses[t] = buildingBonuses[t] *
            Money(1.0 - PRODUCTION_BONUS_SMOOTHING) +
            Money(targetBonus * PRODUCTION_BONUS_SMOOTHING);
        buildingBonuses[t] = clamp(buildingBonuses[t], Money(0),
                                   buildingWages[t] * Money(MAX_BONUS_TO_BASE_WAGE));
        effectiveWages[t] += buildingBonuses[t];
    }
    laborShortage = totalRampedTarget > maxLabor;

    // Development buildings have first claim on available labor.
    // This keeps their public/infrastructure staffing obligation at the
    // highest possible rate before ordinary production competes for the remainder.
    actualEmployment = allocateLaborByWage(
        developmentIdeal, effectiveWages, maxLabor);
    double developmentEmployment = 0.0;
    for (double employed : actualEmployment)
        developmentEmployment += employed;
    const double ordinaryLabor =
        std::max(0.0, maxLabor - developmentEmployment);
    const auto ordinaryEmployment = allocateLaborByWage(
        ordinaryIdeal, effectiveWages, ordinaryLabor);
    for (int t = 0; t < TYPE_COUNT; ++t)
        actualEmployment[t] += ordinaryEmployment[t];
    for (int t = 0; t < TYPE_COUNT; ++t) {
        const double fullEmployment =
            bld.getBuildingCounts()[t] *
            bld.getTemplates()[t].laborPerUnit;
        actualEmploymentRate[t] = fullEmployment > 0.0
            ? std::clamp(actualEmployment[t] / fullEmployment, 0.0, 1.0)
            : 0.0;
    }

    double employedTotal = std::accumulate(
        actualEmployment.begin(), actualEmployment.end(), 0.0);
    double laborPool = std::max(0.0, maxLabor - employedTotal);

    // ===== 阶级人数统计 =====
    // 劳工阶级 = 就业劳工 + 失业劳动力（含自给农场人口） + 依赖人口
    // 工程师和资本家仅来自就业人口
    auto employedClasses = calculateEmployedClasses(
        bld.getTemplates(), actualEmployment);
    totalLaborers = employedClasses[LABORER] + laborPool + dependentPopulation;
    totalEngineers = employedClasses[ENGINEER];
    totalCapitalists = employedClasses[CAPITALIST];

    const double unemployedLabor = std::max(0.0, laborPool);
    const double maxSubsistencePopulation =
        static_cast<double>(subsistenceFarms) * 5000.0;
    subsistencePop = std::min(unemployedLabor, maxSubsistencePopulation);
}

void LocalMarket::processPopulationGrowth() {
    constexpr double MAX_52W_GROWTH = 0.05;
    constexpr double MIN_52W_GROWTH = -0.05;
    constexpr double SATISFACTION_NEUTRAL = 0.75;
    constexpr double WEEKS_PER_YEAR = 52.0;

    double growthRate52;
    if (satisfaction >= SATISFACTION_NEUTRAL) {
        double t = (satisfaction - SATISFACTION_NEUTRAL) / (1.0 - SATISFACTION_NEUTRAL);
        growthRate52 = t * MAX_52W_GROWTH;
    } else {
        double t = (SATISFACTION_NEUTRAL - satisfaction) / SATISFACTION_NEUTRAL;
        growthRate52 = -t * std::abs(MIN_52W_GROWTH);
    }

    // ===== 修复：把年增长率等价转化为周增长率（复合增长） =====
    // 线性除法（错误）：r_weekly = r_annual / 52
    // 复合转换（正确）：(1 + r_weekly)^52 = 1 + r_annual
    //                   r_weekly = (1 + r_annual)^(1/52) - 1
    double annualFactor = 1.0 + growthRate52;
    // growthRate52 取值范围为 [-0.20, 0.05]，annualFactor 始终为正
    double weeklyGrowth = std::pow(annualFactor, 1.0 / WEEKS_PER_YEAR) - 1.0;

    laborPopulation *= (1.0 + weeklyGrowth);
    if (laborPopulation < 1000.0) laborPopulation = 1000.0;

    maxLabor = laborPopulation * LABOR_FORCE_PARTICIPATION;
    dependentPopulation = laborPopulation - maxLabor;
}
