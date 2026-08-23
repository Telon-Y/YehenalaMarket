#pragma once

#include "constants.h"
#include "recent_average.h"

#include <array>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using WarehouseId = int;
using WarehouseOrderId = std::uint64_t;

constexpr WarehouseOrderId NO_WAREHOUSE_ORDER = 0;

struct InventoryPolicy {
    Money targetStock = Money(0);
    Money reorderPoint = Money(0);
    bool baseStockReplenishment = false;
    Money weeklyDemand = Money(0);
};

struct InventoryState {
    Money onHand = Money(0);
    Money reserved = Money(0);
    Money confirmedInbound = Money(0);
    Money physicalInTransit = Money(0);
    Money backlog = Money(0);
    Money lastRawReplenishment = Money(0);
    Money lastPlannedReplenishment = Money(0);
    InventoryPolicy policy{};
    RecentAverageFilter<Money, 4> replenishmentFilter{};

    Money available() const;
    Money committedOutbound() const;
    Money position() const;
    Money replenishmentQuantity() const;
    Money planReplenishmentQuantity();
};

enum class InventoryReviewReason {
    StockSufficient,
    RequestSmoothedToZero,
    NewOrderCreated,
    ExistingOrderUpdated
};

struct InventoryReviewDecision {
    int cycle = -1;
    WarehouseId warehouseId = -1;
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

enum class WarehouseOrderKind {
    BuildingMaterialDemand,
    WarehouseReplenishment,
    RemotePurchase,
    SupplierProduction
};

enum class WarehouseOrderStatus {
    PendingLocalAllocation,
    WaitingForRoute,
    AwaitingSupply,
    Confirmed,
    PartiallyConfirmed,
    InTransit,
    PartiallyFulfilled,
    Fulfilled,
    Cancelled
};

struct WarehouseOrder {
    WarehouseOrderId id = NO_WAREHOUSE_ORDER;
    WarehouseOrderId rootDemandId = NO_WAREHOUSE_ORDER;
    WarehouseOrderId parentOrderId = NO_WAREHOUSE_ORDER;
    std::string idempotencyKey;
    WarehouseOrderKind kind = WarehouseOrderKind::BuildingMaterialDemand;
    WarehouseOrderStatus status = WarehouseOrderStatus::PendingLocalAllocation;
    int createdCycle = 0;
    int eligibleCycle = 0;
    WarehouseId buyerWarehouseId = -1;
    WarehouseId sellerWarehouseId = -1;
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
    std::array<Money, NUM_GOODS> inputRequired{};
    std::array<Money, NUM_GOODS> inputReserved{};
    std::array<Money, NUM_GOODS> inputConsumed{};
    bool productionPlanned = false;
};

struct Shipment {
    std::uint64_t id = 0;
    WarehouseOrderId orderId = NO_WAREHOUSE_ORDER;
    int routeId = -1;
    WarehouseId sourceWarehouseId = -1;
    WarehouseId destinationWarehouseId = -1;
    int goodIndex = -1;
    Money cargo = Money(0);
    Money capacityUsed = Money(0);
    int dispatchedCycle = 0;
    int remainingCycles = 0;
    bool delivered = false;
};

struct SupplyRoute {
    int id = -1;
    WarehouseId sourceWarehouseId = -1;
    WarehouseId destinationWarehouseId = -1;
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
    double railwayMarkupRate = RAILWAY_MARKUP_RATE;
    double warehouseMarginShare = WAREHOUSE_MARGIN_SHARE;
    int transitCycles = 1;
    bool active = true;
    bool dynamicPricing = false;
    bool railwayAvailable = true;
    bool profitable = true;
};

struct ProductionRecipe {
    WarehouseId warehouseId = -1;
    int buildingType = -1;
    int outputGood = -1;
    Money outputPerBatch = Money(1);
    std::array<Money, NUM_GOODS> inputs{};
    Money maxOutputPerCycle = Money(1e12L);
};

struct BuildingMaterialRequest {
    std::string idempotencyKey;
    WarehouseId warehouseId = -1;
    int buildingType = -1;
    int goodIndex = -1;
    Money quantity = Money(0);
};

struct WarehouseAudit {
    bool valid = true;
    std::string message;
};

struct WarehouseCycleFlow {
    int cycle = -1;
    std::array<Money, NUM_GOODS> produced{};
    std::array<Money, NUM_GOODS> received{};
    std::array<Money, NUM_GOODS> dispatched{};
    std::array<Money, NUM_GOODS> movedToBuilding{};
    std::array<Money, NUM_GOODS> buildingConsumed{};
};

struct WarehouseTradeFlow {
    WarehouseId sourceWarehouseId = -1;
    WarehouseId destinationWarehouseId = -1;
    int goodIndex = -1;
    Money quantity = Money(0);
    int cycle = -1;
};

class Warehouse {
public:
    explicit Warehouse(WarehouseId warehouseId = -1)
        : id(warehouseId) {}

