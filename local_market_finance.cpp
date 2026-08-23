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
    bld.addCash(INDUSTRIAL_BANK, payInterest);
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
    // ==========================================
    if (bld.getBuildingCounts()[FINANCE] > 0) {
        Money revenue = revenueByBuilding[FINANCE];
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

    // Actual profit rates.
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (bld.getBuildingCounts()[t] == 0) continue;
        Money totalCost = inputCostByBuilding[t] + laborCostByBuilding[t];
        Money totalRevenue = revenueByBuilding[t];
        if (buildingOutput[t] < Money(1e-6) && !bld.getTemplates()[t].isFinancial) {
            actualProfitRates[t] = 0.0;
        } else if (totalCost.abs() > Money(1e-6)) {
            actualProfitRates[t] = ((totalRevenue - totalCost) / totalCost).toDouble();
        }
    }
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
