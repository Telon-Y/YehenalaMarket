// ==================== local_market_finance.cpp ====================
// Banking, lending, profit distribution, and money supply.
#include "local_market.h"
#include "country.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>

using namespace std;

Money LocalMarket::calculateInvestmentCreditCapacity() const {
    const bool maturedDebtOutstanding = investmentLoanBalance > Money(0) &&
        investmentLoanDueStep >= 0 && stepCount >= investmentLoanDueStep;
    if (maturedDebtOutstanding) return Money(0);

    Money systemCreditLimit = moneySupplyBaseline() *
                              Money(BANK_MAX_SYSTEM_CREDIT_RATIO);
    Money systemHeadroom = std::max(Money(0), systemCreditLimit - totalDebt);
    Money levelCapacity = Money(bld.getBuildingCounts()[INDUSTRIAL_BANK]) *
                          Money(BANK_LOAN_CAPACITY_PER_LEVEL);
    return std::min({std::max(Money(0), bld.getCashPools()[INDUSTRIAL_BANK]),
                     levelCapacity, systemHeadroom});
}

void LocalMarket::processBankLoans(Money weeklyConstrDemand, Money constrPrice) {
    // ==========================================
    // The industrial bank is the primary commercial lender.
    // ==========================================
    recalculateTotalDebt();
    bool settledMaturedLoanThisWeek = false;

    // Investment-pool credit is interest-free for five years. At maturity,
    // available pool funds repay principal before any new construction spend.
    if (investmentLoanBalance > Money(0) &&
        investmentLoanDueStep >= 0 && stepCount >= investmentLoanDueStep) {
        Money principalPaid = std::min(std::max(Money(0), investmentPool),
                                       investmentLoanBalance);
        investmentPool -= principalPaid;
        bld.addCash(INDUSTRIAL_BANK, principalPaid);
        investmentLoanBalance -= principalPaid;

        if (investmentLoanBalance > Money(0)) {
            ++investmentLoanDelinquentWeeks;
        } else {
            investmentLoanBalance = Money(0);
            investmentLoanDueStep = -1;
            investmentLoanDelinquentWeeks = 0;
            settledMaturedLoanThisWeek = true;
        }
        recalculateTotalDebt();
    }

    bankLoanCapacity = calculateInvestmentCreditCapacity();

    // Investment pool borrowing is triggered by construction demand.
    Money neededMoney = weeklyConstrDemand * constrPrice * Money(INVEST_LOAN_TRIGGER_RATIO);
    bool investmentCreditEligible = !settledMaturedLoanThisWeek &&
                                    (investmentLoanDueStep < 0 ||
                                     stepCount < investmentLoanDueStep);
    Money availableInvestmentFunds = std::max(Money(0), investmentPool);
    if (investmentCreditEligible && neededMoney > Money(0) &&
        availableInvestmentFunds < neededMoney &&
        bankLoanCapacity > Money(0)) {
        Money gap = neededMoney - availableInvestmentFunds;
        Money borrow = std::min(gap, bankLoanCapacity);
        if (borrow > Money(0)) {
            investmentPool += borrow;
            investmentLoanBalance += borrow;
            bld.addCash(INDUSTRIAL_BANK, -borrow);
            if (investmentLoanDueStep < 0)
                investmentLoanDueStep = stepCount + INVEST_LOAN_TERM_WEEKS;

            recalculateTotalDebt();
            bankLoanCapacity = calculateInvestmentCreditCapacity();
        }
    }
}