    WarehouseId getId() const { return id; }
    InventoryState& stock(int goodIndex);
    const InventoryState& stock(int goodIndex) const;
    InventoryState& buildingInput(int buildingType, int goodIndex);
    const InventoryState& buildingInput(int buildingType, int goodIndex) const;
    std::array<Money, NUM_GOODS> onHandSnapshot() const;

private:
    friend class WarehouseNetwork;
    WarehouseId id = -1;
    std::array<InventoryState, NUM_GOODS> goods{};
    std::array<std::array<InventoryState, NUM_GOODS>, TYPE_COUNT>
        buildingInputs{};
};

class WarehouseNetwork {
public:
    WarehouseNetwork() = default;

    bool addWarehouse(WarehouseId warehouseId);
    bool attachWarehouse(Warehouse& warehouse);
    bool hasWarehouse(WarehouseId warehouseId) const;
    int addRoute(WarehouseId sourceWarehouseId,
                 WarehouseId destinationWarehouseId,
                 int goodIndex, Money capacityPerCycle,
                 Money unitPrice = Money(0), int transitCycles = 1);
    int addRailRoute(WarehouseId sourceWarehouseId,
                     WarehouseId destinationWarehouseId,
                     int goodIndex, Money capacityPerCycle,
                     double distanceKm,
                     Money capacityCoefficient =
                         Money(RAIL_DISTANCE_CAPACITY_COEFFICIENT),
                     double railwayMarkupRate = RAILWAY_MARKUP_RATE,
                     double warehouseMarginShare = WAREHOUSE_MARGIN_SHARE);
    bool addProducer(const ProductionRecipe& recipe);
    bool upsertProducer(const ProductionRecipe& recipe);
    bool removeProducer(WarehouseId warehouseId, int outputGood);
    void attachSettlementAccount(
        WarehouseId warehouseId,
        std::function<bool(int, Money, Money)> debit,
        std::function<void(int, Money, Money)> credit,
        std::function<Money(Money)> quoteTax = {},
        std::function<void(Money)> collectTax = {},
        std::function<Money(int)> quoteUnitPrice = {},
        std::function<int()> railwayLevels = {},
        std::function<void(Money, Money)> creditLogistics = {},
        std::function<void(int, Money, Money)> refund = {},
        std::function<Money()> quoteRailCapacityPrice = {});

    InventoryState& warehouseStock(WarehouseId warehouseId, int goodIndex);
    const InventoryState& warehouseStock(WarehouseId warehouseId,
                                         int goodIndex) const;
    InventoryState& buildingStock(WarehouseId warehouseId, int buildingType,
                                  int goodIndex);
    const InventoryState& buildingStock(WarehouseId warehouseId,
                                        int buildingType,
                                        int goodIndex) const;

    void setWarehouseOnHandForSetup(WarehouseId warehouseId, int goodIndex,
                                    Money quantity);
    void setWarehouseStateForSetup(WarehouseId warehouseId, int goodIndex,
                                   const InventoryState& state);
    void setWarehousePolicy(WarehouseId warehouseId, int goodIndex,
                            const InventoryPolicy& policy);
    void setBuildingOnHandForSetup(WarehouseId warehouseId, int buildingType,
                                   int goodIndex, Money quantity);
    void setBuildingStateForSetup(WarehouseId warehouseId, int buildingType,
                                  int goodIndex, const InventoryState& state);
    void setBuildingPolicy(WarehouseId warehouseId, int buildingType,
                           int goodIndex, const InventoryPolicy& policy);

    WarehouseOrderId submitBuildingDemand(
        const BuildingMaterialRequest& request);
    WarehouseOrderId planWarehouseReplenishment(WarehouseId warehouseId,
                                                 int goodIndex);
    bool planWeeklyInventoryReview(WarehouseId warehouseId);
    WarehouseOrderId planBuildingReplenishment(WarehouseId warehouseId,
                                                int buildingType,
                                                int goodIndex,
                                                const std::string& key,
                                                WarehouseOrderId rootDemandId =
                                                    NO_WAREHOUSE_ORDER,
                                                WarehouseOrderId parentOrderId =
                                                    NO_WAREHOUSE_ORDER);

