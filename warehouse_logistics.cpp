#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

namespace {
std::string appendNumber(const std::string& prefix, std::uint64_t value) {
    return prefix + std::to_string(value);
}
}

void WarehouseNetwork::allocateLocal() {
    touchRevision();
    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    std::vector<WarehouseOrder> supplyOrders;
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* demandPtr = findMutableOrder(orderId);
        if (demandPtr == nullptr) continue;
        WarehouseOrder& demand = *demandPtr;
        if (demand.kind != WarehouseOrderKind::BuildingMaterialDemand ||
            terminal(demand.status) ||
            demand.eligibleCycle > cycle) {
            continue;
        }
        InventoryState& source = warehouseStock(demand.buyerWarehouseId,
                                                demand.goodIndex);
        const Money stillNeeded =
            std::max(Money(0), demand.requested - demand.accepted);
        const Money local = std::min(source.available(), stillNeeded);
        if (local > Money(0)) {
            source.reserved += local;
            demand.locallyAllocated += local;
            demand.reserved += local;
            demand.accepted += local;
            buildingStock(demand.buyerWarehouseId, demand.buildingType,
                          demand.goodIndex).confirmedInbound += local;
        }
        const Money shortage = std::max(Money(0), stillNeeded - local);
        WarehouseOrder* activePurchase = nullptr;
        WarehouseOrder* activeProduction = nullptr;
        const auto childRange = orderIdsByParent.equal_range(demand.id);
        for (auto childIt = childRange.first; childIt != childRange.second;
             ++childIt) {
            const auto childIndex = orderIndexById.find(childIt->second);
            if (childIndex == orderIndexById.end()) continue;
            WarehouseOrder& child = orderList[childIndex->second];
            if (child.parentOrderId != demand.id ||
                terminal(child.status)) continue;
            if (child.kind == WarehouseOrderKind::RemotePurchase)
                activePurchase = &child;
            else if (child.kind == WarehouseOrderKind::SupplierProduction)
                activeProduction = &child;
        }
        if (activePurchase != nullptr) {
            const Money unconfirmed =
                std::max(Money(0), demand.requested - demand.accepted);
            activePurchase->requested =
                activePurchase->accepted + unconfirmed;
            if (activePurchase->requested <= Money(0) &&
                activePurchase->accepted <= Money(0)) {
                activePurchase->status = WarehouseOrderStatus::Cancelled;
                recomputeOrderStatus(*activePurchase);
            }
        }
        if (shortage <= Money(0)) {
            demand.status = WarehouseOrderStatus::Confirmed;
        } else if (activePurchase == nullptr &&
                   activeProduction == nullptr) {
            const ProductionRecipe* localProducer = findProducer(
                demand.buyerWarehouseId, demand.goodIndex);
            WarehouseOrder supply;
            supply.rootDemandId = demand.rootDemandId;
            supply.parentOrderId = demand.id;
            supply.createdCycle = cycle;
            supply.eligibleCycle = cycle + 1;
            supply.buyerWarehouseId = demand.buyerWarehouseId;
            supply.goodIndex = demand.goodIndex;
            supply.requested = shortage;
            if (localProducer != nullptr) {
                supply.idempotencyKey = appendNumber(
                    "local-production-for:", demand.id);
                supply.kind = WarehouseOrderKind::SupplierProduction;
                supply.status = WarehouseOrderStatus::Confirmed;
                supply.sellerWarehouseId = demand.buyerWarehouseId;
                supply.buildingType = localProducer->buildingType;
                supply.accepted = shortage;
            } else {
                supply.idempotencyKey = appendNumber(
                    "purchase-for:", demand.id);
                supply.kind = WarehouseOrderKind::RemotePurchase;
                supply.status = WarehouseOrderStatus::WaitingForRoute;
                supply.buildingType = demand.buildingType;
            }
            supplyOrders.push_back(std::move(supply));
            demand.status = demand.locallyAllocated > Money(0)
                ? WarehouseOrderStatus::PartiallyConfirmed
                : WarehouseOrderStatus::AwaitingSupply;
        } else {
            recomputeOrderStatus(demand);
        }
    }
    for (WarehouseOrder& supply : supplyOrders)
        createOrder(std::move(supply), false);
    recomputeBacklogViews();
}

