// ==================== local_market_consumption.cpp ====================
// Consumption and wages.
#include "local_market.h"
#include "country.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>

using namespace std;

void LocalMarket::processConsumption(const std::array<Money, NUM_GOODS>& realOut,
                                     const std::array<Money, NUM_GOODS>& realIn,
                                     std::array<Money, NUM_GOODS>& consumerTarget,
                                     std::array<Money, NUM_GOODS>& consumerPlanned,
                                     std::array<Money, NUM_GOODS>& consumerActual,
                                     std::array<Money, NUM_GOODS>& consumerSpending) {
    (void)realOut;
    (void)realIn;
    constexpr double popScale = 1.0 / 100000.0;
    std::array<double, GROUP_COUNT> urbanGroupDemand{};
    for (int g = 0; g < GROUP_COUNT; ++g) {
        urbanGroupDemand[g] = demandTable[0][g] * totalLaborers * popScale
                            + demandTable[1][g] * totalEngineers * popScale
                            + demandTable[2][g] * totalCapitalists * popScale;
    }
    const double subsistenceFood =
        demandTable[0][GRP_BASIC_FOOD] * subsistencePop * popScale;
    urbanGroupDemand[GRP_BASIC_FOOD] = std::max(
        0.0, urbanGroupDemand[GRP_BASIC_FOOD] - subsistenceFood);

    Money totalClassFund = classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST];
    double wealthPerCap = totalClassFund.toDouble() / max(laborPopulation, 1.0);
    double luxuryFactor = clamp(wealthPerCap / 1500.0, 0.0, 3.0);

    const double alphaSmooth = 0.2;
    smoothedLuxuryFactor = smoothedLuxuryFactor + alphaSmooth * (luxuryFactor - smoothedLuxuryFactor);
    luxuryFactor = smoothedLuxuryFactor;

    urbanGroupDemand[GRP_STANDARD_CLOTHES] *= (1.0 + luxuryFactor * 0.4);
    urbanGroupDemand[GRP_HOUSING] += laborPopulation * popScale * luxuryFactor * 1.5;

    std::array<Money, NUM_GOODS> rawConsumerTarget{};
    rawConsumerTarget.fill(Money(0));
    for (int g = 0; g < GROUP_COUNT; ++g) {
        double remain = urbanGroupDemand[g];
        if (remain <= 1e-9) continue;
        vector<int> goods = groupGoods[g];

        vector<pair<int, double>> candidates;
        double totalWeight = 0.0;
        for (int good : goods) {
            double u = valueCoeff[good][g];
            if (u <= 0.0 || priceState.prices[good] <= Money(1e-9)) continue;
            double weight = u / priceState.prices[good].toDouble();
            candidates.push_back({good, weight});
            totalWeight += weight;
        }
        if (totalWeight <= 1e-12 || candidates.empty()) continue;

        for (const auto& [good, weight] : candidates) {
            double u = valueCoeff[good][g];
            double share = weight / totalWeight;
            double want = share * remain / u;
            rawConsumerTarget[good] += Money(want);
        }
    }

    for (int good = 0; good < NUM_GOODS; ++good) {
        latestRawConsumerTarget[good] = rawConsumerTarget[good];
        // The production-facing demand policy is the exact mean of the most
        // recent 52 observed weeks. During startup it uses all observed weeks
        // rather than treating future, unobserved weeks as zero demand.
        smoothedConsumerDemand[good] =
            consumerDemandFilters[good].update(
                rawConsumerTarget[good], 1.0);
        consumerTarget[good] = smoothedConsumerDemand[good];
    }

    Money urbanBudget = clamp(classCash[LABORER] + classCash[ENGINEER] + classCash[CAPITALIST],
                              Money(0), CLASS_CASH_MAX_MONEY);
    Money idealCost = Money(0);
    for (int i = 0; i < NUM_GOODS; ++i)
        idealCost += consumerTarget[i] * priceState.prices[i];
    Money actualCost = std::min(urbanBudget, idealCost);
    Money scale = (idealCost > Money(1e-6)) ? (actualCost / idealCost) : Money(1);

    consumerActual.fill(Money(0));
    consumerPlanned.fill(Money(0));
    consumerSpending.fill(Money(0));
    for (int i = 0; i < NUM_GOODS; ++i) {
        consumerPlanned[i] = consumerTarget[i] * scale;
        consumerActual[i] = std::min(
            consumerPlanned[i], warehouse.stock(i).available());
        consumerSpending[i] = consumerActual[i] * priceState.prices[i];
    }

    latestConsumerTarget = consumerTarget;
    latestPlannedConsumerDemand = consumerPlanned;
}

void LocalMarket::processWagePayment(std::array<Money, TYPE_COUNT>& laborCostByBuilding) {
    laborCostByBuilding.fill(Money(0));
    std::array<Money, CLASS_COUNT> wageIncome;
    wageIncome.fill(Money(0));

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (actualEmployment[t] <= 0) continue;
        Money lc = Money(actualEmployment[t]) * (buildingWages[t] + buildingBonuses[t]);
        Money paid = lc;
        if (bld.getTemplates()[t].isDevelopment()) {
            const int levels = std::max(0, bld.getBuildingCounts()[t]);
            const int governmentLevels = std::clamp(
                bld.getOwnedBuildings()[t][OWNER_GOVERNMENT], 0, levels);
            const Money governmentWages = levels > 0
                ? lc * Money(governmentLevels) / Money(levels)
                : Money(0);
            const Money privateWages =
                std::max(Money(0), lc - governmentWages);
            if (privateWages > Money(0))
                bld.addCash(t, -privateWages);

            if (fiscalCountry == nullptr) {
                paid = privateWages + bld.payDevelopmentWages(
                    t, governmentWages, playerCash, investmentPool,
                    classCash);
            } else {
                // Only the government-owned share is paid by the country.
                // Reserved construction funds remain unavailable to payroll;
                // privately owned infrastructure pays from building cash.
                Money governmentCash = fiscalCountry->getAvailableTreasury();
                Money governmentPaid = bld.payDevelopmentWages(
                    t, governmentWages, governmentCash, investmentPool,
                    classCash);
                if (governmentPaid > Money(0) &&
                    !fiscalCountry->spendTreasury(governmentPaid)) {
                    governmentPaid = Money(0);
                }
                paid = privateWages + governmentPaid;
            }
        } else {
            bld.addCash(t, -lc);
        }
        // Profit statements and class income must reflect cash that actually
        // left the government pool, rather than an unfunded payroll request.
        laborCostByBuilding[t] = paid;
        const auto& shares = bld.getTemplates()[t].workforceShares;
        for (int c = 0; c < CLASS_COUNT; ++c)
            wageIncome[c] += paid * Money(shares[c]);
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        classCash[c] += wageIncome[c];
        clampMoney(classCash[c]);
    }
}