    void allocateLocal();
    void routeShortages();
    void confirmAndReserve();
    void dispatch();
    int inboundLeadCycles(WarehouseId warehouseId, int goodIndex) const;
    void advanceTransit();
    void receive();
    void processProductionOrders();
    Money pendingProduction(WarehouseId warehouseId, int buildingType,
                            int outputGood) const;
    Money productionCommand(WarehouseId warehouseId, int buildingType,
                            int outputGood) const;
    Money productionInputAvailability(WarehouseId warehouseId,
                                      int buildingType, int goodIndex) const;
    Money completeProduction(WarehouseId warehouseId, int buildingType,
                             int outputGood, Money quantity);
    void finishCycle();
    void runLogisticsCycle();
    // Coalesce backlog index rebuilds across one world/local logistics cycle.
    void beginLogisticsBatch();
    void endLogisticsBatch();

    Money consumeBuildingInput(WarehouseId warehouseId, int buildingType,
                               int goodIndex, Money quantity);
    void addProductionOutput(WarehouseId warehouseId, int goodIndex,
                             Money quantity);

    int currentCycle() const { return cycle; }
    int lastCompletedUsageCycle() const { return completedUsageCycle; }
    Money lastCompletedRouteUsage(int routeId) const;
    Money lastCompletedRailwayRevenue(int routeId) const;
    Money lastCompletedWarehouseProfit(int routeId) const;
    WarehouseCycleFlow lastCompletedFlow(WarehouseId warehouseId) const;
    int lastInventoryReviewCycle(WarehouseId warehouseId) const;
    std::uint64_t inventoryReviewCount(WarehouseId warehouseId) const;
    std::uint64_t suppressedInventoryReviewCount(
        WarehouseId warehouseId) const;
    const InventoryReviewDecision* inventoryReviewDecision(
        WarehouseId warehouseId, int goodIndex) const;
    Money productionForecastDemand(WarehouseId warehouseId, int buildingType,
                                 int outputGood) const;
    Money forecastSupplyRate(WarehouseId warehouseId, int goodIndex) const;
    Money expectedOutboundDemand(WarehouseId warehouseId,
                                 int goodIndex) const;
    void refreshProductionForecasts();
    Money productionDemand52(WarehouseId warehouseId, int buildingType,
                             int outputGood) const;
    int productionCommandCycle(WarehouseId warehouseId, int buildingType,
                               int outputGood) const;
    std::uint64_t stateRevision() const { return revision; }
    const std::unordered_set<WarehouseOrderId>& activeOrderIds() const {
        return openOrderIds;
    }
    const std::vector<WarehouseOrder>& orders() const { return orderList; }
    const std::vector<Shipment>& shipments() const { return shipmentList; }
    const std::vector<WarehouseTradeFlow>& completedTradeFlows() const {
        return completedTradeFlowsThisWindow;
    }
    const std::vector<SupplyRoute>& routes() const { return routeList; }
    const std::vector<ProductionRecipe>& producers() const {
        return producerList;
    }
    const WarehouseOrder& order(WarehouseOrderId orderId) const;
    std::vector<const WarehouseOrder*> ordersForRoot(
        WarehouseOrderId rootDemandId) const;
    std::vector<const WarehouseOrder*> childrenOf(
        WarehouseOrderId parentOrderId) const;
    int activeOrders(WarehouseId buyerWarehouseId, int goodIndex) const;
    Money economicRootDemand(int goodIndex) const;
    Money physicalGoods(int goodIndex) const;
    Money escrowBalance() const { return totalEscrow; }
    std::uint64_t backlogRebuildCount() const { return backlogRebuilds; }
    WarehouseAudit audit() const;

private:
    std::unordered_map<WarehouseId, Warehouse*> warehouses;
    std::vector<std::unique_ptr<Warehouse>> ownedWarehouses;
    std::vector<SupplyRoute> routeList;
    std::vector<ProductionRecipe> producerList;
    std::vector<WarehouseOrder> orderList;
    std::vector<Shipment> shipmentList;
    std::vector<WarehouseTradeFlow> completedTradeFlowsThisWindow;
    std::unordered_map<WarehouseOrderId, std::size_t> orderIndexById;
    std::unordered_multimap<WarehouseOrderId, WarehouseOrderId>
        orderIdsByRoot;
    std::unordered_multimap<WarehouseOrderId, WarehouseOrderId>
        orderIdsByParent;
    std::unordered_multimap<std::uint64_t, WarehouseOrderId>
        orderIdsByWarehouseGood;
    std::unordered_map<std::uint64_t,
                       std::unordered_set<WarehouseOrderId>>
        activeProductionOrderIdsByKey;
    std::unordered_set<WarehouseOrderId> openOrderIds;
    std::unordered_map<std::uint64_t, std::size_t> producerIndexByKey;
    // Reachability is rebuilt once per routing quote and used as a cheap
    // negative filter before the lineage-aware DFS in selectRoute().
    std::array<std::unordered_set<WarehouseId>, NUM_GOODS>
        eventualSupplierWarehouses;
    struct ProductionControlState {
        RecentAverageFilter<Money, DEMAND_AVERAGE_WEEKS> demandFilter{};
        Money command = Money(0);
        Money demand52 = Money(0);
        int updatedCycle = -1;
    };
    std::unordered_map<std::uint64_t, ProductionControlState>
        productionControlByKey;
    std::unordered_map<std::uint64_t, Money>
        productionForecastByKey;
    std::unordered_map<std::string, WarehouseOrderId> idByIdempotencyKey;
    std::unordered_map<std::string, WarehouseOrderId> activeEpisodeByKey;
    struct SettlementAccount {
        std::function<bool(int, Money, Money)> debit;
        std::function<void(int, Money, Money)> credit;
        std::function<Money(Money)> quoteTax;
        std::function<void(Money)> collectTax;
        std::function<Money(int)> quoteUnitPrice;
        std::function<int()> railwayLevels;
        std::function<void(Money, Money)> creditLogistics;
        std::function<void(int, Money, Money)> refund;
        std::function<Money()> quoteRailCapacityPrice;
    };
    std::unordered_map<WarehouseId, SettlementAccount> settlementAccounts;
    Money totalEscrow = Money(0);
    WarehouseOrderId nextOrderId = 1;
    std::uint64_t nextShipmentId = 1;
    int nextRouteId = 0;
    int cycle = 0;
    int completedUsageCycle = -1;
    std::unordered_map<int, Money> currentRouteUsage;
    std::unordered_map<int, Money> completedRouteUsage;
    std::unordered_map<int, Money> currentRailwayRevenue;
    std::unordered_map<int, Money> completedRailwayRevenue;
    std::unordered_map<int, Money> currentWarehouseProfit;
    std::unordered_map<int, Money> completedWarehouseProfit;
    std::unordered_map<WarehouseId, int> lastInventoryReviewByWarehouse;
    std::unordered_map<WarehouseId, std::uint64_t> inventoryReviewsByWarehouse;
    std::unordered_map<WarehouseId, std::uint64_t>
        suppressedInventoryReviewsByWarehouse;
    std::unordered_map<WarehouseId,
                       std::array<InventoryReviewDecision, NUM_GOODS>>
        inventoryReviewDecisionsByWarehouse;
    std::unordered_map<WarehouseId, WarehouseCycleFlow>
        currentFlowByWarehouse;
    std::unordered_map<WarehouseId, WarehouseCycleFlow>
        completedFlowByWarehouse;
    std::uint64_t revision = 0;
    int logisticsBatchDepth = 0;
    std::uint64_t backlogRebuilds = 0;

