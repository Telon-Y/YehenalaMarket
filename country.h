#pragma once

#include "constants.h"
#include "national_market.h"

#include <cstdint>
#include <string>
#include <vector>

class World;

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

// A national project is the authoritative identity for every player-issued
// construction command. The target market may execute the work, but the
// payer, reservation and lifecycle stay with the country.
enum class ConstructionProjectStatus {
    Queued,
    Active,
    Completed,
    Cancelled,
    Blocked
};

struct NationalConstructionProject {
    std::uint64_t id = 0;
    int payerCountryId = -1;
    std::string payerCountryTag;
    int targetProvinceId = -1;
    int typeIndex = -1;
    int quantity = 0;
    Money totalBudget = Money(0);
    Money unitPrice = Money(0);
    Money reservedBudget = Money(0);
    Money paidBudget = Money(0);
    Money totalConstruction = Money(0);
    Money remainingConstruction = Money(0);
    double expectedProfitPriority = 0.0;
    int createdStep = 0;
    int lastSettledStep = -1;
    ConstructionProjectStatus status = ConstructionProjectStatus::Queued;

    bool active() const {
        return status == ConstructionProjectStatus::Queued ||
               status == ConstructionProjectStatus::Active;
    }
};

struct NationalConstructionPoolSource {
    int provinceId = -1;
    Money available = Money(0);
    Money used = Money(0);
};

// This is rebuilt once per world cycle. Industrial capacity is backed by
// produced construction goods; the base supplement is a non-storable national
// capacity floor and therefore has no provincial warehouse source.
struct NationalConstructionPoolState {
    Money industrialCapacity = Money(0);
    Money industrialAvailable = Money(0);
    Money baseSupplement = Money(0);
    Money industrialUsed = Money(0);
    Money baseUsed = Money(0);
    Money baseExpenditure = Money(0);
    std::vector<NationalConstructionPoolSource> industrialSources;
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
    getConstructionQueue() const { return constructionQueue; }
    const std::vector<NationalConstructionProject>&
    getConstructionProjects() const { return constructionQueue; }
    const NationalConstructionPoolState& getConstructionPoolState() const {
        return constructionPoolState;
    }
    bool hasActiveConstructionForProvince(int provinceId) const;
    bool hasActiveConstruction() const;
    const NationalConstructionProject* findConstructionProject(
        std::uint64_t projectId) const;
    void pruneFinishedConstructionProjects();
    void setTreasuryForSetup(Money amount) {
        // Setup resets may replace the ledger, but never leave active
        // projects pointing at a reservation that was discarded.
        for (NationalConstructionProject& project : constructionQueue) {
            if (!project.active()) continue;
            project.status = ConstructionProjectStatus::Cancelled;
            project.reservedBudget = Money(0);
        }
        treasury = clamp(amount, Money(0), MONEY_SUPPLY_MAX_MONEY);
        initialTreasury = treasury;
        reservedConstructionBudget = Money(0);
    }
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
    std::vector<NationalConstructionProject> constructionQueue;
    NationalConstructionPoolState constructionPoolState;
};
