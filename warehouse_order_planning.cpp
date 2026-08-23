#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace {

bool isStorableGood(int goodIndex) {
    return goodIndex >= 0 && goodIndex < NUM_GOODS &&
           goodIndex != CONSTR_GOOD_INDEX &&
           goodIndex != TRANSPORT_CAPACITY_GOOD_INDEX;
}

}  // namespace

WarehouseOrderId WarehouseNetwork::createOrder(WarehouseOrder order,
                                                bool countAsEconomicDemand) {
    if (!order.idempotencyKey.empty()) {
        auto existing = idByIdempotencyKey.find(order.idempotencyKey);
        if (existing != idByIdempotencyKey.end()) return existing->second;
    }
    order.id = nextOrderId++;
    if (order.rootDemandId == NO_WAREHOUSE_ORDER)
        order.rootDemandId = order.id;
    order.requested = nonNegative(order.requested);
    orderList.push_back(order);
    orderIndexById[order.id] = orderList.size() - 1;
    orderIdsByRoot.emplace(order.rootDemandId, order.id);
    if (order.parentOrderId != NO_WAREHOUSE_ORDER)
        orderIdsByParent.emplace(order.parentOrderId, order.id);
    if (order.buyerWarehouseId >= 0 && validGood(order.goodIndex))
        orderIdsByWarehouseGood.emplace(
            warehouseGoodKey(order.buyerWarehouseId, order.goodIndex),
            order.id);
    if (order.kind == WarehouseOrderKind::SupplierProduction &&
        validBuilding(order.buildingType) && validGood(order.goodIndex))
        activeProductionOrderIdsByKey[
            productionOrderKey(order.buyerWarehouseId, order.buildingType,
                                order.goodIndex)].insert(order.id);
    openOrderIds.insert(order.id);
    touchRevision();
    if (!order.idempotencyKey.empty())
        idByIdempotencyKey[order.idempotencyKey] = order.id;
    (void)countAsEconomicDemand;
    return order.id;
}

WarehouseOrderId WarehouseNetwork::submitBuildingDemand(
    const BuildingMaterialRequest& request) {
    if (!hasWarehouse(request.warehouseId) ||
        !validBuilding(request.buildingType) ||
        !isStorableGood(request.goodIndex) || request.quantity <= Money(0) ||
        !isfinite(request.quantity) || request.idempotencyKey.empty()) {
        return NO_WAREHOUSE_ORDER;
    }
    auto existing = idByIdempotencyKey.find(request.idempotencyKey);
    if (existing != idByIdempotencyKey.end()) return existing->second;

    WarehouseOrder order;
    order.idempotencyKey = request.idempotencyKey;
    order.kind = WarehouseOrderKind::BuildingMaterialDemand;
    order.createdCycle = cycle;
    order.eligibleCycle = cycle;
    order.buyerWarehouseId = request.warehouseId;
    order.buildingType = request.buildingType;
    order.goodIndex = request.goodIndex;
    order.requested = request.quantity;
    order.status = WarehouseOrderStatus::PendingLocalAllocation;
    return createOrder(std::move(order), true);
}

WarehouseOrderId WarehouseNetwork::planWarehouseReplenishment(
    WarehouseId warehouseId, int goodIndex) {
    if (!hasWarehouse(warehouseId) || !isStorableGood(goodIndex))
        return NO_WAREHOUSE_ORDER;
    InventoryState& stock = warehouseStock(warehouseId, goodIndex);
    const Money quantity = stock.planReplenishmentQuantity();
    const std::string episode = warehouseEpisodeKey(warehouseId, goodIndex);
    auto active = activeEpisodeByKey.find(episode);
    if (active != activeEpisodeByKey.end()) {
        WarehouseOrder* existing = findMutableOrder(active->second);
        if (existing != nullptr && !terminal(existing->status)) {
            const Money desired = existing->received + quantity;
            if (desired > existing->requested) {
                existing->requested = desired;
                recomputeOrderStatus(*existing);
            } else if (desired < existing->requested) {
                shrinkOrderTree(*existing, desired);
                recomputeBacklogViews();
            }
            return existing->id;
        }
        activeEpisodeByKey.erase(active);

    }
    if (quantity <= Money(0)) return NO_WAREHOUSE_ORDER;
    WarehouseOrder order;
    order.idempotencyKey = episode + ":cycle:" + std::to_string(cycle);
    order.kind = WarehouseOrderKind::WarehouseReplenishment;
    order.createdCycle = cycle;
    order.eligibleCycle = cycle + 1;
    order.buyerWarehouseId = warehouseId;
    order.goodIndex = goodIndex;
    order.requested = quantity;
    order.status = WarehouseOrderStatus::WaitingForRoute;
    const WarehouseOrderId id = createOrder(std::move(order), true);
    activeEpisodeByKey[episode] = id;
    return id;
}