bool WarehouseNetwork::canEventuallySupply(
    WarehouseId warehouseId, int goodIndex,
    const std::unordered_set<WarehouseId>& excluded,
    std::unordered_set<WarehouseId>& visiting) const {
    if (excluded.find(warehouseId) != excluded.end() ||
        !visiting.insert(warehouseId).second) {
        return false;
    }
    const InventoryState& localStock =
        warehouseStock(warehouseId, goodIndex);
    const Money exportable = std::max(
        Money(0),
        localStock.available() - localStock.policy.reorderPoint);
    if (exportable > Money(0) ||
        findProducer(warehouseId, goodIndex) != nullptr) {
        visiting.erase(warehouseId);
        return true;
    }
    for (const SupplyRoute& route : routeList) {
        if (!route.active || !route.profitable ||
            route.destinationWarehouseId != warehouseId ||
            route.goodIndex != goodIndex) {
            continue;
        }
        if (canEventuallySupply(route.sourceWarehouseId, goodIndex,
                                excluded, visiting)) {
            visiting.erase(warehouseId);
            return true;
        }
    }
    visiting.erase(warehouseId);
    return false;
}

const SupplyRoute* WarehouseNetwork::selectRoute(
    const WarehouseOrder& request,
    Money& remainingCapacity) const {
    const SupplyRoute* best = nullptr;
    int bestSupplyRank = 3;
    remainingCapacity = Money(0);
    std::unordered_set<WarehouseId> excluded;
    excluded.insert(request.buyerWarehouseId);
    WarehouseOrderId parentId = request.parentOrderId;
    while (parentId != NO_WAREHOUSE_ORDER) {
        const WarehouseOrder* lineage = nullptr;
        auto parentIt = orderIndexById.find(parentId);
        if (parentIt != orderIndexById.end())
            lineage = &orderList[parentIt->second];
        if (lineage == nullptr) break;
        // Only same-good ancestors can form a transport forwarding loop.
        // Production dependencies legitimately return to an earlier market
        // for a different input good (for example housing -> steel -> coal).
        if (lineage->goodIndex == request.goodIndex) {
            excluded.insert(lineage->buyerWarehouseId);
            if (lineage->sellerWarehouseId >= 0)
                excluded.insert(lineage->sellerWarehouseId);
        }
        parentId = lineage->parentOrderId;
    }
    for (const SupplyRoute& route : routeList) {
        if (!route.active || !route.profitable ||
            route.destinationWarehouseId != request.buyerWarehouseId ||
            route.goodIndex != request.goodIndex ||
            excluded.find(route.sourceWarehouseId) != excluded.end()) {
            continue;
        }
        const auto& eventual = eventualSupplierWarehouses[
            static_cast<std::size_t>(request.goodIndex)];
        if (eventual.find(route.sourceWarehouseId) == eventual.end())
            continue;
        std::unordered_set<WarehouseId> visiting;
        if (!canEventuallySupply(route.sourceWarehouseId,
                                 request.goodIndex,
                                 excluded, visiting)) {
            continue;
        }
        const InventoryState& source = warehouseStock(
            route.sourceWarehouseId, request.goodIndex);
        const Money exportable = std::max(
            Money(0),
            source.available() - source.policy.reorderPoint);
        const int supplyRank = findProducer(route.sourceWarehouseId, request.goodIndex) != nullptr
            ? 0 : (exportable > Money(0) ? 1 : 2);
        const Money capacity = route.capacityPerCycle;
        if (best == nullptr || supplyRank < bestSupplyRank ||
            (supplyRank == bestSupplyRank &&
             (route.unitPrice < best->unitPrice ||
              (route.unitPrice == best->unitPrice &&
               route.id < best->id)))) {
            best = &route;
            bestSupplyRank = supplyRank;
            remainingCapacity = capacity;
        }
    }
    return best;
}