void LocalMarket::processBuildingBorrowing() {
    if (bld.getBuildingCounts()[INDUSTRIAL_BANK] <= 0) return;
    Money industriBankCash = bld.getCashPools()[INDUSTRIAL_BANK];

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK || t == FINANCE || t == INDUSTRIAL_BANK || t == SAVINGS_BANK ||
            bld.getBuildingCounts()[t] == 0 || loanDelinquentWeeks[t] > 0) continue;
        Money cash = bld.getCashPools()[t];
        if (cash >= Money(0)) continue;

        int maxLoans = bld.getBuildingCounts()[t];
        int currentLoans = buildingLoanCount[t];
        int canBorrow = maxLoans - currentLoans;
        if (canBorrow <= 0) continue;

        Money needed = -cash;
        int neededUnits = (int)std::ceil(needed.toDouble() / BANK_LOAN_UNIT_VALUE);
        int borrowUnits = std::min(neededUnits, canBorrow);
        int bankAffordableUnits = (int)std::floor(industriBankCash.toDouble() / BANK_LOAN_UNIT_VALUE);
        recalculateTotalDebt();
        Money systemHeadroom = std::max(
            Money(0), moneySupplyBaseline() * Money(BANK_MAX_SYSTEM_CREDIT_RATIO) - totalDebt);
        int regulatoryUnits = static_cast<int>(std::floor(
            (systemHeadroom / Money(BANK_LOAN_UNIT_VALUE)).toDouble()));
        int actualBorrow = std::min({borrowUnits, bankAffordableUnits, regulatoryUnits});
        if (actualBorrow <= 0) continue;

        Money borrowAmount = Money(actualBorrow * BANK_LOAN_UNIT_VALUE);
        bld.addCash(t, borrowAmount);
        bld.addCash(INDUSTRIAL_BANK, -borrowAmount);
        buildingLoanCount[t] += actualBorrow;
        loanBalance[t] += borrowAmount;
        industriBankCash = bld.getCashPools()[INDUSTRIAL_BANK];
    }
    recalculateTotalDebt();
}

void LocalMarket::processBuildingRepayment(int typeIdx, Money& curCash, Money targetCash) {
    if (buildingLoanCount[typeIdx] <= 0) {
        loanDelinquentWeeks[typeIdx] = 0;
        return;
    }
    Money operatingReserve = targetCash * Money(0.2);
    Money repayBudget = std::max(Money(0), curCash - operatingReserve);

    Money interest = loanBalance[typeIdx] * Money(LOAN_INTEREST_PER_WEEK);
    Money payInterest = std::min(interest, repayBudget);
    bld.addCash(typeIdx, -payInterest);
    // The financial district is paid an intermediation fee out of interest the
    // borrower has already handed over, so the split moves existing income
    // between two institutions instead of creating any.
    Money intermediationFee = Money(0);
    if (bld.getBuildingCounts()[FINANCE] > 0)
        intermediationFee = payInterest * Money(FINANCE_INTERMEDIATION_FEE_SHARE);
    if (intermediationFee > Money(0)) {
        bld.addCash(FINANCE, intermediationFee);
        bld.addCash(INDUSTRIAL_BANK, payInterest - intermediationFee);
        financeFeeThisCycle += intermediationFee;
        bankSpreadThisCycle += payInterest - intermediationFee;
    } else {
        bld.addCash(INDUSTRIAL_BANK, payInterest);
        bankSpreadThisCycle += payInterest;
    }
    repayBudget -= payInterest;

    Money unpaidInterest = interest - payInterest;
    if (unpaidInterest > Money(0)) {
        ++loanDelinquentWeeks[typeIdx];
    } else {
        loanDelinquentWeeks[typeIdx] = 0;
    }

    int repayUnits = (int)std::floor(repayBudget.toDouble() / BANK_LOAN_UNIT_VALUE);
    repayUnits = std::min(repayUnits, buildingLoanCount[typeIdx]);
    if (repayUnits > 0) {
        Money repayAmount = Money(repayUnits * BANK_LOAN_UNIT_VALUE);
        bld.addCash(typeIdx, -repayAmount);
        bld.addCash(INDUSTRIAL_BANK, repayAmount);
        buildingLoanCount[typeIdx] -= repayUnits;
        loanBalance[typeIdx] -= repayAmount;
        curCash = bld.getCashPools()[typeIdx];
    }

    constexpr int BAD_DEBT_WEEKS = 260;
    if (loanDelinquentWeeks[typeIdx] >= BAD_DEBT_WEEKS) {
        Money borrowerCash = bld.getCashPools()[typeIdx];
        // Write off the bank's asset and charge the matching shortfall to
        // bank cash/capital. Do not recapitalize the borrower from nowhere:
        // setting a negative borrower balance to zero must have an equal and
        // opposite entry on the lender side.
        if (borrowerCash < Money(0)) {
            const Money loss = -borrowerCash;
            bld.addCash(typeIdx, loss);
            bld.addCash(INDUSTRIAL_BANK, -loss);
            // A written-off loan also destroys the capital that backed it, so
            // the bank's level-based credit limit contracts with its losses.
            bld.addFinancialCapital(INDUSTRIAL_BANK, -loss);
        }
        loanBalance[typeIdx] = Money(0);
        buildingLoanCount[typeIdx] = 0;
        loanDelinquentWeeks[typeIdx] = 0;
    }
    recalculateTotalDebt();
}

