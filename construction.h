#pragma once

#include "constants.h"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

using ConstructionProjectId = std::uint64_t;

enum class ConstructionFundingKind {
    CountryTreasury,
    ProvinceInvestmentPool,
    SandboxTreasury
};

struct ConstructionFundingRef {
    ConstructionFundingKind kind = ConstructionFundingKind::CountryTreasury;
    int countryId = -1;
    int provinceId = -1;
};

struct ConstructionOwnerRef {
    OwnerType type = OWNER_GOVERNMENT;
    int countryId = -1;
    int provinceId = -1;
};

enum class ConstructionProjectStatus {
    Queued,
    Active,
    Paused,
    Completed,
    Cancelled,
    Invalidated
};

enum class ConstructionBlockReason {
    None,
    NoSupply,
    NoProvider,
    PriceLimit,
    BudgetLimit,
    FundingDepleted
};

enum class ConstructionCommandError {
    None,
    InvalidCount,
    InvalidType,
    UnknownCountry,
    UnknownProvince,
    WrongCountry,
    FinancialBuilding,
    CapacityReached,
    InvalidFunding,
    InvalidOwner,
    InvalidPrice,
    InvalidReservation,
    InsufficientFunds,
    InsufficientTreasury,
    UnknownProject,
    InvalidProjectState,
    DuplicateRequest,
    QueueRejected
};

enum class ConstructionLedgerKind {
    Submitted,
    FundsReserved,
    CapacityReserved,
    ProviderPayment,
    TaxPayment,
    StartupCapitalized,
    UnitCompleted,
    FundsReleased,
    CapacityReleased,
    Paused,
    Resumed,
    PriorityChanged,
    BudgetAdded,
    Cancelled,
    Invalidated
};

struct ConstructionRequest {
    int countryId = -1;
    int targetProvinceId = -1;
    int typeIndex = -1;
    int quantity = 0;
    ConstructionFundingRef funding;
    ConstructionOwnerRef owner;
    int priority = 0;
    Money maximumUnitPrice = Money(0);
    std::uint64_t clientRequestId = 0;
    // Manual commands enter immediately ahead of autonomous expansion.
    bool playerInitiated = false;
};

struct ConstructionQuote {
    ConstructionCommandError error = ConstructionCommandError::None;
    int acceptedCount = 0;
    int availableSlots = 0;
    Money constructionPointsPerUnit = Money(0);
    Money observedUnitPrice = Money(0);
    Money maximumUnitPrice = Money(0);
    Money constructionBudget = Money(0);
    Money startupBudget = Money(0);
    Money taxBudget = Money(0);
    Money totalBudget = Money(0);
    int estimatedCycles = 0;
    int quotedCycle = 0;

    explicit operator bool() const {
        return error == ConstructionCommandError::None && acceptedCount > 0;
    }
};

struct ConstructionCommandResult {
    ConstructionCommandError error = ConstructionCommandError::None;
    int acceptedCount = 0;
    ConstructionProjectId projectId = 0;
    Money unitBudget = Money(0);
    Money totalBudget = Money(0);

    explicit operator bool() const {
        return error == ConstructionCommandError::None &&
               acceptedCount > 0 && projectId != 0;
    }
};

struct ConstructionProject {
    ConstructionProjectId id = 0;
    std::uint64_t clientRequestId = 0;
    int payerCountryId = -1;
    std::string payerCountryTag;
    int targetProvinceId = -1;
    int typeIndex = -1;
    int quantity = 0;
    int completedUnits = 0;
    ConstructionFundingRef funding;
    ConstructionOwnerRef owner;
    Money totalBudget = Money(0);
    Money unitPrice = Money(0);
    Money maximumUnitPrice = Money(0);
    Money reservedBudget = Money(0);
    Money paidBudget = Money(0);
    Money startupCapitalPerUnit = Money(0);
    Money reservedStartupCapital = Money(0);
    Money paidStartupCapital = Money(0);
    Money totalConstruction = Money(0);
    Money remainingConstruction = Money(0);
    Money currentUnitProgress = Money(0);
    double expectedProfitPriority = 0.0;
    int priority = 0;
    std::uint64_t sequence = 0;
    int createdStep = 0;
    int lastSettledStep = -1;
    int finishedStep = -1;
    ConstructionProjectStatus status = ConstructionProjectStatus::Queued;
    ConstructionBlockReason blockReason = ConstructionBlockReason::None;

    bool live() const {
        return status == ConstructionProjectStatus::Queued ||
               status == ConstructionProjectStatus::Active ||
               status == ConstructionProjectStatus::Paused;
    }

    bool active() const { return live(); }

    bool runnable() const {
        return status == ConstructionProjectStatus::Queued ||
               status == ConstructionProjectStatus::Active;
    }

    int remainingUnits() const {
        return quantity > completedUnits ? quantity - completedUnits : 0;
    }
};

using NationalConstructionProject = ConstructionProject;

struct ConstructionProviderOffer {
    int provinceId = -1;
    Money available = Money(0);
    Money used = Money(0);
    Money unitPrice = Money(0);
    Money revenue = Money(0);
    bool nationalBase = false;
};

using NationalConstructionPoolSource = ConstructionProviderOffer;

inline Money NationalConstructionTotalCapacity(Money industrialCapacity) {
    const Money base(COUNTRY_BASE_CONSTRUCTION_CAPACITY);
    return industrialCapacity > base ? industrialCapacity : base;
}

inline Money NationalConstructionBaseSupplement(Money industrialCapacity) {
    const Money base(COUNTRY_BASE_CONSTRUCTION_CAPACITY);
    return industrialCapacity < base
        ? base - industrialCapacity : Money(0);
}

struct NationalConstructionPoolState {
    Money industrialCapacity = Money(0);
    Money industrialAvailable = Money(0);
    Money baseSupplement =
        Money(COUNTRY_BASE_CONSTRUCTION_CAPACITY);
    Money industrialUsed = Money(0);
    Money baseUsed = Money(0);
    Money baseExpenditure = Money(0);
    Money centralProviderRevenue = Money(0);
    std::vector<NationalConstructionPoolSource> industrialSources;
};

struct ConstructionLedgerEntry {
    int cycle = 0;
    ConstructionProjectId projectId = 0;
    ConstructionLedgerKind kind = ConstructionLedgerKind::Submitted;
    int provinceId = -1;
    Money amount = Money(0);
    Money quantity = Money(0);
};

struct CountryConstructionState {
    std::vector<ConstructionProject> projects;
    std::vector<ConstructionProject> history;
    std::vector<ConstructionLedgerEntry> ledger;
    std::unordered_map<int, std::array<int, TYPE_COUNT>> reservedCapacity;
    NationalConstructionPoolState pool;
    std::uint64_t nextSequence = 1;
    bool manuallyOrdered = false;
};
