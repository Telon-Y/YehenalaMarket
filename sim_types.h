// ==================== sim_types.h ====================
// 跨市场共享数据结构
#pragma once
#include "constants.h"
#include "decimal.h"
#include "warehouse.h"
#include <string>
#include <array>
#include <cstdint>
#include <vector>

// 跨市场贸易路径
struct TradePath {
    int id = -1;
    int routeId = -1;
    int sourceMarketId = -1;
    int targetMarketId = -1;
    int goodIndex = 0;
    Money maxVolumePerWeek = Money(0);       // 每周最大运输量
    Money transportCostPerUnit = Money(0);   // 每单位货物的铁路运输成本
    Money transportCapacityPerUnit = Money(0);
    Money railwayCapacityPricePerUnit = Money(0);
    double distanceKm = 0.0;
    double railwayMarkupRate = RAILWAY_MARKUP_RATE;
    double warehouseMarginShare = WAREHOUSE_MARGIN_SHARE;
    bool railwayRequired = false;
    bool active = true;
};

struct MarketFlowSnapshot {
    int cycle = -1;
    std::array<Money, NUM_GOODS> openingStock{};
    std::array<Money, NUM_GOODS> production{};
    std::array<Money, NUM_GOODS> received{};
    std::array<Money, NUM_GOODS> dispatched{};
    std::array<Money, NUM_GOODS> movedToBuilding{};
    std::array<Money, NUM_GOODS> buildingConsumed{};
    std::array<Money, NUM_GOODS> directProductionUse{};
    std::array<Money, NUM_GOODS> consumerUse{};
    std::array<Money, NUM_GOODS> constructionUse{};
    std::array<Money, NUM_GOODS> closingStock{};
    std::array<Money, NUM_GOODS> inTransit{};
    std::array<Money, NUM_GOODS> inventoryResidual{};
    Money consumerValue = Money(0);
    Money constructionValue = Money(0);
    Money grossOutputValue = Money(0);
    Money intermediateCost = Money(0);
    Money gdp = Money(0);
    bool inventoryBalanced = true;
};

// 市场快照（供世界层查询，避免直接耦合内部状态）
struct MarketSnapshot {
    int marketId = -1;
    std::string marketName;
    std::array<Money, NUM_GOODS> prices{};
    std::array<Money, NUM_GOODS> inventory{};
    struct StockSnapshot {
        Money onHand = Money(0);
        Money reserved = Money(0);
        Money available = Money(0);
        Money confirmedInbound = Money(0);
        Money physicalInTransit = Money(0);
        Money backlog = Money(0);
        Money position = Money(0);
        Money targetStock = Money(0);
        Money reorderPoint = Money(0);
        Money replenishment = Money(0);
        Money demand52 = Money(0);
        Money averageDemand = Money(0);
        double coverageWeeks = 0.0;
        int reviewCycle = -1;
        Money reviewAverageDemand = Money(0);
        Money reviewTargetStock = Money(0);
        Money reviewReorderPoint = Money(0);
        Money reviewOnHand = Money(0);
        Money reviewReserved = Money(0);
        Money reviewAvailable = Money(0);
        Money reviewPosition = Money(0);
        Money reviewConfirmedInbound = Money(0);
        Money reviewPhysicalInTransit = Money(0);
        Money reviewBacklog = Money(0);
        double reviewCoverageWeeks = 0.0;
        Money rawReplenishment = Money(0);
        Money plannedRequest = Money(0);
        WarehouseOrderId linkedOrderId = NO_WAREHOUSE_ORDER;
        InventoryReviewReason reviewReason =
            InventoryReviewReason::StockSufficient;
    };
    std::array<StockSnapshot, NUM_GOODS> warehouseStock{};
    std::array<Money, NUM_GOODS> productionDemand52{};
    std::array<Money, NUM_GOODS> productionCommand{};
    std::array<Money, NUM_GOODS> pendingProduction{};
    int productionPlanCycle = -1;
    int productionResultCycle = -1;
    int lastInventoryReviewCycle = -1;
    std::uint64_t inventoryReviewCount = 0;
    std::uint64_t suppressedInventoryReviews = 0;
    Money gdp = Money(0);
    double population = 0.0;
    int cycle = 0;
    MarketFlowSnapshot flow{};
};