const ProductionRecipe* WarehouseNetwork::findProducer(
    WarehouseId warehouseId, int outputGood) const {
    if (!hasWarehouse(warehouseId) || !validGood(outputGood)) return nullptr;
    const auto it = producerIndexByKey.find(producerKey(warehouseId, outputGood));
    if (it == producerIndexByKey.end() || it->second >= producerList.size())
        return nullptr;
    return &producerList[it->second];
}

void WarehouseNetwork::routeShortages() {
    touchRevision();
    // Prices, railway availability, and arbitrage profitability are sampled
    // once at the weekly routing phase. Confirmed orders then keep that quote.
    refreshRouteEconomics();
    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    std::vector<WarehouseOrder> productionOrders;
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* requestPtr = findMutableOrder(orderId);
        if (requestPtr == nullptr) continue;
        WarehouseOrder& request = *requestPtr;
        if ((request.kind != WarehouseOrderKind::RemotePurchase &&
             request.kind != WarehouseOrderKind::WarehouseReplenishment) ||
            // A partially confirmed contract can grow when its parent demand
            // grows. Keep retrying open orders so the new quantity is funded.
            terminal(request.status) ||
            request.eligibleCycle > cycle) {
            continue;
        }
        const bool needsConfirmation =
            request.accepted + Money(1e-9) < request.requested;
        if (!needsConfirmation &&
            request.status != WarehouseOrderStatus::WaitingForRoute &&
            request.status != WarehouseOrderStatus::AwaitingSupply) {
            continue;
        }
        Money routeCapacity = Money(0);
        const bool hasContract =
            request.sellerWarehouseId >= 0 && request.routeId >= 0;
        const SupplyRoute* route = nullptr;
        if (hasContract) {
            for (const SupplyRoute& candidate : routeList) {
                if (candidate.id == request.routeId && candidate.active &&
                    candidate.sourceWarehouseId ==
                        request.sellerWarehouseId &&
                    candidate.destinationWarehouseId ==
                        request.buyerWarehouseId &&
                    candidate.goodIndex == request.goodIndex) {
                    route = &candidate;
                    break;
                }
            }
        } else {
            route = selectRoute(request, routeCapacity);
        }
        const ProductionRecipe* localProducer =
            findProducer(request.buyerWarehouseId, request.goodIndex);
        if (request.kind == WarehouseOrderKind::WarehouseReplenishment &&
            localProducer != nullptr && !hasContract) {
            const Money toPlan = std::max(
                Money(0), request.requested - request.accepted);
            if (toPlan > Money(1e-9)) {
                WarehouseOrder* activeProduction = nullptr;
                const auto children =
                    orderIdsByParent.equal_range(request.id);
                for (auto child = children.first; child != children.second;
                     ++child) {
                    WarehouseOrder* candidate =
                        findMutableOrder(child->second);
                    if (candidate == nullptr ||
                        candidate->kind !=
                            WarehouseOrderKind::SupplierProduction ||
                        terminal(candidate->status) ||
                        candidate->buyerWarehouseId !=
                            request.buyerWarehouseId ||
                        candidate->buildingType !=
                            localProducer->buildingType ||
                        candidate->goodIndex != request.goodIndex) {
                        continue;
                    }
                    activeProduction = candidate;
                    break;
                }
                const bool extended =
                    activeProduction != nullptr &&
                    extendSupplierProduction(*activeProduction, toPlan);
                if (!extended) {
                    WarehouseOrder production;
                    production.rootDemandId = request.rootDemandId;
                    production.parentOrderId = request.id;
                    production.idempotencyKey =
                        "local-production-for:" +
                        std::to_string(request.id) + ":cycle:" +
                        std::to_string(cycle);
                    production.kind =
                        WarehouseOrderKind::SupplierProduction;
                    production.status = WarehouseOrderStatus::Confirmed;
                    production.createdCycle = cycle;
                    production.eligibleCycle = cycle + 1;
                    production.buyerWarehouseId =
                        request.buyerWarehouseId;
                    production.sellerWarehouseId =
                        request.buyerWarehouseId;
                    production.buildingType =
                        localProducer->buildingType;
                    production.goodIndex = request.goodIndex;
                    production.requested = toPlan;
                    production.accepted = toPlan;
                    productionOrders.push_back(std::move(production));
                }
                request.accepted = std::min(
                    request.requested, request.accepted + toPlan);
                request.productionPlanned = true;
                warehouseStock(request.buyerWarehouseId,
                               request.goodIndex).confirmedInbound +=
                    toPlan;
            }
            recomputeOrderStatus(request);
            continue;
        }
        if (route == nullptr) {
            request.status = WarehouseOrderStatus::AwaitingSupply;
            continue;
        }
        if (!hasContract) {
            request.sellerWarehouseId = route->sourceWarehouseId;
            request.routeId = route->id;
            request.contractPrice = route->unitPrice;
            request.sourceUnitPrice = route->sourceUnitPrice;
            request.destinationUnitPrice = route->destinationUnitPrice;
            request.transportCostPerUnit = route->transportCostPerUnit;
            request.transportCapacityPerUnit = route->transportCapacityPerUnit;
            request.railwayCapacityPricePerUnit =
                route->railwayCapacityPricePerUnit;
            request.railwayChargePerUnit = route->railwayChargePerUnit;
            request.railwayMarkupPerUnit = route->railwayMarkupPerUnit;
            request.warehouseMarginPerUnit = route->warehouseMarginPerUnit;
            request.profitableTrade = route->profitable;
        }
        const Money toConfirm =
            std::max(Money(0), request.requested - request.accepted);
        if (!fundOrder(request, toConfirm)) {
            request.status = WarehouseOrderStatus::WaitingForRoute;
            continue;
        }
        request.accepted += toConfirm;
        request.status = request.accepted < request.requested
            ? WarehouseOrderStatus::PartiallyConfirmed
            : WarehouseOrderStatus::Confirmed;
        warehouseStock(request.buyerWarehouseId, request.goodIndex)
            .confirmedInbound += toConfirm;
    }
    for (WarehouseOrder& production : productionOrders)
        createOrder(std::move(production), false);
    recomputeBacklogViews();
}

