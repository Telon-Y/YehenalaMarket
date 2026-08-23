#include "country.h"

#include <algorithm>
#include <utility>

Country::Country(int id, std::string name, int nationalMarketId,
                 std::string key, std::string countryCode)
    : id(id),
      key(std::move(key)),
      countryCode(std::move(countryCode)),
      name(std::move(name)),
      nationalMarket(nationalMarketId, id),
      treasury(COUNTRY_INITIAL_TREASURY_MONEY),
      initialTreasury(COUNTRY_INITIAL_TREASURY_MONEY) {}

bool Country::containsProvince(int provinceId) const {
    return std::find(provinceIds.begin(), provinceIds.end(), provinceId) !=
           provinceIds.end();
}

bool Country::hasActiveConstruction() const {
    return std::any_of(constructionQueue.begin(), constructionQueue.end(),
                       [](const NationalConstructionProject& project) {
        return project.active();
    });
}

bool Country::hasActiveConstructionForProvince(int provinceId) const {
    for (const NationalConstructionProject& project : constructionQueue) {
        if (project.active() && project.targetProvinceId == provinceId)
            return true;
    }
    return false;
}

const NationalConstructionProject* Country::findConstructionProject(
    std::uint64_t projectId) const {
    for (const NationalConstructionProject& project : constructionQueue) {
        if (project.id == projectId) return &project;
    }
    return nullptr;
}

void Country::pruneFinishedConstructionProjects() {
    constructionQueue.erase(
        std::remove_if(
            constructionQueue.begin(), constructionQueue.end(),
            [](const NationalConstructionProject& project) {
                return !project.active();
            }),
        constructionQueue.end());
}

bool Country::enqueueConstructionProject(NationalConstructionProject project) {
    if (project.id == 0 || project.payerCountryId != id ||
        project.targetProvinceId < 0 || project.quantity <= 0 ||
        project.totalBudget <= Money(0) ||
        project.unitPrice <= Money(0) ||
        project.reservedBudget != project.totalBudget ||
        project.paidBudget != Money(0) ||
        project.totalConstruction <= Money(0) ||
        project.remainingConstruction != project.totalConstruction ||
        project.remainingConstruction <= Money(0) ||
        !project.active() || findConstructionProject(project.id) != nullptr) {
        return false;
    }
    constructionQueue.push_back(std::move(project));
    return true;
}

void Country::addProvince(int provinceId) {
    if (!containsProvince(provinceId))
        provinceIds.push_back(provinceId);
}

void Country::removeProvince(int provinceId) {
    provinceIds.erase(std::remove(provinceIds.begin(), provinceIds.end(), provinceId),
                      provinceIds.end());
}

bool Country::spendTreasury(Money amount) {
    if (!isfinite(amount) || amount < Money(0) ||
        getAvailableTreasury() < amount)
        return false;
    treasury -= amount;
    return true;
}

void Country::creditTreasury(Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return;
    treasury = clamp(treasury + amount, Money(0), MONEY_SUPPLY_MAX_MONEY);
}

bool Country::reserveConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0) ||
        getAvailableTreasury() < amount) {
        return false;
    }
    reservedConstructionBudget = clamp(
        reservedConstructionBudget + amount, Money(0), MONEY_SUPPLY_MAX_MONEY);
    return true;
}

bool Country::canSettleConstructionPayment(Money amount) const {
    return isfinite(amount) && amount >= Money(0) &&
           treasury >= amount && reservedConstructionBudget >= amount;
}

bool Country::settleConstructionPayment(Money amount) {
    if (!canSettleConstructionPayment(amount)) return false;
    treasury -= amount;
    reservedConstructionBudget = std::max(
        Money(0), reservedConstructionBudget - amount);
    return true;
}

void Country::releaseConstructionBudget(Money amount) {
    if (!isfinite(amount) || amount <= Money(0)) return;
    reservedConstructionBudget = std::max(
        Money(0), reservedConstructionBudget - amount);
}

Money Country::quoteTransactionTax(Money taxableAmount, int step) const {
    if (!isfinite(taxableAmount) || taxableAmount <= Money(0) ||
        !transactionTax.activeAt(step)) {
        return Money(0);
    }
    return taxableAmount * transactionTax.rate;
}
Money Country::collectTransactionTax(Money taxableAmount, int step) {
    const Money tax = quoteTransactionTax(taxableAmount, step);
    if (tax <= Money(0)) return Money(0);
    creditTreasury(tax);
    collectedTax += tax;
    return tax;
}
