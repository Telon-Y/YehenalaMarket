#include "warehouse.h"

#include <algorithm>
#include <stdexcept>

InventoryState& WarehouseNetwork::warehouseStock(WarehouseId warehouseId,
                                                  int goodIndex) {
    if (!validGood(goodIndex)) throw std::out_of_range("good index out of range");
    auto it = warehouses.find(warehouseId);
    if (it == warehouses.end()) throw std::out_of_range("warehouse id not found");
    return it->second->stock(goodIndex);
}

const InventoryState& WarehouseNetwork::warehouseStock(
    WarehouseId warehouseId, int goodIndex) const {
    if (!validGood(goodIndex)) throw std::out_of_range("good index out of range");
    auto it = warehouses.find(warehouseId);
    if (it == warehouses.end()) throw std::out_of_range("warehouse id not found");
    return it->second->stock(goodIndex);
}

InventoryState& WarehouseNetwork::buildingStock(WarehouseId warehouseId,
                                                 int buildingType,
                                                 int goodIndex) {
    if (!validBuilding(buildingType))
        throw std::out_of_range("building type out of range");
    if (!validGood(goodIndex)) throw std::out_of_range("good index out of range");
    auto it = warehouses.find(warehouseId);
    if (it == warehouses.end()) throw std::out_of_range("warehouse id not found");
    return it->second->buildingInput(buildingType, goodIndex);
}

const InventoryState& WarehouseNetwork::buildingStock(
    WarehouseId warehouseId, int buildingType, int goodIndex) const {
    if (!validBuilding(buildingType))
        throw std::out_of_range("building type out of range");
    if (!validGood(goodIndex)) throw std::out_of_range("good index out of range");
    auto it = warehouses.find(warehouseId);
    if (it == warehouses.end()) throw std::out_of_range("warehouse id not found");
    return it->second->buildingInput(buildingType, goodIndex);
}

void WarehouseNetwork::setWarehouseOnHandForSetup(WarehouseId warehouseId,
                                                   int goodIndex,
                                                   Money quantity) {
    InventoryState& state = warehouseStock(warehouseId, goodIndex);
    if (!isStorableGood(goodIndex)) {
        state = InventoryState{};
        return;
    }
    state.onHand = nonNegative(quantity);
}

void WarehouseNetwork::setWarehouseStateForSetup(WarehouseId warehouseId,
                                                  int goodIndex,
                                                  const InventoryState& state) {
    InventoryState& target = warehouseStock(warehouseId, goodIndex);
    if (!isStorableGood(goodIndex)) {
        target = InventoryState{};
        return;
    }
    target = state;
    target.onHand = nonNegative(target.onHand);
    target.reserved = std::min(nonNegative(target.reserved), target.onHand);
    target.confirmedInbound = nonNegative(target.confirmedInbound);
    target.physicalInTransit = nonNegative(target.physicalInTransit);
    target.backlog = nonNegative(target.backlog);
    target.lastRawReplenishment =
        nonNegative(target.lastRawReplenishment);
    target.lastPlannedReplenishment =
        nonNegative(target.lastPlannedReplenishment);
    target.policy.targetStock = nonNegative(target.policy.targetStock);
    target.policy.reorderPoint = nonNegative(target.policy.reorderPoint);
    target.policy.weeklyDemand = nonNegative(target.policy.weeklyDemand);
    target.policy.plannedFinalDemand =
        nonNegative(target.policy.plannedFinalDemand);
}

void WarehouseNetwork::setWarehousePolicy(WarehouseId warehouseId,
                                           int goodIndex,
                                           const InventoryPolicy& policy) {
    InventoryState& state = warehouseStock(warehouseId, goodIndex);
    if (!isStorableGood(goodIndex)) {
        state = InventoryState{};
        return;
    }
    state.policy.targetStock = nonNegative(policy.targetStock);
    state.policy.reorderPoint = nonNegative(policy.reorderPoint);
    state.policy.weeklyDemand = nonNegative(policy.weeklyDemand);
    state.policy.baseStockReplenishment = policy.baseStockReplenishment;
    state.policy.plannedFinalDemand = nonNegative(policy.plannedFinalDemand);
}

void WarehouseNetwork::setBuildingOnHandForSetup(WarehouseId warehouseId,
                                                  int buildingType,
                                                  int goodIndex,
                                                  Money quantity) {
    InventoryState& state = buildingStock(warehouseId, buildingType, goodIndex);
    if (!isStorableGood(goodIndex)) {
        state = InventoryState{};
        return;
    }
    state.onHand = nonNegative(quantity);
}

void WarehouseNetwork::setBuildingStateForSetup(WarehouseId warehouseId,
                                                 int buildingType,
                                                 int goodIndex,
                                                 const InventoryState& state) {
    InventoryState& target = buildingStock(warehouseId, buildingType, goodIndex);
    if (!isStorableGood(goodIndex)) {
        target = InventoryState{};
        return;
    }
    target = state;
    target.onHand = nonNegative(target.onHand);
    target.reserved = std::min(nonNegative(target.reserved), target.onHand);
    target.confirmedInbound = nonNegative(target.confirmedInbound);
    target.physicalInTransit = nonNegative(target.physicalInTransit);
    target.backlog = nonNegative(target.backlog);
    target.lastRawReplenishment =
        nonNegative(target.lastRawReplenishment);
    target.lastPlannedReplenishment =
        nonNegative(target.lastPlannedReplenishment);
    target.policy.targetStock = nonNegative(target.policy.targetStock);
    target.policy.reorderPoint = nonNegative(target.policy.reorderPoint);
    target.policy.weeklyDemand = nonNegative(target.policy.weeklyDemand);
}

void WarehouseNetwork::setBuildingPolicy(WarehouseId warehouseId,
                                          int buildingType,
                                          int goodIndex,
                                          const InventoryPolicy& policy) {
    InventoryState& state = buildingStock(warehouseId, buildingType, goodIndex);
    if (!isStorableGood(goodIndex)) {
        state = InventoryState{};
        return;
    }
    state.policy.targetStock = nonNegative(policy.targetStock);
    state.policy.reorderPoint = nonNegative(policy.reorderPoint);
    state.policy.weeklyDemand = nonNegative(policy.weeklyDemand);
    state.policy.baseStockReplenishment = policy.baseStockReplenishment;
    state.policy.plannedFinalDemand = nonNegative(policy.plannedFinalDemand);
}

