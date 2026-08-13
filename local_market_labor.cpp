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

    double totalIdeal = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) {
            idealEmployment[t] = 0.0;
            continue;
        }
        const auto& bt = bld.getTemplates()[t];
        double empRatio = bt.isDevelopment() ? 1.0 : bld.getEmploymentRatio()[t];
        idealEmployment[t] = bld.getBuildingCounts()[t] * bt.laborPerUnit * empRatio;
        totalIdeal += idealEmployment[t];
    }
    laborShortage = totalIdeal > maxLabor;

    // 开发建筑不参与劳动力竞争，始终先锁定满员；普通建筑分配剩余劳动力。
    std::array<double, TYPE_COUNT> ordinaryDemand{};
    double developmentEmployment = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getTemplates()[t].isDevelopment()) {
            actualEmployment[t] = idealEmployment[t];
            actualEmploymentRate[t] = idealEmployment[t] > 0.0 ? 1.0 : 0.0;
            developmentEmployment += idealEmployment[t];
        } else {
            ordinaryDemand[t] = idealEmployment[t];
        }
    }
    double laborPool = std::max(0.0, maxLabor - developmentEmployment);
    auto ordinaryEmployment = allocateLaborByWage(
        ordinaryDemand, buildingWages, laborPool);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getTemplates()[t].isDevelopment()) continue;
        actualEmployment[t] = ordinaryEmployment[t];
        actualEmploymentRate[t] = idealEmployment[t] > 0.0
            ? actualEmployment[t] / idealEmployment[t] : 0.0;
    }

    double employedTotal = std::accumulate(
        actualEmployment.begin(), actualEmployment.end(), 0.0);
    laborPool = std::max(0.0, maxLabor - employedTotal);

    // ===== 阶级人数统计 =====
    // 劳工阶级 = 就业劳工 + 失业劳动力（含自给农场人口） + 依赖人口
    // 工程师和资本家仅来自就业人口
    auto employedClasses = calculateEmployedClasses(
        bld.getTemplates(), actualEmployment);
    totalLaborers = employedClasses[LABORER] + laborPool + dependentPopulation;
    totalEngineers = employedClasses[ENGINEER];
    totalCapitalists = employedClasses[CAPITALIST];

    double unemployedLabor = laborPool;           // 所有未就业劳动力
    if (unemployedLabor < 0.0) unemployedLabor = 0.0;

    double maxSubsistence = (double)subsistenceFarms * 5000.0;
    subsistencePop = std::min(unemployedLabor, maxSubsistence);
    if (subsistencePop < 0.0) subsistencePop = 0.0;
}

void LocalMarket::processPopulationGrowth() {
    constexpr double MAX_52W_GROWTH = 0.05;
    constexpr double MIN_52W_GROWTH = -0.20;
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