bool WarehouseNetwork::planWeeklyInventoryReview(
    WarehouseId warehouseId) {
    if (!hasWarehouse(warehouseId)) return false;
    const auto previous = lastInventoryReviewByWarehouse.find(warehouseId);
    if (previous != lastInventoryReviewByWarehouse.end() &&
        previous->second == cycle) {
        ++suppressedInventoryReviewsByWarehouse[warehouseId];
        return false;
    }
    lastInventoryReviewByWarehouse[warehouseId] = cycle;
    ++inventoryReviewsByWarehouse[warehouseId];
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (!isStorableGood(good)) continue;
        InventoryState& stock = warehouseStock(warehouseId, good);
        InventoryReviewDecision decision;
        decision.cycle = cycle;
        decision.warehouseId = warehouseId;
        decision.goodIndex = good;
        decision.onHand = stock.onHand;
        decision.reserved = stock.reserved;
        decision.available = stock.available();
        decision.averageDemand =
            std::max(Money(0), stock.policy.weeklyDemand);
        decision.targetStock = stock.policy.targetStock;
        decision.reorderPoint = stock.policy.reorderPoint;
        decision.inventoryPosition = stock.position();
        decision.rawGap = std::max(
            Money(0), stock.policy.targetStock - decision.inventoryPosition);
        decision.confirmedInbound = stock.confirmedInbound;
        decision.physicalInTransit = stock.physicalInTransit;
        decision.backlog = stock.backlog;
        decision.linkedOrderId =
            planWarehouseReplenishment(warehouseId, good);
        decision.plannedRequest = stock.lastPlannedReplenishment;
        if (decision.linkedOrderId != NO_WAREHOUSE_ORDER &&
            order(decision.linkedOrderId).createdCycle == cycle) {
            // A weekly mean-demand order is emitted even when the stock gap is
            // currently zero; the review reason must describe that order.
            decision.reason = InventoryReviewReason::NewOrderCreated;
        } else if (stock.replenishmentQuantity() <= Money(0)) {
            decision.reason = InventoryReviewReason::StockSufficient;
        } else if (decision.plannedRequest <= Money(0)) {
            decision.reason =
                InventoryReviewReason::RequestSmoothedToZero;
        } else {
            decision.reason = InventoryReviewReason::ExistingOrderUpdated;
        }
        inventoryReviewDecisionsByWarehouse[warehouseId]
            [static_cast<std::size_t>(good)] = decision;
    }
    touchRevision();
    return true;
}

WarehouseOrderId WarehouseNetwork::planBuildingReplenishment(
    WarehouseId warehouseId, int buildingType, int goodIndex,
    const std::string& key, WarehouseOrderId rootDemandId,
    WarehouseOrderId parentOrderId) {
    if (!hasWarehouse(warehouseId) || !validBuilding(buildingType) ||
        !isStorableGood(goodIndex) || key.empty()) {
        return NO_WAREHOUSE_ORDER;
    }
    if (parentOrderId != NO_WAREHOUSE_ORDER) {
        const WarehouseOrder* parent = findMutableOrder(parentOrderId);
        if (parent == nullptr) return NO_WAREHOUSE_ORDER;
        if (rootDemandId == NO_WAREHOUSE_ORDER)
            rootDemandId = parent->rootDemandId;
        if (rootDemandId != parent->rootDemandId)
            return NO_WAREHOUSE_ORDER;
    } else if (rootDemandId != NO_WAREHOUSE_ORDER) {
        return NO_WAREHOUSE_ORDER;
    }
    InventoryState& input = buildingStock(
        warehouseId, buildingType, goodIndex);
    const Money quantity = input.planReplenishmentQuantity();
    const std::string episode = buildingEpisodeKey(
        warehouseId, buildingType, goodIndex);
    auto active = activeEpisodeByKey.find(episode);
    if (active != activeEpisodeByKey.end()) {
        WarehouseOrder* existing = findMutableOrder(active->second);
        if (existing != nullptr && !terminal(existing->status)) {
            const Money desired = existing->received + quantity;
            if (desired > existing->requested) {
                existing->requested = desired;
                recomputeOrderStatus(*existing);
            } else if (desired < existing->requested) {
                shrinkOrderTree(*existing, desired);
                recomputeBacklogViews();
            }
            return existing->id;
        }
        activeEpisodeByKey.erase(active);
    }
    if (quantity <= Money(0)) return NO_WAREHOUSE_ORDER;

    WarehouseOrder order;
    order.rootDemandId = rootDemandId;
    order.parentOrderId = parentOrderId;
    order.idempotencyKey = key;
    order.kind = WarehouseOrderKind::BuildingMaterialDemand;
    order.createdCycle = cycle;
    order.eligibleCycle =
        parentOrderId == NO_WAREHOUSE_ORDER ? cycle : cycle + 1;
    order.buyerWarehouseId = warehouseId;
    order.buildingType = buildingType;
    order.goodIndex = goodIndex;
    order.requested = quantity;
    order.status = WarehouseOrderStatus::PendingLocalAllocation;
    const WarehouseOrderId id = createOrder(
        std::move(order), parentOrderId == NO_WAREHOUSE_ORDER);
    activeEpisodeByKey[episode] = id;
    return id;
}

WarehouseOrder* WarehouseNetwork::findMutableOrder(WarehouseOrderId orderId) {
    auto it = orderIndexById.find(orderId);
    return it == orderIndexById.end() ? nullptr : &orderList[it->second];
}

const WarehouseOrder& WarehouseNetwork::order(
    WarehouseOrderId orderId) const {
    auto it = orderIndexById.find(orderId);
    if (it == orderIndexById.end()) throw std::out_of_range("order id not found");
    return orderList[it->second];
}