void WarehouseNetwork::confirmAndReserve() {
    touchRevision();
    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    std::vector<WarehouseOrder> productionOrders;
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* purchasePtr = findMutableOrder(orderId);
        if (purchasePtr == nullptr) continue;
        WarehouseOrder& purchase = *purchasePtr;
        if ((purchase.kind != WarehouseOrderKind::RemotePurchase &&
             purchase.kind != WarehouseOrderKind::WarehouseReplenishment) ||
            terminal(purchase.status) || purchase.sellerWarehouseId < 0 ||
            purchase.eligibleCycle > cycle) {
            continue;
        }
        InventoryState& source = warehouseStock(purchase.sellerWarehouseId,
                                                purchase.goodIndex);
        const Money outstanding = std::max(
            Money(0), purchase.accepted - purchase.shipped -
                          purchase.reserved);
        // A producer's safety stock is a planning target, not an embargo on
        // goods already committed by a confirmed contract. Production will
        // refill that buffer through the continuous forecast; withholding
        // all output until it is full turns every downstream shortage into a
        // multi-week stop despite available production.
        const Money protectedStock = findProducer(
            purchase.sellerWarehouseId, purchase.goodIndex) == nullptr
            ? source.policy.reorderPoint : Money(0);
        const Money exportable = std::max(
            Money(0),
            source.available() - protectedStock);
        const Money newlyReserved = std::min(exportable, outstanding);
        source.reserved += newlyReserved;
        purchase.reserved += newlyReserved;

        if (purchase.kind == WarehouseOrderKind::RemotePurchase &&
            purchase.parentOrderId != NO_WAREHOUSE_ORDER) {
            WarehouseOrder* parent = findMutableOrder(purchase.parentOrderId);
            // Only the retail building-demand layer derives its accepted
            // quantity from a RemotePurchase child. An upstream purchase can
            // also be parented by another purchase/replenishment order; in
            // that case overwriting the parent's accepted total with the
            // smaller upstream shortage makes shipped > accepted and leaves
            // confirmedInbound permanently stranded.
            if (parent != nullptr &&
                parent->kind ==
                    WarehouseOrderKind::BuildingMaterialDemand) {
                const Money oldAccepted = parent->accepted;
                parent->accepted = std::min(
                    parent->requested,
                    parent->locallyAllocated + purchase.accepted);
                const Money newlyConfirmed = std::max(
                    Money(0), parent->accepted - oldAccepted);
                if (newlyConfirmed > Money(0)) {
                    buildingStock(parent->buyerWarehouseId,
                                  parent->buildingType,
                                  parent->goodIndex).confirmedInbound +=
                        newlyConfirmed;
                }
                recomputeOrderStatus(*parent);
            }
        }

        const Money productionShortage = std::max(
            Money(0), purchase.accepted - purchase.shipped -
                          purchase.reserved);
        Money childSupplyOutstanding = Money(0);
        WarehouseOrder* activeProduction = nullptr;
        WarehouseOrder* activeUpstream = nullptr;
        const auto childRange = orderIdsByParent.equal_range(purchase.id);
        for (auto childIt = childRange.first; childIt != childRange.second;
             ++childIt) {
            const auto childIndex = orderIndexById.find(childIt->second);
            if (childIndex == orderIndexById.end()) continue;
            WarehouseOrder& child = orderList[childIndex->second];
            if (terminal(child.status) ||
                (child.kind != WarehouseOrderKind::SupplierProduction &&
                 child.kind != WarehouseOrderKind::RemotePurchase)) {
                continue;
            }
            childSupplyOutstanding += std::max(
                Money(0), child.requested - child.received);
            if (child.kind == WarehouseOrderKind::SupplierProduction &&
                activeProduction == nullptr) {
                activeProduction = &child;
            } else if (child.kind == WarehouseOrderKind::RemotePurchase &&
                       activeUpstream == nullptr) {
                activeUpstream = &child;
            }
        }
        const Money unplannedShortage = std::max(
            Money(0), productionShortage - childSupplyOutstanding);
        const ProductionRecipe* recipe = findProducer(
            purchase.sellerWarehouseId, purchase.goodIndex);
        if (unplannedShortage > Money(1e-9) && recipe != nullptr) {
            const bool extended =
                activeProduction != nullptr &&
                extendSupplierProduction(
                    *activeProduction, unplannedShortage);
            if (!extended) {
                WarehouseOrder production;
                production.rootDemandId = purchase.rootDemandId;
                production.parentOrderId = purchase.id;
                production.idempotencyKey =
                    "production-for:" +
                    std::to_string(purchase.id) + ":cycle:" +
                    std::to_string(cycle);
                production.kind =
                    WarehouseOrderKind::SupplierProduction;
                production.status = WarehouseOrderStatus::Confirmed;
                production.createdCycle = cycle;
                production.eligibleCycle = cycle + 1;
                production.buyerWarehouseId =
                    purchase.sellerWarehouseId;
                production.sellerWarehouseId =
                    purchase.sellerWarehouseId;
                production.buildingType = recipe->buildingType;
                production.goodIndex = purchase.goodIndex;
                production.requested = unplannedShortage;
                production.accepted = unplannedShortage;
                productionOrders.push_back(std::move(production));
            }
            purchase.productionPlanned = true;
        } else if (unplannedShortage > Money(1e-9) &&
                   recipe == nullptr) {
            if (activeUpstream != nullptr) {
                activeUpstream->requested = nonNegative(
                    activeUpstream->requested + unplannedShortage);
                recomputeOrderStatus(*activeUpstream);
            } else {
                WarehouseOrder upstream;
                upstream.rootDemandId = purchase.rootDemandId;
                upstream.parentOrderId = purchase.id;
                upstream.idempotencyKey =
                    "upstream-for:" +
                    std::to_string(purchase.id) + ":cycle:" +
                    std::to_string(cycle);
                upstream.kind = WarehouseOrderKind::RemotePurchase;
                upstream.status = WarehouseOrderStatus::WaitingForRoute;
                upstream.createdCycle = cycle;
                upstream.eligibleCycle = cycle + 1;
                upstream.buyerWarehouseId =
                    purchase.sellerWarehouseId;
                upstream.goodIndex = purchase.goodIndex;
                upstream.requested = unplannedShortage;
                productionOrders.push_back(std::move(upstream));
            }
            purchase.productionPlanned = true;
        }
    }
    for (WarehouseOrder& production : productionOrders)
        createOrder(std::move(production), false);
    recomputeBacklogViews();
}

