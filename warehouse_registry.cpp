#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace {

Money clampQuantity(Money value) {
    if (!isfinite(value) || value < Money(0)) return Money(0);
    return std::min(value, Money(1e12L));
}

bool isStorableGood(int goodIndex) {
    return goodIndex >= 0 && goodIndex < NUM_GOODS &&
           goodIndex != CONSTR_GOOD_INDEX &&
           goodIndex != TRANSPORT_CAPACITY_GOOD_INDEX;
}

}  // namespace

bool WarehouseNetwork::validGood(int goodIndex) {
    return goodIndex >= 0 && goodIndex < NUM_GOODS;
}

bool WarehouseNetwork::validBuilding(int buildingType) {
    return buildingType >= 0 && buildingType < TYPE_COUNT;
}

Money WarehouseNetwork::nonNegative(Money value) {
    return clampQuantity(value);
}

std::uint64_t WarehouseNetwork::producerKey(WarehouseId warehouseId,
                                            int outputGood) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(warehouseId))
            << 32) |
           static_cast<std::uint32_t>(outputGood);
}

std::uint64_t WarehouseNetwork::productionOrderKey(WarehouseId warehouseId,
                                                   int buildingType,
                                                   int outputGood) {
    std::uint64_t key = static_cast<std::uint32_t>(warehouseId);
    key = (key << 16) | static_cast<std::uint16_t>(buildingType);
    key = (key << 16) | static_cast<std::uint16_t>(outputGood);
    return key;
}

std::uint64_t WarehouseNetwork::warehouseGoodKey(WarehouseId warehouseId,
                                                 int goodIndex) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(warehouseId))
            << 32) |
           static_cast<std::uint32_t>(goodIndex);
}

bool WarehouseNetwork::terminal(WarehouseOrderStatus status) {
    return status == WarehouseOrderStatus::Fulfilled ||
           status == WarehouseOrderStatus::Cancelled;
}

std::string WarehouseNetwork::warehouseEpisodeKey(WarehouseId warehouseId,
                                                  int goodIndex) {
    return "warehouse:" + std::to_string(warehouseId) + ":" +
           std::to_string(goodIndex);
}

std::string WarehouseNetwork::buildingEpisodeKey(WarehouseId warehouseId,
                                                 int buildingType,
                                                 int goodIndex) {
    return "building:" + std::to_string(warehouseId) + ":" +
           std::to_string(buildingType) + ":" + std::to_string(goodIndex);
}

bool WarehouseNetwork::addWarehouse(WarehouseId warehouseId) {
    if (warehouseId < 0 || hasWarehouse(warehouseId)) return false;
    auto warehouse = std::make_unique<Warehouse>(warehouseId);
    Warehouse* value = warehouse.get();
    ownedWarehouses.push_back(std::move(warehouse));
    warehouses.emplace(warehouseId, value);
    currentFlowByWarehouse[warehouseId].cycle = cycle;
    touchRevision();
    return true;
}

bool WarehouseNetwork::attachWarehouse(Warehouse& warehouse) {
    if (warehouse.getId() < 0 || hasWarehouse(warehouse.getId())) return false;
    warehouses.emplace(warehouse.getId(), &warehouse);
    currentFlowByWarehouse[warehouse.getId()].cycle = cycle;
    touchRevision();
    return true;
}

bool WarehouseNetwork::hasWarehouse(WarehouseId warehouseId) const {
    return warehouses.find(warehouseId) != warehouses.end();
}

int WarehouseNetwork::addRoute(WarehouseId sourceWarehouseId,
                               WarehouseId destinationWarehouseId,
                               int goodIndex, Money capacityPerCycle,
                               Money unitPrice, int transitCycles) {
    if (!hasWarehouse(sourceWarehouseId) ||
        !hasWarehouse(destinationWarehouseId) ||
        sourceWarehouseId == destinationWarehouseId ||
        !isStorableGood(goodIndex) || capacityPerCycle <= Money(0) ||
        !isfinite(capacityPerCycle) || unitPrice < Money(0) ||
        !isfinite(unitPrice)) {
        return -1;
    }
    SupplyRoute route;
    route.id = nextRouteId++;
    route.sourceWarehouseId = sourceWarehouseId;
    route.destinationWarehouseId = destinationWarehouseId;
    route.goodIndex = goodIndex;
    route.capacityPerCycle = capacityPerCycle;
    route.unitPrice = unitPrice;
    route.sourceUnitPrice = unitPrice;
    route.destinationUnitPrice = unitPrice;
    route.transitCycles = std::max(1, transitCycles);
    routeList.push_back(route);
    touchRevision();
    return route.id;
}