struct ConstructionProjectSnapshot {
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
    Money progress = Money(0);
    int createdStep = 0;
    int lastSettledStep = -1;
    int status = 0;
};

struct BuildingSnapshot {
    int typeIndex = -1;
    int count = 0;
    double employment = 0.0;
    double utilization = 0.0;
    double fullEmployment = 0.0;
    double targetEmployment = 0.0;
    double actualEmploymentRate = 0.0;
    double recruitmentSatisfaction = 0.0;
    Money baseWage = Money(0);
    Money bonusWage = Money(0);
    Money effectiveWage = Money(0);
    double fundingAvailability = 0.0;
    double materialAvailability = 0.0;
    double capacityUtilization = 0.0;
    Money staffedCapacity = Money(0);
    Money productionTarget = Money(0);
    double output = 0.0;
    double profitRate = 0.0;
    bool operational = false;
    int pending = 0;
};

struct PopulationClassSnapshot {
    int classIndex = -1;
    double population = 0.0;
    double employed = 0.0;
    double unemployed = 0.0;
    Money income = Money(0);
    double demandSatisfaction = 0.0;
};

struct ProvinceSnapshot {
    int provinceId = -1;
    int countryId = -1;
    int cycle = 0;
    std::string name;
    std::string countryName;
    double population = 0.0;
    double dependentPopulation = 0.0;
    Money gdp = Money(0);
    double employmentRate = 0.0;
    Money priceLevel = Money(0);
    double satisfaction = 0.0;
    Money totalMoneySupply = Money(0);
    Money investmentPool = Money(0);
    Money bankLoanCapacity = Money(0);
    Money totalDebt = Money(0);
    Money investmentLoanBalance = Money(0);
    std::vector<Money> gdpHistory;
    std::vector<double> populationHistory;
    std::vector<BuildingSnapshot> buildings;
    std::vector<PopulationClassSnapshot> populationClasses;
};

struct CountrySnapshot {
    int countryId = -1;
    int cycle = 0;
    std::string key;
    std::string countryCode;
    // Compatibility alias retained for serialized/UI callers.
    std::string tag;
    std::string name;
    std::vector<int> provinceIds;
    double population = 0.0;
    Money gdp = Money(0);
    Money treasury = Money(0);
    Money reservedConstructionBudget = Money(0);
    Money availableTreasury = Money(0);
    Money industrialConstructionCapacity = Money(0);
    Money industrialConstructionAvailable = Money(0);
    Money baseConstructionSupplement = Money(0);
    Money industrialConstructionUsed = Money(0);
    Money baseConstructionUsed = Money(0);
    Money baseConstructionExpenditure = Money(0);
    std::vector<ConstructionProjectSnapshot> constructionProjects;
};

struct WarehouseOrderSnapshot {
    WarehouseOrderId id = NO_WAREHOUSE_ORDER;
    WarehouseOrderId rootDemandId = NO_WAREHOUSE_ORDER;
    WarehouseOrderId parentOrderId = NO_WAREHOUSE_ORDER;
    WarehouseOrderKind kind = WarehouseOrderKind::BuildingMaterialDemand;
    WarehouseOrderStatus status =
        WarehouseOrderStatus::PendingLocalAllocation;
    int createdCycle = 0;
    int eligibleCycle = 0;
    int buyerWarehouseId = -1;
    int sellerWarehouseId = -1;
    int routeId = -1;
    int buildingType = -1;
    int goodIndex = -1;
    Money requested = Money(0);
    Money locallyAllocated = Money(0);
    Money accepted = Money(0);
    Money reserved = Money(0);
    Money shipped = Money(0);
    Money received = Money(0);
    Money contractPrice = Money(0);
    Money sourceUnitPrice = Money(0);
    Money destinationUnitPrice = Money(0);
    Money transportCostPerUnit = Money(0);
    Money transportCapacityPerUnit = Money(0);
    Money railwayCapacityPricePerUnit = Money(0);
    Money railwayChargePerUnit = Money(0);
    Money railwayMarkupPerUnit = Money(0);
    Money warehouseMarginPerUnit = Money(0);
    Money escrowed = Money(0);
    bool profitableTrade = false;
};

