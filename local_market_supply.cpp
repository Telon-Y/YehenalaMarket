// ==================== local_market_supply.cpp ====================
// Material allocation and production through warehouse buffers.
#include "local_market.h"
#include "local_market_internal.h"

#include <algorithm>
#include <cmath>

using namespace std;

void LocalMarket::processSupplyRatios(
    std::array<double, TYPE_COUNT>& activityRate,
    std::array<double, TYPE_COUNT>& supplyRatio,
    std::array<Money, NUM_GOODS>& potentialIn) {
    potentialIn.fill(Money(0));
    activityRate.fill(0.0);
    supplyRatio.fill(1.0);
    std::array<double, TYPE_COUNT> capacityUtilization{};
    std::array<double, TYPE_COUNT> fundingAvailability{};
    std::array<double, TYPE_COUNT> materialAvailability{};
    std::array<Money, TYPE_COUNT> staffedCapacity{};
    std::array<Money, TYPE_COUNT> productionTarget{};
    fundingAvailability.fill(1.0);
    materialAvailability.fill(1.0);

    Money sharedBankCash =
        std::max(Money(0), bld.getCashPools()[INDUSTRIAL_BANK]);
    Money regulatoryCredit = std::max(
        Money(0),
        moneySupplyBaseline() * Money(BANK_MAX_SYSTEM_CREDIT_RATIO) -
            totalDebt);

    for (int type = 0; type < TYPE_COUNT; ++type) {
        if (bld.getBuildingCounts()[type] == 0) {
            supplyRatio[type] = 1.0;
            continue;
        }
        const BuildingTemplate& bt = bld.getTemplates()[type];
        const double baseActivity = bt.isFinancial
            ? actualEmploymentRate[type]
            : bt.outputRate * actualEmploymentRate[type];
        const Money staffedOutput =
            Money(bld.getBuildingCounts()[type]) * Money(baseActivity);
        staffedCapacity[type] = staffedOutput;
        const Money installedOutput =
            Money(bld.getBuildingCounts()[type]) *
            Money(bt.isFinancial ? 1.0 : bt.outputRate);

        Money plannedTarget = installedOutput;
        if (type == RAILWAY) {
            plannedTarget = installedOutput;
        } else if (type == CONST_DEPT) {
            plannedTarget = constructionOutputPlanForPolicy();
        } else if (!bt.isFinancial && bt.outputGood >= 0) {
            plannedTarget = logisticsNetwork == nullptr
                ? Money(0)
                : logisticsNetwork->productionCommand(
                      marketId, type, bt.outputGood);
        }
        plannedTarget = std::clamp(
            plannedTarget, Money(0), installedOutput);
        capacityUtilization[type] = installedOutput > Money(0)
            ? std::clamp(
                  (plannedTarget / installedOutput).toDouble(), 0.0, 1.0)
            : 0.0;
        productionTarget[type] = plannedTarget;
        const Money operatingTarget =
            std::min(plannedTarget, staffedOutput);
        const double orderRatio = staffedOutput > Money(0)
            ? std::clamp(
                  (operatingTarget / staffedOutput).toDouble(), 0.0, 1.0)
            : 0.0;

        double fundingRate = 1.0;
        if (type != CONST_DEPT) {
            const Money wageCost =
                Money(actualEmployment[type]) *
                (buildingWages[type] + buildingBonuses[type]);
            // Government-owned development levels are paid from the
            // treasury in processWagePayment. Private levels must keep their
            // own building cash solvent, so only that share belongs in the
            // building's operating funding requirement.
            Money privateWageCost = wageCost;
            if (bt.isDevelopment()) {
                const int levels = std::max(0, bld.getBuildingCounts()[type]);
                const int governmentLevels = std::clamp(
                    bld.getOwnedBuildings()[type][OWNER_GOVERNMENT], 0,
                    levels);
                privateWageCost = levels > 0
                    ? wageCost * Money(levels - governmentLevels) /
                          Money(levels)
                    : Money(0);
            }
            Money weeklyCost = privateWageCost;
            for (int good = 0; good < NUM_GOODS; ++good) {
                weeklyCost += operatingTarget * Money(bt.inputs[good]) *
                              priceState.prices[good];
            }

            Money funding = type == SAVINGS_BANK
                ? std::max(Money(0), investmentPool)
                : std::max(Money(0), bld.getCashPools()[type]);
            // Government payroll is funded by the treasury; private
            // development payroll remains a building cash obligation.
            if (bt.isFinancial && !bt.isDevelopment())
                funding = std::max(Money(0), funding - wageCost);
            const bool canBorrow =
                !bt.isFinancial && loanDelinquentWeeks[type] == 0;
            if (canBorrow && weeklyCost > funding) {
                const int unusedLoanSlots = std::max(
                    0, bld.getBuildingCounts()[type] -
                           buildingLoanCount[type]);
                const int neededUnits = static_cast<int>(std::ceil(
                    ((weeklyCost - funding) /
                     Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                const int bankUnits = static_cast<int>(std::floor(
                    (sharedBankCash /
                     Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                const int regulatoryUnits = static_cast<int>(std::floor(
                    (regulatoryCredit /
                     Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
                const int grantedUnits = std::max(
                    0, std::min({unusedLoanSlots, neededUnits, bankUnits,
                                 regulatoryUnits}));
                const Money granted =
                    Money(grantedUnits * BANK_LOAN_UNIT_VALUE);
                if (granted > Money(0)) {
                    // Commit the loan at the same point where the supply
                    // ratio assumes it exists. The later borrowing pass is
                    // retained only as a safety net for other cash deficits.
                    bld.addCash(type, granted);
                    bld.addCash(INDUSTRIAL_BANK, -granted);
                    buildingLoanCount[type] += grantedUnits;
                    loanBalance[type] += granted;
                    recalculateTotalDebt();
                    funding += granted;
                    sharedBankCash -= granted;
                    regulatoryCredit = std::max(
                        Money(0), regulatoryCredit - granted);
                }
            }
            if (weeklyCost > Money(0)) {
                fundingRate = std::clamp(
                    (funding / weeklyCost).toDouble(), 0.0, 1.0);
            }
        }
        fundingAvailability[type] = fundingRate;

        const double fundedActivity = baseActivity * fundingRate;
        const Money fundedOutput =
            Money(bld.getBuildingCounts()[type]) *
            Money(fundedActivity);

        const Money plannedOutput = fundedOutput * Money(orderRatio);
        double materialRatio = 1.0;
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (bt.inputs[good] <= 0.0) continue;
            const Money required =
                plannedOutput * Money(bt.inputs[good]);
            potentialIn[good] += required;
            if (required <= Money(0)) continue;
            const InventoryState& inputState = bt.isFinancial
                ? warehouse.stock(good)
                : warehouse.buildingInput(type, good);
            const Money available = bt.isFinancial || logisticsNetwork == nullptr
                ? inputState.available()
                : logisticsNetwork->productionInputAvailability(
                      marketId, type, good);
            materialRatio = std::min(
                materialRatio,
                std::clamp(
                    (available / required).toDouble(), 0.0, 1.0));
        }

        materialAvailability[type] = materialRatio;
        supplyRatio[type] = materialRatio;
        activityRate[type] = fundedActivity * orderRatio * materialRatio;
    }

    double employedTotal = 0.0;
    for (double employed : actualEmployment) employedTotal += employed;
    const double unemployedLabor =
        std::max(0.0, maxLabor - employedTotal);
    const auto employedClasses = calculateEmployedClasses(
        bld.getTemplates(), actualEmployment);
    totalLaborers =
        employedClasses[LABORER] + unemployedLabor + dependentPopulation;
    totalEngineers = employedClasses[ENGINEER];
    totalCapitalists = employedClasses[CAPITALIST];
    subsistencePop = std::min(
        unemployedLabor,
        static_cast<double>(subsistenceFarms) * 5000.0);

    bld.setProductionMetrics(capacityUtilization, fundingAvailability,
                             materialAvailability, staffedCapacity,
                             productionTarget);
}

void LocalMarket::processProduction(
    const std::array<double, TYPE_COUNT>& activityRate,
    std::array<Money, NUM_GOODS>& formalOut,
    std::array<Money, NUM_GOODS>& realOut,
    std::array<Money, NUM_GOODS>& realIn,
    std::array<Money, TYPE_COUNT>& buildingOutput) {
    formalOut.fill(Money(0));
    realOut.fill(Money(0));
    realIn.fill(Money(0));
    buildingOutput.fill(Money(0));

    for (int type = 0; type < TYPE_COUNT; ++type) {
        if (bld.getBuildingCounts()[type] == 0) continue;
        const BuildingTemplate& bt = bld.getTemplates()[type];
        const Money requested =
            Money(bld.getBuildingCounts()[type]) *
            Money(activityRate[type]);
        if (type == RAILWAY) {
            formalOut[TRANSPORT_CAPACITY_GOOD_INDEX] += requested;
            realOut[TRANSPORT_CAPACITY_GOOD_INDEX] += requested;
            buildingOutput[type] = requested;
            continue;
        }

        if (!bt.isFinancial && bt.outputGood >= 0) {
            if (type == CONST_DEPT) {
                if (logisticsNetwork == nullptr) continue;
                double inputRatio = 1.0;
                for (int good = 0; good < NUM_GOODS; ++good) {
                    const Money required = requested * Money(bt.inputs[good]);
                    if (required <= Money(0)) continue;
                    const Money available = logisticsNetwork->buildingStock(
                        marketId, type, good).available();
                    inputRatio = std::min(
                        inputRatio,
                        std::clamp((available / required).toDouble(), 0.0, 1.0));
                }
                Money actualOutput = requested * Money(inputRatio);
                for (int good = 0; good < NUM_GOODS; ++good) {
                    const Money required = actualOutput * Money(bt.inputs[good]);
                    const Money consumed = logisticsNetwork->consumeBuildingInput(
                        marketId, type, good, required);
                    realIn[good] += consumed;
                }
                // Construction power is a special non-storable good. Keep it
                // in the market's current-cycle output ledger only; local and
                // national construction settlement purchases it directly.
                formalOut[bt.outputGood] += actualOutput;
                realOut[bt.outputGood] += actualOutput;
                buildingOutput[type] = actualOutput;
                continue;
            }
            const Money output = logisticsNetwork == nullptr
                ? Money(0)
                : logisticsNetwork->completeProduction(
                      marketId, type, bt.outputGood, requested);
            formalOut[bt.outputGood] += output;
            realOut[bt.outputGood] += output;
            buildingOutput[type] = output;
            for (int good = 0; good < NUM_GOODS; ++good)
                realIn[good] += output * Money(bt.inputs[good]);
            continue;
        }

        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money required =
                requested * Money(bt.inputs[good]);
            const Money consumed = takeFromInventory(good, required);
            realIn[good] += consumed;
            latestFlow.directProductionUse[good] += consumed;
        }
    }
}
