#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <limits>

Money WarehouseNetwork::productionForecastDemand(
    WarehouseId warehouseId, int buildingType, int outputGood) const {
    const auto it = productionForecastByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    return it == productionForecastByKey.end()
        ? Money(0) : std::max(Money(0), it->second);
}

Money WarehouseNetwork::forecastSupplyRate(
    WarehouseId warehouseId, int goodIndex) const {
    if (!hasWarehouse(warehouseId) || goodIndex < 0 ||
        goodIndex >= NUM_GOODS || goodIndex == CONSTR_GOOD_INDEX ||
        goodIndex == TRANSPORT_CAPACITY_GOOD_INDEX)
        return Money(0);

    const ProductionRecipe* local = findProducer(warehouseId, goodIndex);
    if (local != nullptr) {
        return std::max(
            productionForecastDemand(warehouseId, local->buildingType,
                                     goodIndex),
            productionCommand(warehouseId, local->buildingType,
                              goodIndex));
    }

    int bestTransit = std::numeric_limits<int>::max();
    Money result = Money(0);
    for (const SupplyRoute& route : routeList) {
        if (!route.active || route.destinationWarehouseId != warehouseId ||
            route.goodIndex != goodIndex) {
            continue;
        }
        const ProductionRecipe* producer =
            findProducer(route.sourceWarehouseId, goodIndex);
        if (producer == nullptr) continue;
        const int transit = std::max(1, route.transitCycles);
        if (transit < bestTransit) {
            bestTransit = transit;
            result = Money(0);
        }
        if (transit != bestTransit) continue;
        result += std::max(
            productionForecastDemand(route.sourceWarehouseId,
                                     producer->buildingType, goodIndex),
            productionCommand(route.sourceWarehouseId,
                              producer->buildingType, goodIndex));
    }
    return std::max(Money(0), result);
}

Money WarehouseNetwork::expectedOutboundDemand(
    WarehouseId warehouseId, int goodIndex) const {
    if (!hasWarehouse(warehouseId) || !validGood(goodIndex) ||
        goodIndex == CONSTR_GOOD_INDEX ||
        goodIndex == TRANSPORT_CAPACITY_GOOD_INDEX) {
        return Money(0);
    }

    Money completed = Money(0);
    for (const WarehouseTradeFlow& flow : completedTradeFlowsThisWindow) {
        if (flow.sourceWarehouseId == warehouseId &&
            flow.goodIndex == goodIndex) {
            completed += std::max(Money(0), flow.quantity);
        }
    }
    const int observedCycles = std::max(
        1, std::min(DEMAND_AVERAGE_WEEKS, cycle));
    const Money completedRate = completed / Money(observedCycles);

    // Demand for local replenishment is the observed 52-cycle demand rate.
    // Do not add accepted-but-unshipped backlog here: doing so turns a
    // temporary transport shortage into a self-reinforcing stream of larger
    // orders, rather than preserving the fixed moving-average signal.
    return completedRate;
}

void WarehouseNetwork::refreshProductionForecasts() {
    std::unordered_map<std::uint64_t, Money> baseForecast;
    std::unordered_map<std::uint64_t, const ProductionRecipe*> recipes;
    for (const ProductionRecipe& recipe : producerList) {
        recipes.emplace(productionOrderKey(recipe.warehouseId,
                                            recipe.buildingType,
                                            recipe.outputGood), &recipe);
    }

    auto sourceKeysFor = [this](WarehouseId destinationId, int good) {
        std::vector<std::uint64_t> keys;
        const ProductionRecipe* local = findProducer(destinationId, good);
        if (local != nullptr) {
            keys.push_back(productionOrderKey(destinationId,
                                               local->buildingType, good));
            return keys;
        }

        int bestTransit = std::numeric_limits<int>::max();
        std::unordered_set<std::uint64_t> seen;
        for (const SupplyRoute& route : routeList) {
            if (!route.active || route.destinationWarehouseId != destinationId ||
                route.goodIndex != good) {
                continue;
            }
            const ProductionRecipe* producer =
                findProducer(route.sourceWarehouseId, good);
            if (producer == nullptr) continue;
            const int transit = std::max(1, route.transitCycles);
            const std::uint64_t key = productionOrderKey(
                route.sourceWarehouseId, producer->buildingType, good);
            if (transit < bestTransit) {
                bestTransit = transit;
                keys.clear();
                seen.clear();
            }
            if (transit == bestTransit && seen.insert(key).second)
                keys.push_back(key);
        }
        return keys;
    };

    // Seed the graph with the construction department's planned weekly draw.
    // Then repeatedly propagate recipe inputs upstream. The bounded fixed
    // point handles cyclic chains such as steel -> tools -> steel without
    // turning a one-week construction plan into a one-off production order.
    for (const auto& [destinationId, warehouse] : warehouses) {
        (void)warehouse;
        for (int good = 0; good < NUM_GOODS; ++good) {
            const InventoryState& input =
                buildingStock(destinationId, CONST_DEPT, good);
            if (!input.policy.baseStockReplenishment ||
                input.policy.weeklyDemand <= Money(1e-9)) {
                continue;
            }
            const std::vector<std::uint64_t> keys =
                sourceKeysFor(destinationId, good);
            if (keys.empty()) continue;
            const Money share = input.policy.weeklyDemand /
                                Money(static_cast<int>(keys.size()));
            for (const std::uint64_t key : keys)
                baseForecast[key] += share;
        }
    }

    productionForecastByKey = baseForecast;
    for (int iteration = 0; iteration < 24; ++iteration) {
        std::unordered_map<std::uint64_t, Money> next = baseForecast;
        for (const auto& [key, demand] : productionForecastByKey) {
            if (demand <= Money(1e-9)) continue;
            const auto recipeIt = recipes.find(key);
            if (recipeIt == recipes.end()) continue;
            const ProductionRecipe& recipe = *recipeIt->second;
            for (int good = 0; good < NUM_GOODS; ++good) {
                const Money inputPerOutput =
                    recipe.inputs[static_cast<std::size_t>(good)] /
                    recipe.outputPerBatch;
                if (inputPerOutput <= Money(0)) continue;
                const std::vector<std::uint64_t> keys =
                    sourceKeysFor(recipe.warehouseId, good);
                if (keys.empty()) continue;
                const Money share = demand * inputPerOutput /
                                    Money(static_cast<int>(keys.size()));
                for (const std::uint64_t sourceKey : keys)
                    next[sourceKey] += share;
            }
        }
        Money largestDelta = Money(0);
        for (const auto& [key, value] : next) {
            const auto old = productionForecastByKey.find(key);
            const Money previous = old == productionForecastByKey.end()
                ? Money(0) : old->second;
            largestDelta = std::max(largestDelta,
                                    Money(std::abs((value - previous).toDouble())));
        }
        productionForecastByKey = std::move(next);
        if (largestDelta <= 1.0e-7) break;
    }
    // Keep a modest reserve above the exact fixed point.  The construction
    // demand graph is nearly balanced by design, so rounding and one-cycle
    // transit gaps would otherwise drain a producer's input buffer forever.
    for (auto& entry : productionForecastByKey)
        entry.second *= Money(PRODUCTION_FORECAST_SAFETY_FACTOR);

}