void WarehouseNetwork::dispatch() {
    touchRevision();
    std::unordered_map<int, Money> dispatchedCapacityByRoute;
    std::unordered_map<WarehouseId, Money> railCapacityRemaining;
    for (const Shipment& shipment : shipmentList) {
        if (!shipment.delivered && shipment.dispatchedCycle == cycle &&
            shipment.routeId >= 0)
            dispatchedCapacityByRoute[shipment.routeId] += shipment.cargo;
    }
    std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    // Shared rail capacity is scarce. Serve older orders first so new weekly
    // demand cannot indefinitely starve an already confirmed shipment.
    std::sort(phaseOrderIds.begin(), phaseOrderIds.end());
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* purchasePtr = findMutableOrder(orderId);
        if (purchasePtr == nullptr) continue;
        WarehouseOrder& purchase = *purchasePtr;
        if ((purchase.kind != WarehouseOrderKind::RemotePurchase &&
             purchase.kind != WarehouseOrderKind::WarehouseReplenishment) ||
            purchase.sellerWarehouseId < 0 || purchase.eligibleCycle > cycle ||
            terminal(purchase.status)) {
            continue;
        }
        InventoryState& source = warehouseStock(purchase.sellerWarehouseId,
                                                purchase.goodIndex);
        const SupplyRoute* route = nullptr;
        for (const SupplyRoute& candidate : routeList) {
            if (candidate.active && candidate.id == purchase.routeId) {
                route = &candidate;
                break;
            }
        }
        if (route == nullptr) continue;

        const Money dispatchedOnRoute = dispatchedCapacityByRoute[
            route->id];
        Money remainingCapacity = std::max(
            Money(0), route->capacityPerCycle - dispatchedOnRoute);
        if (route->dynamicPricing) {
            auto [it, inserted] = railCapacityRemaining.emplace(
                route->sourceWarehouseId, Money(0));
            if (inserted) {
                const auto account = settlementAccounts.find(
                    route->sourceWarehouseId);
                const int levels = account != settlementAccounts.end() &&
                    account->second.railwayLevels
                    ? std::max(0, account->second.railwayLevels()) : 0;
                it->second = Money(levels) * Money(RAIL_CAPACITY_PER_LEVEL);
            }
            const Money capacityPerUnit =
                std::max(Money(0), route->transportCapacityPerUnit);
            if (capacityPerUnit > Money(0))
                remainingCapacity = std::min(
                    remainingCapacity, it->second / capacityPerUnit);
        }
        const Money outstanding = std::max(
            Money(0), purchase.accepted - purchase.shipped);
        const Money quantity = std::min(
            std::min(purchase.reserved, outstanding), remainingCapacity);
        if (quantity <= Money(0)) continue;

        source.reserved = std::max(Money(0), source.reserved - quantity);
        source.onHand -= quantity;
        if (source.onHand < Money(0)) source.onHand = Money(0);
        source.reserved = std::min(source.reserved, source.onHand);
        purchase.reserved =
            std::max(Money(0), purchase.reserved - quantity);
        purchase.shipped = std::min(
            purchase.accepted, purchase.shipped + quantity);
        purchase.status = WarehouseOrderStatus::InTransit;
        currentFlow(purchase.sellerWarehouseId)
            .dispatched[static_cast<std::size_t>(purchase.goodIndex)] +=
            quantity;

        InventoryState& destination = warehouseStock(
            purchase.buyerWarehouseId, purchase.goodIndex);
        destination.physicalInTransit += quantity;

        // A remote child of a building demand is already committed to the
        // building, even while the cargo is travelling through the warehouse
        // network. Keep that physical leg separate from accepted-but-unshipped
        // contract quantity so the construction input can plan against real
        // arrivals.
        if (purchase.kind == WarehouseOrderKind::RemotePurchase &&
            purchase.parentOrderId != NO_WAREHOUSE_ORDER) {
            const WarehouseOrder* parent =
                findMutableOrder(purchase.parentOrderId);
            if (parent != nullptr &&
                parent->kind == WarehouseOrderKind::BuildingMaterialDemand &&
                parent->buyerWarehouseId == purchase.buyerWarehouseId &&
                parent->goodIndex == purchase.goodIndex) {
                buildingStock(parent->buyerWarehouseId, parent->buildingType,
                              parent->goodIndex).physicalInTransit += quantity;
            }
        }

        Shipment shipment;
        shipment.id = nextShipmentId++;
        shipment.orderId = purchase.id;
        shipment.routeId = route->id;
        shipment.sourceWarehouseId = purchase.sellerWarehouseId;
        shipment.destinationWarehouseId = purchase.buyerWarehouseId;
        shipment.goodIndex = purchase.goodIndex;
        shipment.cargo = quantity;
        shipment.capacityUsed = quantity * route->transportCapacityPerUnit;
        shipment.dispatchedCycle = cycle;
        shipment.remainingCycles = route->transitCycles;
        shipmentList.push_back(shipment);
        currentRouteUsage[route->id] += quantity;
        if (route->dynamicPricing)
            railCapacityRemaining[route->sourceWarehouseId] = std::max(
                Money(0), railCapacityRemaining[route->sourceWarehouseId] -
                              shipment.capacityUsed);
    }
}