int WarehouseNetwork::addRailRoute(
    WarehouseId sourceWarehouseId, WarehouseId destinationWarehouseId,
    int goodIndex, Money capacityPerCycle, double distanceKm,
    Money capacityCoefficient, double railwayMarkupRate,
    double warehouseMarginShare) {
    if (!std::isfinite(distanceKm) || distanceKm <= 0.0 ||
        !isfinite(capacityCoefficient) || capacityCoefficient < Money(0) ||
        !std::isfinite(railwayMarkupRate) || railwayMarkupRate < 0.0 ||
        !std::isfinite(warehouseMarginShare) ||
        warehouseMarginShare < 0.0 || warehouseMarginShare > 1.0) {
        return -1;
    }
    const int transitCycles = 1;
    const int routeId = addRoute(
        sourceWarehouseId, destinationWarehouseId, goodIndex,
        capacityPerCycle, Money(0), transitCycles);
    if (routeId < 0) return -1;
    SupplyRoute& route = routeList.back();
    route.distanceKm = distanceKm;
    (void)railwayMarkupRate;
    (void)warehouseMarginShare;
    route.transportCapacityPerUnit =
        capacityCoefficient * Money(distanceKm) / Money(100);
    route.transportCostPerUnit = Money(0);
    route.railwayCapacityPricePerUnit = Money(0);
    route.railwayMarkupRate = 0.0;
    route.warehouseMarginShare = 0.0;
    route.dynamicPricing = true;
    refreshRouteEconomics();
    touchRevision();
    return routeId;
}

int WarehouseNetwork::inboundLeadCycles(
    WarehouseId warehouseId, int goodIndex) const {
    if (!hasWarehouse(warehouseId) || !isStorableGood(goodIndex))
        return 1;
    int best = std::numeric_limits<int>::max();
    for (const SupplyRoute& route : routeList) {
        if (!route.active ||
            route.destinationWarehouseId != warehouseId ||
            route.goodIndex != goodIndex ||
            findProducer(route.sourceWarehouseId, goodIndex) == nullptr) {
            continue;
        }
        best = std::min(best, std::max(1, route.transitCycles));
    }
    return best == std::numeric_limits<int>::max() ? 1 : best;
}

bool WarehouseNetwork::addProducer(const ProductionRecipe& recipe) {
    if (!hasWarehouse(recipe.warehouseId) ||
        !validBuilding(recipe.buildingType) ||
        !isStorableGood(recipe.outputGood) ||
        recipe.outputPerBatch <= Money(0) ||
        !isfinite(recipe.outputPerBatch) ||
        recipe.maxOutputPerCycle <= Money(0) ||
        !isfinite(recipe.maxOutputPerCycle)) {
        return false;
    }
    for (const Money input : recipe.inputs) {
        if (input < Money(0) || !isfinite(input)) return false;
    }
    const std::uint64_t key = producerKey(recipe.warehouseId, recipe.outputGood);
    if (producerIndexByKey.find(key) != producerIndexByKey.end()) return false;
    producerIndexByKey.emplace(key, producerList.size());
    producerList.push_back(recipe);
    touchRevision();
    return true;
}

bool WarehouseNetwork::upsertProducer(const ProductionRecipe& recipe) {
    if (!hasWarehouse(recipe.warehouseId) ||
        !validBuilding(recipe.buildingType) ||
        !isStorableGood(recipe.outputGood) ||
        recipe.outputPerBatch <= Money(0) ||
        !isfinite(recipe.outputPerBatch) ||
        recipe.maxOutputPerCycle <= Money(0) ||
        !isfinite(recipe.maxOutputPerCycle)) {
        return false;
    }
    for (const Money input : recipe.inputs) {
        if (input < Money(0) || !isfinite(input)) return false;
    }
    const std::uint64_t key = producerKey(recipe.warehouseId, recipe.outputGood);
    auto existing = producerIndexByKey.find(key);
    if (existing != producerIndexByKey.end()) {
        producerList[existing->second] = recipe;
        touchRevision();
        return true;
    }
    producerIndexByKey.emplace(key, producerList.size());
    producerList.push_back(recipe);
    touchRevision();
    return true;
}

