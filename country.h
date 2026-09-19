#pragma once

#include "constants.h"
#include "construction.h"
#include "national_market.h"

#include <cstdint>
#include <string>
#include <vector>

class World;
class ConstructionService;
class ConstructionSystem;

struct TransactionTaxPolicy {
    Money rate = Money(0.01);
    int startStep = 0;
    // National construction and public services need a recurring revenue
    // source. Scenarios that want a temporary introductory tax can still set
    // an explicit end step.
    int endStep = -1;

    bool activeAt(int step) const {
        return rate > Money(0) && step >= startStep &&
               (endStep < 0 || step < endStep);
    }
};

class Country {
public:
    Country(int id, std::string name, int nationalMarketId,
            std::string key = "", std::string countryCode = "");

    int getId() const { return id; }
    const std::string& getKey() const { return key; }
    const std::string& getCountryCode() const { return countryCode; }
    // Compatibility alias for older callers that used the tag name.
    const std::string& getTag() const { return countryCode; }
    const std::string& getName() const { return name; }
    const std::vector<int>& getProvinceIds() const { return provinceIds; }
    int getOverlordCountryId() const { return overlordCountryId; }
    bool isSovereign() const { return overlordCountryId < 0; }
    bool containsProvince(int provinceId) const;


    const std::vector<NationalConstructionProject>&
    getConstructionQueue() const { return constructionState.projects; }
    const std::vector<NationalConstructionProject>&
    getConstructionProjects() const { return constructionState.projects; }
    const std::vector<ConstructionProject>& getConstructionHistory() const {
        return constructionState.history;
    }
    const std::vector<ConstructionLedgerEntry>& getConstructionLedger() const {
        return constructionState.ledger;
    }
    const NationalConstructionPoolState& getConstructionPoolState() const {
        return constructionState.pool;
    }
    bool hasActiveConstruction() const;
    const NationalConstructionProject* findConstructionProject(
        std::uint64_t projectId) const;
    void pruneFinishedConstructionProjects();
    void setTreasuryForSetup(Money amount);
    Money quoteTransactionTax(Money taxableAmount, int step) const;
    Money getTreasury() const { return treasury; }
    Money getInitialTreasury() const { return initialTreasury; }
    Money getReservedConstructionBudget() const {
        return reservedConstructionBudget;
    }
    Money getAvailableTreasury() const {
        return std::max(Money(0), treasury - reservedConstructionBudget);
    }
    Money getCollectedTax() const { return collectedTax; }
    const TransactionTaxPolicy& getTransactionTaxPolicy() const {
        return transactionTax;
    }
    void setTransactionTaxPolicy(TransactionTaxPolicy policy) {
        policy.rate = std::max(Money(0), policy.rate);
        transactionTax = policy;
    }
    bool spendTreasury(Money amount);
    void creditTreasury(Money amount);
    bool reserveConstructionBudget(Money amount);
    bool canSettleConstructionPayment(Money amount) const;
    bool settleConstructionPayment(Money amount);
    void releaseConstructionBudget(Money amount);
    Money collectTransactionTax(Money taxableAmount, int step);

    NationalMarket& getNationalMarket() { return nationalMarket; }
    const NationalMarket& getNationalMarket() const { return nationalMarket; }

private:
    friend class World;
    friend class ConstructionService;
    friend class ConstructionSystem;

    void addProvince(int provinceId);
    void removeProvince(int provinceId);
    void setOverlordCountryId(int countryId) { overlordCountryId = countryId; }

    bool enqueueConstructionProject(NationalConstructionProject project);
    int id;
    std::string key;
    std::string countryCode;
    std::string name;
    int overlordCountryId = -1;
    std::vector<int> provinceIds;
    NationalMarket nationalMarket;
    Money treasury = Money(0);
    Money initialTreasury = Money(0);
    Money reservedConstructionBudget = Money(0);
    Money collectedTax = Money(0);
    TransactionTaxPolicy transactionTax;
    CountryConstructionState constructionState;
};