void LocalMarket::recalculateTotalDebt() {
    totalDebt = std::max(Money(0), investmentLoanBalance);
    for (const Money& balance : loanBalance)
        totalDebt += std::max(Money(0), balance);
    totalDebt = clamp(totalDebt, Money(0), MONEY_SUPPLY_MAX_MONEY);
}

void LocalMarket::processProfitDistribution(
    const std::array<Money, TYPE_COUNT>& revenueByBuilding,
    const std::array<Money, TYPE_COUNT>& inputCostByBuilding,
    const std::array<Money, TYPE_COUNT>& laborCostByBuilding,
    const std::array<Money, TYPE_COUNT>& buildingOutput,
    bool laborShortage,
    std::array<double, TYPE_COUNT>& actualProfitRates) {

    actualProfitRates.fill(0.0);
    for (int t = 0; t < TYPE_COUNT; ++t)
        bld.setActualUnitProfit(t, Money(0));

    // Rebuilt by processBuildingRepayment() below for this cycle.
    financeFeeThisCycle = Money(0);
    bankSpreadThisCycle = Money(0);
    savingsBankIncomeThisCycle = Money(0);

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0 ||
            bld.getTemplates()[t].isDevelopment()) continue;
        Money netProfit = revenueByBuilding[t] - inputCostByBuilding[t] - laborCostByBuilding[t];
        Money profitPerLevel = (bld.getBuildingCounts()[t] > 0)
            ? netProfit / Money(bld.getBuildingCounts()[t]) : Money(0);
        bld.setActualUnitProfit(t, profitPerLevel);

        Money targetWage = Money(averageWage);
        if (laborShortage && netProfit > Money(0) && actualEmployment[t] > 0)
            targetWage += netProfit * Money(0.1) / Money(actualEmployment[t]);
        if (netProfit < Money(0)) targetWage *= Money(0.9);
        Money maximumWage = Money(averageWage * 8.0);
        targetWage = clamp(targetWage, Money(0.5), maximumWage);
        buildingWages[t] += (targetWage - buildingWages[t]) * Money(0.05);
        buildingWages[t] = clamp(buildingWages[t], Money(0.5), maximumWage);

        // Every owner receives the same 10% dividend on its share of positive
        // operating profit. The remainder stays in the building for wages,
        // debt service, and reproduction.
        const auto& owned = bld.getOwnedBuildings()[t];
        Money totalDividend = Money(0);
        for (int o = 0; o < OWNER_COUNT; ++o) {
            Money dividend = profitPerLevel * Money(owned[o]) * Money(0.1);
            if (dividend <= Money(0)) continue;
            totalDividend += dividend;
            if (o == OWNER_GOVERNMENT) {
                if (fiscalCountry != nullptr) fiscalCountry->creditTreasury(dividend);
                else playerCash += dividend;
            } else if (o == OWNER_INITIAL) {
                classCash[CAPITALIST] += dividend;
            } else if (o == OWNER_FINANCE) {
                bld.addCash(FINANCE, dividend);
            }
        }
        if (totalDividend > Money(0)) {
            bld.addCash(t, -totalDividend);
            netProfit -= totalDividend;
            if (fiscalCountry == nullptr)
                playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
            clampMoney(classCash[CAPITALIST]);
        }

        Money curCash = bld.getCashPools()[t];
        Money targetCash = Money(bld.getBuildingCounts()[t]) * Money(500000.0);

        processBuildingRepayment(t, curCash, targetCash);
        curCash = bld.getCashPools()[t];

        if (curCash > targetCash) {
            Money excess = curCash - targetCash;
            bld.addCash(t, -excess);
            investmentPool += excess;
            investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
        }
    }

    // ==========================================
    // Transfer all financial-district cash to the industrial bank.
    // The district's revenue is the intermediation fee collected above, so this
    // branch is now reachable instead of being dead code.
    // ==========================================
    if (bld.getBuildingCounts()[FINANCE] > 0) {
        Money revenue = revenueByBuilding[FINANCE] + financeFeeThisCycle;
        Money cost = inputCostByBuilding[FINANCE] + laborCostByBuilding[FINANCE];
        Money net = revenue - cost;
        if (net > Money(0)) {
            bld.addCash(FINANCE, -net);
            bld.addCash(INDUSTRIAL_BANK, net);
        }
        Money curCash = bld.getCashPools()[FINANCE];
        Money targetCash = Money(bld.getBuildingCounts()[FINANCE]) * Money(500000.0);
        if (curCash > targetCash) {
            Money excess = curCash - targetCash;
            bld.addCash(FINANCE, -excess);
            bld.addCash(INDUSTRIAL_BANK, excess);
        }
    }

    // ==========================================
    // Retain half of central-bank profit and remit the rest to government.
    // ==========================================
    if (bld.getBuildingCounts()[BANK] > 0) {
        Money cbRevenue = revenueByBuilding[BANK];
        Money cbCost = inputCostByBuilding[BANK] + laborCostByBuilding[BANK];
        Money cbNet = cbRevenue - cbCost;
        if (cbNet > Money(0)) {
            Money retained = cbNet * Money(0.5);
            Money distributed = cbNet - retained;
            bld.addCash(BANK, -distributed);
            if (fiscalCountry != nullptr) fiscalCountry->creditTreasury(distributed);
            else playerCash += distributed;
            if (fiscalCountry == nullptr)
                playerCash = clamp(playerCash, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
        }
        // Move central-bank cash above the cap into the investment pool.
        if (bld.getCashPools()[BANK] > Money(1e12L)) {
            Money excess = bld.getCashPools()[BANK] - Money(1e12L);
            bld.addCash(BANK, -excess);
            investmentPool += excess;
            investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
        }
    }

    // ==========================================
    // Savings bank: intermediates savings and pays a deposit rate.
    // Its scale comes from the savings pool, its payout is bounded by the
    // spread the commercial bank actually earned this cycle, and the part it
    // keeps becomes bank capital. Every step moves existing money.
    // ==========================================
    if (bld.getBuildingCounts()[SAVINGS_BANK] > 0) {
        const Money levelCapacity =
            Money(bld.getBuildingCounts()[SAVINGS_BANK]) *
            Money(BANK_LOAN_CAPACITY_PER_LEVEL);
        const Money intermediated =
            std::min(std::max(Money(0), investmentPool), levelCapacity);
        const Money depositInterest =
            intermediated * Money(SAVINGS_DEPOSIT_RATE_PER_WEEK);
        const Money payable = std::min(
            depositInterest,
            bankSpreadThisCycle * Money(SAVINGS_PASS_THROUGH_SHARE));
        if (payable > Money(0)) {
            bld.addCash(INDUSTRIAL_BANK, -payable);
            const Money toHouseholds = payable * Money(SAVINGS_PASS_THROUGH_SHARE);
            const Money retained = payable - toHouseholds;
            bld.addCash(SAVINGS_BANK, retained);
            bld.addFinancialCapital(SAVINGS_BANK, retained);
            savingsBankIncomeThisCycle = retained;
            if (toHouseholds > Money(0)) {
                Money weight = Money(0);
                for (int c = 0; c < CLASS_COUNT; ++c)
                    weight += std::max(Money(0), classCash[c]);
                if (weight > Money(0)) {
                    for (int c = 0; c < CLASS_COUNT; ++c) {
                        const Money share = std::max(Money(0), classCash[c]);
                        if (share <= Money(0)) continue;
                        const Money credit = toHouseholds * share / weight;
                        classCash[c] += credit;
                        if (c == LABORER) recordLaborerDepositInterest(credit);
                        clampMoney(classCash[c]);
                    }
                } else {
                    classCash[LABORER] += toHouseholds;
                    recordLaborerDepositInterest(toHouseholds);
                    clampMoney(classCash[LABORER]);
                }
            }
        }
    }

    // Financial institutions collect most of their income as cash credits made
    // while loans are serviced rather than through revenueByBuilding. Reporting
    // and capital retention must both use the same, complete revenue figure, or
    // a profitable bank would look loss-making and bleed capital.
    const auto effectiveRevenue = [&](int t) {
        Money revenue = revenueByBuilding[t];
        if (t == FINANCE) revenue += financeFeeThisCycle;
        else if (t == INDUSTRIAL_BANK) revenue += bankSpreadThisCycle;
        else if (t == SAVINGS_BANK) revenue += savingsBankIncomeThisCycle;
        return revenue;
    };

    // ==========================================
    // Retained earnings build bank capital and operating losses consume it.
    // Capital, not cash, is what sets an institution's level and therefore its
    // credit limit, so profit lets a bank grow and a persistent loss makes it
    // contract instead of persisting at a size it can no longer support.
    // ==========================================
    for (const int type : {BANK, INDUSTRIAL_BANK, SAVINGS_BANK}) {
        if (bld.getBuildingCounts()[type] == 0) continue;
        const Money net = effectiveRevenue(type) -
                          inputCostByBuilding[type] -
                          laborCostByBuilding[type];
        if (net > Money(0)) {
            Money retained = net * Money(0.1);
            retained =
                std::min(retained, std::max(Money(0), bld.getCashPools()[type]));
            if (retained <= Money(0)) continue;
            // Retention is a claim on cash the institution already holds, not a
            // second payment: the cash stays in the pool and only the capital
            // counter moves. Debiting the pool here would leave that money with
            // no receiver at all, which is exactly what a money audit reports as
            // a black hole.
            bld.addFinancialCapital(type, retained);
        } else if (net < Money(0)) {
            // The operating loss has already reduced cash, so charging it to
            // capital is the balancing entry and needs no second cash movement.
            const Money loss =
                std::min(-net, bld.getFinancialCapital(type));
            if (loss > Money(0)) bld.addFinancialCapital(type, -loss);
        }
    }

    // ==========================================
    // Move industrial-bank cash above the cap into the investment pool.
    // ==========================================
    if (bld.getBuildingCounts()[INDUSTRIAL_BANK] > 0 && bld.getCashPools()[INDUSTRIAL_BANK] > Money(1e12L)) {
        Money excess = bld.getCashPools()[INDUSTRIAL_BANK] - Money(1e12L);
        bld.addCash(INDUSTRIAL_BANK, -excess);
        investmentPool += excess;
        investmentPool = clamp(investmentPool, -INVEST_POOL_MAX_MONEY, INVEST_POOL_MAX_MONEY);
    }

    // ==========================================
    // The savings-bank cash pool is the investment pool itself.
    // ==========================================

    // Working capital the market's own construction demand cannot absorb is
    // returned to households instead of accumulating in the pool forever.
    distributeExcessInvestmentPool();

    // Actual profit rates.
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        Money totalCost = inputCostByBuilding[t] + laborCostByBuilding[t];
        // Same complete revenue figure the capital-retention step uses, so the
        // reported profit rate and the capital decision cannot disagree.
        Money totalRevenue = effectiveRevenue(t);
        if (buildingOutput[t] < Money(1e-6) && !bld.getTemplates()[t].isFinancial) {
            actualProfitRates[t] = 0.0;
        } else if (totalCost.abs() > Money(1e-6)) {
            actualProfitRates[t] = ((totalRevenue - totalCost) / totalCost).toDouble();
        }
    }
}