void WarehouseNetwork::advanceTransit() {
    touchRevision();
    for (Shipment& shipment : shipmentList) {
        if (shipment.delivered || shipment.remainingCycles <= 0) continue;
        --shipment.remainingCycles;
    }
}

void WarehouseNetwork::receive() {
    touchRevision();
    for (Shipment& shipment : shipmentList) {
        if (shipment.delivered || shipment.remainingCycles > 0) continue;
        WarehouseOrder* purchase = findMutableOrder(shipment.orderId);
        if (purchase == nullptr) continue;

        InventoryState& destination = warehouseStock(
            shipment.destinationWarehouseId, shipment.goodIndex);
        destination.onHand += shipment.cargo;
        currentFlow(shipment.destinationWarehouseId)
            .received[static_cast<std::size_t>(shipment.goodIndex)] +=
            shipment.cargo;
        destination.physicalInTransit = std::max(
            Money(0), destination.physicalInTransit - shipment.cargo);
        destination.confirmedInbound = std::max(
            Money(0), destination.confirmedInbound - shipment.cargo);
        purchase->received += shipment.cargo;
        settleReceived(*purchase, shipment.cargo);
        // Cargo bought for an upstream leg is already committed to the
        // downstream parent order. Reserve it on arrival so an intermediate
        // warehouse's safety-stock floor cannot absorb the shipment and leave
        // the parent permanently confirmed but unshippable.
        if (purchase->kind == WarehouseOrderKind::RemotePurchase &&
            purchase->parentOrderId != NO_WAREHOUSE_ORDER) {
            WarehouseOrder* parent =
                findMutableOrder(purchase->parentOrderId);
            if (parent != nullptr && !terminal(parent->status) &&
                (parent->kind == WarehouseOrderKind::RemotePurchase ||
                 parent->kind ==
                     WarehouseOrderKind::WarehouseReplenishment) &&
                parent->sellerWarehouseId ==
                    shipment.destinationWarehouseId &&
                parent->goodIndex == shipment.goodIndex) {
                const Money parentOutstanding = std::max(
                    Money(0), parent->accepted - parent->shipped -
                                  parent->reserved);
                const Money forwarded =
                    std::min(shipment.cargo, parentOutstanding);
                parent->reserved += forwarded;
                destination.reserved += forwarded;
                recomputeOrderStatus(*parent);
            }
        }
        completedTradeFlowsThisWindow.push_back({
            shipment.sourceWarehouseId, shipment.destinationWarehouseId,
            shipment.goodIndex, shipment.cargo, cycle});
        shipment.delivered = true;
        recomputeOrderStatus(*purchase);
    }

    shipmentList.erase(
        std::remove_if(shipmentList.begin(), shipmentList.end(),
                      [](const Shipment& shipment) {
                          return shipment.delivered;
                      }),
        shipmentList.end());

    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* demandPtr = findMutableOrder(orderId);
        if (demandPtr == nullptr) continue;
        WarehouseOrder& demand = *demandPtr;
        if (demand.kind != WarehouseOrderKind::BuildingMaterialDemand ||
            terminal(demand.status)) {
            continue;
        }
        Money remoteReceived = Money(0);
        const auto childRange = orderIdsByParent.equal_range(demand.id);
        for (auto childIt = childRange.first; childIt != childRange.second; ++childIt) {
            const auto childIndex = orderIndexById.find(childIt->second);
            if (childIndex == orderIndexById.end()) continue;
            const WarehouseOrder& child = orderList[childIndex->second];
            if (child.kind == WarehouseOrderKind::RemotePurchase)
                remoteReceived += child.received;
        }
        InventoryState& warehouse = warehouseStock(demand.buyerWarehouseId,
                                                   demand.goodIndex);
        const Money localOutstanding = std::max(
            Money(0), demand.locallyAllocated - demand.received);
        const Money remoteOutstanding = std::max(
            Money(0), remoteReceived -
                          std::max(Money(0), demand.received -
                                                demand.locallyAllocated));
        const Money deliverable = std::min(
            warehouse.onHand,
            std::min(demand.requested - demand.received,
                     localOutstanding + remoteOutstanding));
        if (deliverable <= Money(0)) continue;
        warehouse.onHand =
            std::max(Money(0), warehouse.onHand - deliverable);
        currentFlow(demand.buyerWarehouseId)
            .movedToBuilding[static_cast<std::size_t>(demand.goodIndex)] +=
            deliverable;
        const Money localDelivery = std::min(
            demand.reserved, deliverable);
        const Money remoteDelivery = std::min(
            remoteOutstanding, std::max(Money(0), deliverable - localDelivery));
        demand.reserved =
            std::max(Money(0), demand.reserved - localDelivery);
        warehouse.reserved = std::max(
            Money(0), warehouse.reserved - localDelivery);
        warehouse.reserved =
            std::min(warehouse.reserved, warehouse.onHand);
        InventoryState& input = buildingStock(demand.buyerWarehouseId,
                                              demand.buildingType,
                                              demand.goodIndex);
        input.onHand += deliverable;
        input.confirmedInbound = std::max(
            Money(0), input.confirmedInbound - deliverable);
        demand.received += deliverable;
        input.physicalInTransit = std::max(
            Money(0), input.physicalInTransit - remoteDelivery);
        recomputeOrderStatus(demand);
    }
    recomputeBacklogViews();
}