    static bool validGood(int goodIndex);
    static bool validBuilding(int buildingType);
    static Money nonNegative(Money value);
    static std::uint64_t producerKey(WarehouseId warehouseId,
                                     int outputGood);
    static std::uint64_t productionOrderKey(WarehouseId warehouseId,
                                             int buildingType,
                                             int outputGood);
    static std::uint64_t warehouseGoodKey(WarehouseId warehouseId,
                                           int goodIndex);
    static bool terminal(WarehouseOrderStatus status);
    static std::string warehouseEpisodeKey(WarehouseId warehouseId,
                                           int goodIndex);
    static std::string buildingEpisodeKey(WarehouseId warehouseId,
                                          int buildingType,
                                          int goodIndex);
    WarehouseOrderId createOrder(WarehouseOrder order,
                                 bool countAsEconomicDemand);
    WarehouseOrder* findMutableOrder(WarehouseOrderId orderId);
    const SupplyRoute* selectRoute(const WarehouseOrder& request,
                                   Money& remainingCapacity) const;
    void refreshRouteEconomics();
    bool canEventuallySupply(
        WarehouseId warehouseId, int goodIndex,
        const std::unordered_set<WarehouseId>& excluded,
        std::unordered_set<WarehouseId>& visiting) const;
    const ProductionRecipe* findProducer(WarehouseId warehouseId,
                                         int outputGood) const;
    void releaseEpisodeIfTerminal(WarehouseOrder& order);
    void recomputeOrderStatus(WarehouseOrder& order);
    void syncOpenOrderIndex(const WarehouseOrder& order);
    void recomputeBacklogViews();
    void updateProductionCommands();
    bool extendSupplierProduction(WarehouseOrder& production,
                                  Money additional);
    void touchRevision() { ++revision; }
    bool fundOrder(WarehouseOrder& order, Money quantity);
    void settleReceived(WarehouseOrder& order, Money quantity);
    void refundEscrow(WarehouseOrder& order, Money quantity);
    Money releaseOrderReservation(WarehouseOrder& order, Money quantity);
    void shrinkOrderTree(WarehouseOrder& order, Money desiredRequested);
    WarehouseCycleFlow& currentFlow(WarehouseId warehouseId);
};