void LocalMarket::distributeExcessInvestmentPool() {
    if (investmentPool <= Money(0)) return;
    // Working requirement: what this market's own investment-funded
    // construction actually buys over the coming year, at the current
    // construction price.
    const Money weeklyConstructionSpend =
        getWeeklyPrivateConstructionDemand(std::max(Money(0), lastConstrProduced)) *
        priceState.prices[CONSTR_GOOD_INDEX];
    const Money ceiling = std::max(
        INVESTMENT_POOL_MIN_WORKING_MONEY,
        weeklyConstructionSpend * Money(INVESTMENT_POOL_WORKING_WEEKS));
    const Money surplus = investmentPool - ceiling;
    if (surplus <= Money(0)) return;
    const Money payout = surplus * Money(INVESTMENT_POOL_RETURN_SHARE);
    if (payout <= Money(0)) return;

    investmentPool -= payout;
    bld.syncBankLevels(investmentPool);

    // Households are the terminal pool, distributed by population. The labor
    // pool holds the overwhelming majority of the population, so it is the
    // destination; the other classes receive their proportional share, which
    // keeps their own demand alive instead of draining them to zero.
    const double weight = totalLaborers + totalEngineers + totalCapitalists;
    if (!(weight > 0.0)) {
        classCash[LABORER] += payout;
        householdTransferInflow[LABORER] += payout;
        clampMoney(classCash[LABORER]);
        investmentPoolReturned += payout;
        return;
    }
    Money distributed = Money(0);
    for (int c = 0; c < CLASS_COUNT; ++c) {
        const double population = c == LABORER ? totalLaborers
            : c == ENGINEER ? totalEngineers : totalCapitalists;
        Money share = payout * Money(population / weight);
        if (c + 1 == CLASS_COUNT) share = payout - distributed;
        if (share <= Money(0)) continue;
        classCash[c] += share;
        householdTransferInflow[c] += share;
        distributed += share;
        clampMoney(classCash[c]);
    }
    investmentPoolReturned += distributed;
}