void WarehouseNetwork::updateProductionCommands() {
    constexpr int kBacklogDrainWeeks = CONSTRUCTION_BACKLOG_DRAIN_WEEKS;
    for (const ProductionRecipe& recipe : producerList) {
        const std::uint64_t key = productionOrderKey(
            recipe.warehouseId, recipe.buildingType, recipe.outputGood);
        ProductionControlState& control = productionControlByKey[key];
        if (control.updatedCycle == cycle) continue;

        Money arrivals = Money(0);
        Money pending = Money(0);
        const auto active = activeProductionOrderIdsByKey.find(key);
        if (active != activeProductionOrderIdsByKey.end()) {
            for (const WarehouseOrderId orderId : active->second) {
                const auto orderIndex = orderIndexById.find(orderId);
                if (orderIndex == orderIndexById.end()) continue;
                const WarehouseOrder& production =
                    orderList[orderIndex->second];
                if (terminal(production.status) ||
                    production.eligibleCycle > cycle) {
                    continue;
                }
                const Money outstanding = std::max(
                    Money(0), production.requested - production.received);
                pending += outstanding;
                if (production.eligibleCycle == cycle)
                    arrivals += outstanding;
            }
        }

        // One sample is admitted per logistics cycle and empty weeks remain
        // in the denominator. This is the exact 52-week constant-demand
        // signal seen by production, rather than a same-week stock reaction.
        control.demand52 = control.demandFilter.update(
            arrivals, 1.0, true);
        const Money backlogRate = pending / Money(kBacklogDrainWeeks);
        // Forecast orders represent a persistent weekly baseline. They must
        // not be drained at the backlog rate, otherwise a 900-unit
        // construction input plan only releases 225 units per cycle and the
        // entire chain oscillates between overstock and starvation.
        const Money forecast = productionForecastDemand(
            recipe.warehouseId, recipe.buildingType, recipe.outputGood);
        const InventoryState& output = warehouseStock(
            recipe.warehouseId, recipe.outputGood);
        const Money baseline = std::max(
            {control.demand52, forecast,
             std::max(Money(0), output.policy.weeklyDemand)});
        const Money inventoryCorrection =
            (output.policy.targetStock - output.position()) /
            Money(PRODUCTION_INVENTORY_RECOVERY_WEEKS);
        const Money inventoryPlan =
            std::max(Money(0), baseline + inventoryCorrection);
        const Money desired = std::max(inventoryPlan, backlogRate);
        control.command = std::min(
            recipe.maxOutputPerCycle, desired);
        if (control.command <= Money(1e-12))
            control.command = Money(0);
        control.updatedCycle = cycle;
    }
}

Money WarehouseNetwork::productionDemand52(
    WarehouseId warehouseId, int buildingType, int outputGood) const {
    const auto control = productionControlByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    return control == productionControlByKey.end()
        ? Money(0) : std::max(Money(0), control->second.demand52);
}

int WarehouseNetwork::productionCommandCycle(
    WarehouseId warehouseId, int buildingType, int outputGood) const {
    const auto control = productionControlByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    return control == productionControlByKey.end()
        ? -1 : control->second.updatedCycle;
}

