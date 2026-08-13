// ==================== local_market_supply.cpp ====================
// 供给比率与生产
#include "local_market.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>

using namespace std;

void LocalMarket::processSupplyRatios(std::array<double, TYPE_COUNT>& activityRate,
                                      std::array<double, TYPE_COUNT>& supplyRatio,
                                      std::array<Money, NUM_GOODS>& potentialIn) {
    potentialIn.fill(Money(0));

    // Allocate scarce operating credit before production. A building may only
    // employ workers and consume inputs that its cash plus unused credit can
    // cover this week. This prevents ordinary operating costs from creating
    // unbounded implicit debt in a negative cash pool.
    Money sharedBankCash = std::max(Money(0), bld.getCashPools()[INDUSTRIAL_BANK]);
    Money regulatoryCredit = std::max(
        Money(0),
        initialTotalMoneySupply * Money(BANK_MAX_SYSTEM_CREDIT_RATIO) - totalDebt);

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) { activityRate[t] = 0.0; continue; }
        const auto& bt = bld.getTemplates()[t];

        double baseActivity = bt.isFinancial
            ? actualEmploymentRate[t]
            : bt.outputRate * actualEmploymentRate[t];
        double fundingRate = 1.0;
        if (t != CONST_DEPT) {
            Money wageCost = Money(actualEmployment[t]) * buildingWages[t];
            Money weeklyCost = bt.isDevelopment() ? Money(0) : wageCost;
            for (int g = 0; g < NUM_GOODS; ++g) {
                weeklyCost += Money(bld.getBuildingCounts()[t]) * Money(baseActivity) *
                              Money(bt.inputs[g]) * priceState.prices[g];
            }

            Money funding = (t == SAVINGS_BANK)
                ? std::max(Money(0), investmentPool)
                : std::max(Money(0), bld.getCashPools()[t]);
            if (bt.isFinancial)
                funding = std::max(Money(0), funding - wageCost);
            const bool canBorrow = !bt.isFinancial &&
                                   loanDelinquentWeeks[t] == 0;
            if (canBorrow && weeklyCost > funding) {
                int unusedLoanSlots = std::max(
                    0, bld.getBuildingCounts()[t] - buildingLoanCount[t]);
                int neededUnits = static_cast<int>(std::ceil(
                    ((weeklyCost - funding) / Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                int bankUnits = static_cast<int>(std::floor(
                    (sharedBankCash / Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                int regulatoryUnits = static_cast<int>(std::floor(
                    (regulatoryCredit / Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                int grantedUnits = std::max(
                    0, std::min({unusedLoanSlots, neededUnits, bankUnits, regulatoryUnits}));
                Money granted = Money(grantedUnits * BANK_LOAN_UNIT_VALUE);
                funding += granted;
                sharedBankCash -= granted;
                regulatoryCredit -= granted;
            }
            if (weeklyCost > Money(0)) {
                fundingRate = std::clamp(
                    (funding / weeklyCost).toDouble(), 0.0, 1.0);
            }
        }
        if (!bt.isDevelopment()) {
            actualEmployment[t] *= fundingRate;
            actualEmploymentRate[t] *= fundingRate;
        }
        activityRate[t] = baseActivity * fundingRate;

        for (int g = 0; g < NUM_GOODS; ++g)
            potentialIn[g] += Money(bld.getBuildingCounts()[t]) *
                              Money(bt.inputs[g]) * Money(activityRate[t]);
    }

    double employedTotal = 0.0;
    for (double employed : actualEmployment) employedTotal += employed;
    double unemployedLabor = std::max(0.0, maxLabor - employedTotal);
    auto employedClasses = calculateEmployedClasses(
        bld.getTemplates(), actualEmployment);
    totalLaborers = employedClasses[LABORER] + unemployedLabor + dependentPopulation;
    totalEngineers = employedClasses[ENGINEER];
    totalCapitalists = employedClasses[CAPITALIST];
    subsistencePop = std::min(unemployedLabor,
                              static_cast<double>(subsistenceFarms) * 5000.0);

    supplyRatio.fill(1.0);
    const double damping = 0.5;          // 阻尼因子
    const int maxIter = 200;             // 最大迭代次数
    const double tolerance = 1e-9;       // 收敛阈值

    for (int iter = 0; iter < maxIter; ++iter) {
        // 根据当前 supplyRatio 计算总产出和总投入
        std::array<Money, NUM_GOODS> tempOut{}, tempIn{};
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            double cr = activityRate[t] * supplyRatio[t];
            if (!bt.isFinancial && bt.outputGood >= 0)
                tempOut[bt.outputGood] += Money(bld.getBuildingCounts()[t]) * Money(cr);
            for (int g = 0; g < NUM_GOODS; ++g)
                tempIn[g] += Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
        }

        // 计算每种商品的供需比（产出 / 投入）
        std::array<Money, NUM_GOODS> ratio;
        for (int g = 0; g < NUM_GOODS; ++g) {
            ratio[g] = (tempIn[g] > Money(0) && tempOut[g] < tempIn[g]) ?
                       tempOut[g] / tempIn[g] : Money(1);
        }

        // 更新每个建筑的新供给比率，带阻尼
        std::array<double, TYPE_COUNT> newRatio;
        newRatio.fill(1.0);
        double maxDiff = 0.0;
        for (int t = 0; t < TYPE_COUNT; ++t) {
            if (bld.getBuildingCounts()[t] == 0) continue;
            const auto& bt = bld.getTemplates()[t];
            bool hasInput = false;
            double minR = 1.0;
            for (int g = 0; g < NUM_GOODS; ++g) {
                if (bt.inputs[g] > 0) {
                    hasInput = true;
                    minR = std::min(minR, ratio[g].toDouble());
                }
            }
            if (hasInput) {
                // 阻尼更新：向目标 minR 移动一部分
                newRatio[t] = supplyRatio[t] * (1.0 - damping) + minR * damping;
            } else {
                newRatio[t] = 1.0; // 无原料需求，不受限
            }
            double diff = std::fabs(newRatio[t] - supplyRatio[t]);
            if (diff > maxDiff) maxDiff = diff;
        }

        supplyRatio = newRatio;
        if (maxDiff < tolerance) break;
    }

    // 确保不小于0.2（基础开工率）
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) supplyRatio[t] = 1.0;
        else supplyRatio[t] = std::clamp(supplyRatio[t], 0.0, 1.0);
    }
    bld.setCurrentSupplyRatio(supplyRatio);
    for (int t = 0; t < TYPE_COUNT; ++t) activityRate[t] *= supplyRatio[t];
}

void LocalMarket::processProduction(const std::array<double, TYPE_COUNT>& activityRate,
                                    std::array<Money, NUM_GOODS>& formalOut,
                                    std::array<Money, NUM_GOODS>& realOut,
                                    std::array<Money, NUM_GOODS>& realIn,
                                    std::array<Money, TYPE_COUNT>& buildingOutput) {
    formalOut.fill(Money(0));
    realOut.fill(Money(0));
    realIn.fill(Money(0));
    buildingOutput.fill(Money(0));

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        const auto& bt = bld.getTemplates()[t];
        double cr = activityRate[t];
        if (!bt.isFinancial && bt.outputGood >= 0) {
            Money outAmt = Money(bld.getBuildingCounts()[t]) * Money(cr);
            formalOut[bt.outputGood] += outAmt;
            realOut[bt.outputGood] += outAmt;
            buildingOutput[t] = outAmt;
        }
        for (int g = 0; g < NUM_GOODS; ++g)
            realIn[g] += Money(bld.getBuildingCounts()[t]) * Money(bt.inputs[g]) * Money(cr);
    }
}