Money LocalMarket::moneyPoolsTotal() const {
    Money total = investmentPool + playerCash;
    for (const Money cash : classCash) total += cash;
    for (const Money cash : bld.getCashPools()) total += cash;
    // Money that has left a pool but has not been credited to one yet. These
    // accumulators are cleared as the cycle allocates them, so counting them
    // here keeps the world total continuous inside a cycle instead of dipping
    // whenever a payment is in flight.
    //
    // pendingNationalConstructionRevenue is deliberately excluded: the sale is
    // credited to the construction department's cash pool at the same moment it
    // is recorded here, so the accumulator is a note, not a second pool.
    for (const Money pending : pendingTradeRevenue) total += pending;
    total += pendingRailwayRevenue + pendingWarehouseProfit;
    return total;
}

void LocalMarket::processMoneySupply() {
    totalMoneySupply = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        Money val = bld.getCashPools()[t];
        if (isfinite(val)) totalMoneySupply += val;
    }
    for (int c = 0; c < CLASS_COUNT; ++c) {
        Money val = classCash[c];
        if (isfinite(val)) totalMoneySupply += val;
    }
    if (isfinite(investmentPool)) totalMoneySupply += investmentPool;
    if (fiscalCountry == nullptr && isfinite(playerCash)) totalMoneySupply += playerCash;
    const Money treasuryShare = fiscalTreasuryShare(false);
    if (isfinite(treasuryShare)) totalMoneySupply += treasuryShare;
    if (!isfinite(totalMoneySupply)) totalMoneySupply = Money(0);
}