bool WarehouseNetwork::removeProducer(WarehouseId warehouseId,
                                      int outputGood) {
    const std::uint64_t key = producerKey(warehouseId, outputGood);
    auto existing = producerIndexByKey.find(key);
    if (existing == producerIndexByKey.end()) return false;
    const std::size_t index = existing->second;
    const std::size_t last = producerList.size() - 1;
    if (index != last) {
        producerList[index] = std::move(producerList[last]);
        producerIndexByKey[producerKey(producerList[index].warehouseId,
                                       producerList[index].outputGood)] = index;
    }
    producerList.pop_back();
    producerIndexByKey.erase(existing);
    touchRevision();
    return true;
}

void WarehouseNetwork::attachSettlementAccount(
    WarehouseId warehouseId,
    std::function<bool(int, Money, Money)> debit,
    std::function<void(int, Money, Money)> credit,
    std::function<Money(Money)> quoteTax,
    std::function<void(Money)> collectTax,
    std::function<Money(int)> quoteUnitPrice,
    std::function<int()> railwayLevels,
    std::function<void(Money, Money)> creditLogistics,
    std::function<void(int, Money, Money)> refund,
    std::function<Money()> quoteRailCapacityPrice) {
    if (!hasWarehouse(warehouseId) || !debit || !credit) return;
    settlementAccounts[warehouseId] =
        {std::move(debit), std::move(credit), std::move(quoteTax),
         std::move(collectTax), std::move(quoteUnitPrice),
         std::move(railwayLevels), std::move(creditLogistics),
         std::move(refund), std::move(quoteRailCapacityPrice)};
    refreshRouteEconomics();
    touchRevision();
}

void WarehouseNetwork::refreshRouteEconomics() {
    for (SupplyRoute& route : routeList) {
        if (!route.dynamicPricing) continue;
        const auto source = settlementAccounts.find(
            route.sourceWarehouseId);
        const auto destination = settlementAccounts.find(
            route.destinationWarehouseId);
        const bool priced =
            source != settlementAccounts.end() &&
            destination != settlementAccounts.end() &&
            source->second.quoteUnitPrice &&
            destination->second.quoteUnitPrice;
        const bool connected =
            priced && source->second.railwayLevels &&
            destination->second.railwayLevels &&
            source->second.railwayLevels() > 0 &&
            destination->second.railwayLevels() > 0;
        route.railwayAvailable = connected;
        if (!priced) {
            route.profitable = false;
            continue;
        }

        route.sourceUnitPrice = nonNegative(
            source->second.quoteUnitPrice(route.goodIndex));
        route.destinationUnitPrice = nonNegative(
            destination->second.quoteUnitPrice(route.goodIndex));
        route.railwayMarkupPerUnit = Money(0);
        // The quoted price is for one unit of railway capacity. Cargo pays
        // according to the distance-derived capacity consumed per unit.
        Money capacityCost = Money(AVERAGE_WAGE_MONEY);
        if (source->second.quoteRailCapacityPrice)
            capacityCost = nonNegative(source->second.quoteRailCapacityPrice());
        route.railwayCapacityPricePerUnit = capacityCost;
        route.railwayChargePerUnit =
            capacityCost * route.transportCapacityPerUnit;
        // Keep the compatibility field meaningful: this is the total rail
        // charge for one unit of cargo after converting distance to capacity.
        route.transportCostPerUnit = route.railwayChargePerUnit;
        const Money landedBeforeWarehouse = route.sourceUnitPrice +
                                             route.railwayChargePerUnit;
        const Money arbitrageSpread = route.destinationUnitPrice -
                                      landedBeforeWarehouse;
        route.profitable = connected && arbitrageSpread > Money(1e-9);
        route.warehouseMarginPerUnit = Money(0);
        route.unitPrice = landedBeforeWarehouse;
    }

    for (auto& suppliers : eventualSupplierWarehouses) suppliers.clear();
    for (const auto& [warehouseId, warehouse] : warehouses) {
        (void)warehouse;
        for (int good = 0; good < NUM_GOODS; ++good) {
            const InventoryState& stock = warehouseStock(warehouseId, good);
            if (stock.available() > stock.policy.reorderPoint ||
                findProducer(warehouseId, good) != nullptr) {
                eventualSupplierWarehouses[static_cast<std::size_t>(good)]
                    .insert(warehouseId);
            }
        }
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (const SupplyRoute& route : routeList) {
            if (!route.active || !route.profitable ||
                route.goodIndex < 0 || route.goodIndex >= NUM_GOODS)
                continue;
            auto& suppliers = eventualSupplierWarehouses[
                static_cast<std::size_t>(route.goodIndex)];
            if (suppliers.find(route.sourceWarehouseId) == suppliers.end())
                continue;
            changed = suppliers.insert(route.destinationWarehouseId).second ||
                      changed;
        }
    }
}