struct ShipmentSnapshot {
    std::uint64_t id = 0;
    WarehouseOrderId orderId = NO_WAREHOUSE_ORDER;
    int routeId = -1;
    int sourceWarehouseId = -1;
    int destinationWarehouseId = -1;
    int goodIndex = -1;
    Money cargo = Money(0);
    Money capacityUsed = Money(0);
    int dispatchedCycle = 0;
    int remainingCycles = 0;
    bool delivered = false;
};

struct TradeFlowSnapshot {
    int sourceWarehouseId = -1;
    int destinationWarehouseId = -1;
    int goodIndex = -1;
    Money quantity = Money(0);
    int cycle = -1;
};

// Immutable transport view used by the GUI and other read-only consumers.
// Keeping routes, orders and shipments under one cycle prevents panels from
// observing different phases of the logistics pipeline.
struct RouteSnapshot {
    int id = -1;
    int sourceWarehouseId = -1;
    int destinationWarehouseId = -1;
    int goodIndex = -1;
    Money capacityPerCycle = Money(0);
    Money unitPrice = Money(0);
    Money sourceUnitPrice = Money(0);
    Money destinationUnitPrice = Money(0);
    Money transportCostPerUnit = Money(0);
    Money transportCapacityPerUnit = Money(0);
    Money railwayCapacityPricePerUnit = Money(0);
    Money railwayChargePerUnit = Money(0);
    Money railwayMarkupPerUnit = Money(0);
    Money warehouseMarginPerUnit = Money(0);
    double distanceKm = 0.0;
    int transitCycles = 1;
    bool active = true;
    bool railwayAvailable = true;
    bool profitable = true;
    Money usedCapacity = Money(0);
    Money queuedVolume = Money(0);
    Money railwayRevenue = Money(0);
    Money warehouseProfit = Money(0);
};

struct WarehouseLocationSnapshot {
    int warehouseId = -1;
    int provinceId = -1;
    int countryId = -1;
    std::string provinceName;
    int railwayLevels = 0;
    int lastInventoryReviewCycle = -1;
    std::uint64_t inventoryReviewCount = 0;
    std::uint64_t suppressedInventoryReviews = 0;
};

struct InventoryReviewDecisionSnapshot {
    int cycle = -1;
    int warehouseId = -1;
    int goodIndex = -1;
    Money onHand = Money(0);
    Money reserved = Money(0);
    Money available = Money(0);
    Money averageDemand = Money(0);
    Money targetStock = Money(0);
    Money reorderPoint = Money(0);
    Money inventoryPosition = Money(0);
    Money rawGap = Money(0);
    Money plannedRequest = Money(0);
    Money confirmedInbound = Money(0);
    Money physicalInTransit = Money(0);
    Money backlog = Money(0);
    WarehouseOrderId linkedOrderId = NO_WAREHOUSE_ORDER;
    InventoryReviewReason reason = InventoryReviewReason::StockSufficient;
};

struct TransportationSnapshot {
    int cycle = 0;
    int usageCycle = -1;
    std::uint64_t revision = 0;
    Money totalEscrow = Money(0);
    Money totalCapacity = Money(0);
    Money totalUsedCapacity = Money(0);
    Money totalQueuedVolume = Money(0);
    Money totalRailwayRevenue = Money(0);
    Money totalWarehouseProfit = Money(0);
    int profitableRoutes = 0;
    int railwayBlockedRoutes = 0;
    int unprofitableRoutes = 0;
    std::vector<WarehouseLocationSnapshot> warehouses;
    std::vector<InventoryReviewDecisionSnapshot> inventoryReviews;
    std::vector<RouteSnapshot> routes;
    std::vector<WarehouseOrderSnapshot> orders;
    std::vector<ShipmentSnapshot> shipments;
    std::vector<TradeFlowSnapshot> tradeFlows;
};
