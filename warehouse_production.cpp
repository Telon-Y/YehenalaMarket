#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

bool WarehouseNetwork::extendSupplierProduction(
    WarehouseOrder& production, Money additional) {
    additional = nonNegative(additional);
    if (additional <= Money(1e-9) ||
        production.kind != WarehouseOrderKind::SupplierProduction ||
        terminal(production.status)) {
        return false;
    }
    const ProductionRecipe* recipe = findProducer(
        production.buyerWarehouseId, production.goodIndex);
    if (recipe == nullptr ||
        recipe->buildingType != production.buildingType) {
        return false;
    }

    if (production.productionPlanned) {
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (recipe->inputs[static_cast<std::size_t>(good)] <= Money(0))
                continue;
            const InventoryPolicy& policy =
                buildingStock(production.buyerWarehouseId,
                              production.buildingType, good).policy;
            // A production order with ad hoc material-demand children cannot
            // grow without growing those children too. Keep the incremental
            // child fallback for that standalone/raw-network case.
            if (policy.targetStock <= Money(0) &&
                policy.reorderPoint <= Money(0)) {
                return false;
            }
        }
    }

    const Money previousRequested = production.requested;
    production.requested = nonNegative(
        production.requested + additional);
    const Money growth = production.requested - previousRequested;
    if (growth <= Money(1e-9)) return false;
    production.accepted = production.requested;
    if (production.productionPlanned) {
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money perBatch =
                recipe->inputs[static_cast<std::size_t>(good)];
            if (perBatch <= Money(0)) continue;
            production.inputRequired[static_cast<std::size_t>(good)] +=
                growth * perBatch / recipe->outputPerBatch;
        }
    }
    syncOpenOrderIndex(production);
    touchRevision();
    return true;
}

void WarehouseNetwork::processProductionOrders() {
    touchRevision();
    struct BuildingReplenishmentPlan {
        WarehouseId warehouseId = -1;
        int buildingType = -1;
        int goodIndex = -1;
        std::string key;
        WarehouseOrderId rootDemandId = NO_WAREHOUSE_ORDER;
        WarehouseOrderId parentOrderId = NO_WAREHOUSE_ORDER;
    };
    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    std::vector<WarehouseOrder> inputOrders;
    std::vector<BuildingReplenishmentPlan> replenishmentPlans;
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* productionPtr = findMutableOrder(orderId);
        if (productionPtr == nullptr) continue;
        WarehouseOrder& production = *productionPtr;
        if (production.kind != WarehouseOrderKind::SupplierProduction ||
            terminal(production.status) ||
            production.eligibleCycle > cycle) {
            continue;
        }
        const ProductionRecipe* recipe = findProducer(
            production.buyerWarehouseId, production.goodIndex);
        if (recipe == nullptr ||
            recipe->buildingType != production.buildingType) {
            production.status = WarehouseOrderStatus::AwaitingSupply;
            continue;
        }
        if (production.requested - production.received <= Money(1e-9)) {
            production.received = production.requested;
            production.shipped = production.accepted;
            recomputeOrderStatus(production);
            continue;
        }

        if (!production.productionPlanned) {
            for (int good = 0; good < NUM_GOODS; ++good) {
                const Money perBatch =
                    recipe->inputs[static_cast<std::size_t>(good)];
                if (perBatch <= Money(0)) continue;
                const Money required =
                    production.requested * perBatch /
                    recipe->outputPerBatch;
                production.inputRequired[static_cast<std::size_t>(good)] =
                    required;
                const InventoryState& input = buildingStock(
                    production.buyerWarehouseId,
                    production.buildingType, good);
                if (input.policy.targetStock > Money(0) ||
                    input.policy.reorderPoint > Money(0)) {
                    continue;
                }
                BuildingMaterialRequest inputRequest;
                inputRequest.idempotencyKey =
                    "production-input:" + std::to_string(production.id) +
                    ":" + std::to_string(good);
                inputRequest.warehouseId = production.buyerWarehouseId;
                inputRequest.buildingType = production.buildingType;
                inputRequest.goodIndex = good;
                inputRequest.quantity = required;

                WarehouseOrder inputOrder;
                inputOrder.rootDemandId = production.rootDemandId;
                inputOrder.parentOrderId = production.id;
                inputOrder.idempotencyKey = inputRequest.idempotencyKey;
                inputOrder.kind = WarehouseOrderKind::BuildingMaterialDemand;
                inputOrder.status = WarehouseOrderStatus::PendingLocalAllocation;
                inputOrder.createdCycle = cycle;
                inputOrder.eligibleCycle = cycle + 1;
                inputOrder.buyerWarehouseId = inputRequest.warehouseId;
                inputOrder.buildingType = inputRequest.buildingType;
                inputOrder.goodIndex = inputRequest.goodIndex;
                inputOrder.requested = inputRequest.quantity;
                inputOrders.push_back(std::move(inputOrder));
            }
            production.productionPlanned = true;
            production.status = WarehouseOrderStatus::AwaitingSupply;
        }
        for (int good = 0; good < NUM_GOODS; ++good) {
            InventoryState& input = buildingStock(
                production.buyerWarehouseId,
                production.buildingType, good);
            Money& orderReserved =
                production.inputReserved[static_cast<std::size_t>(good)];
            if (input.policy.targetStock > Money(0) ||
                input.policy.reorderPoint > Money(0)) {
                // Base-stock inputs are a shared producer buffer. Reservations
                // belong to the replenishment buffer, not to one production
                // order; stale order reservations otherwise make a cyclic
                // recipe permanently report zero available material after a
                // policy transition.
                input.reserved = Money(0);
                orderReserved = Money(0);
                continue;
            }
            const Money outstanding = std::max(
                Money(0),
                production.inputRequired[static_cast<std::size_t>(good)] -
                    production.inputConsumed[static_cast<std::size_t>(good)] -
                    orderReserved);
            if (outstanding <= Money(0)) continue;
            const Money reserved = std::min(input.available(), outstanding);
            input.reserved += reserved;
            orderReserved += reserved;
        }
        production.status = WarehouseOrderStatus::AwaitingSupply;
    }
    recomputeBacklogViews();
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* productionPtr = findMutableOrder(orderId);
        if (productionPtr == nullptr) continue;
        WarehouseOrder& production = *productionPtr;
        if (production.kind != WarehouseOrderKind::SupplierProduction ||
            terminal(production.status) ||
            production.eligibleCycle > cycle ||
            !production.productionPlanned) {
            continue;
        }
        for (int good = 0; good < NUM_GOODS; ++good) {
            const InventoryState& input = buildingStock(
                production.buyerWarehouseId,
                production.buildingType, good);
            if (production.inputRequired[static_cast<std::size_t>(good)] <=
                    Money(0) ||
                (input.policy.targetStock <= Money(0) &&
                 input.policy.reorderPoint <= Money(0))) {
                continue;
            }
            replenishmentPlans.push_back({
                production.buyerWarehouseId,
                production.buildingType,
                good,
                "production-buffer:" + std::to_string(production.id) + ":" +
                    std::to_string(good) + ":cycle:" +
                    std::to_string(cycle),
                production.rootDemandId,
                production.id});
        }
    }
    for (const BuildingReplenishmentPlan& plan : replenishmentPlans) {
        planBuildingReplenishment(
            plan.warehouseId, plan.buildingType, plan.goodIndex, plan.key,
            plan.rootDemandId, plan.parentOrderId);
    }
    for (WarehouseOrder& inputOrder : inputOrders)
        createOrder(std::move(inputOrder), false);
    recomputeBacklogViews();
    updateProductionCommands();
}

Money WarehouseNetwork::pendingProduction(WarehouseId warehouseId,
                                          int buildingType,
                                          int outputGood) const {
    const auto index = activeProductionOrderIdsByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    if (index == activeProductionOrderIdsByKey.end()) return Money(0);
    Money total = Money(0);
    for (const WarehouseOrderId orderId : index->second) {
        const auto orderIndex = orderIndexById.find(orderId);
        if (orderIndex == orderIndexById.end()) continue;
        const WarehouseOrder& production = orderList[orderIndex->second];
        if (production.eligibleCycle > cycle) continue;
        total += std::max(Money(0), production.requested -
                                      production.received);
    }
    return total;
}

Money WarehouseNetwork::productionCommand(WarehouseId warehouseId,
                                          int buildingType,
                                          int outputGood) const {
    const auto control = productionControlByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    if (control == productionControlByKey.end()) {
        return Money(0);
    }
    return std::max(Money(0), control->second.command);
}

Money WarehouseNetwork::productionInputAvailability(
    WarehouseId warehouseId, int buildingType, int goodIndex) const {
    if (!hasWarehouse(warehouseId) || !validBuilding(buildingType) ||
        !validGood(goodIndex)) {
        return Money(0);
    }
    const InventoryState& input = buildingStock(
        warehouseId, buildingType, goodIndex);
    Money committedToProducer = Money(0);
    for (const WarehouseOrderId orderId : openOrderIds) {
        const auto orderIndex = orderIndexById.find(orderId);
        if (orderIndex == orderIndexById.end()) continue;
        const WarehouseOrder& production = orderList[orderIndex->second];
        if (production.kind != WarehouseOrderKind::SupplierProduction ||
            terminal(production.status) ||
            production.eligibleCycle > cycle ||
            production.buyerWarehouseId != warehouseId ||
            production.buildingType != buildingType) {
            continue;
        }
        committedToProducer += std::max(
            Money(0), production.inputReserved[
                static_cast<std::size_t>(goodIndex)]);
    }
    // Reservations on a producer's dedicated input buffer belong to that
    // producer and are consumed by completeProduction. Include them in its
    // material check without making them available to unrelated consumers.
    return std::min(input.onHand,
                    input.available() + committedToProducer);
}

Money WarehouseNetwork::completeProduction(WarehouseId warehouseId,
                                           int buildingType,
                                           int outputGood,
                                           Money quantity) {
    const ProductionRecipe* recipe = findProducer(warehouseId, outputGood);
    if (recipe == nullptr || recipe->buildingType != buildingType)
        return Money(0);
    Money remaining = std::min(nonNegative(quantity),
                               recipe->maxOutputPerCycle);
    if (remaining <= Money(0)) return Money(0);
    const auto produceUncommitted = [&](
        Money requested) {
        Money output = std::max(Money(0), requested);
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money perBatch =
                recipe->inputs[static_cast<std::size_t>(good)];
            if (perBatch <= Money(0)) continue;
            const InventoryState& input = buildingStock(
                warehouseId, buildingType, good);
            output = std::min(
                output,
                input.available() * recipe->outputPerBatch / perBatch);
        }
        if (output <= Money(0)) return Money(0);
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money perBatch =
                recipe->inputs[static_cast<std::size_t>(good)];
            if (perBatch <= Money(0)) continue;
            consumeBuildingInput(
                warehouseId, buildingType, good,
                output * perBatch / recipe->outputPerBatch);
        }
        return output;
    };
    const auto activeIndex = activeProductionOrderIdsByKey.find(
        productionOrderKey(warehouseId, buildingType, outputGood));
    if (activeIndex == activeProductionOrderIdsByKey.end()) {
        const Money completed = produceUncommitted(remaining);
        if (completed > Money(0)) {
            addProductionOutput(warehouseId, outputGood, completed);
            touchRevision();
        }
        return completed;
    }
    // Status updates can remove IDs from the active set.  Copy only the
    // producer-local IDs once so iteration remains valid.
    std::vector<WarehouseOrderId> productionIds;
    productionIds.reserve(activeIndex->second.size());
    for (const WarehouseOrderId id : activeIndex->second)
        productionIds.push_back(id);
    std::sort(productionIds.begin(), productionIds.end());
    Money completed = Money(0);
    for (const WarehouseOrderId productionId : productionIds) {
        const auto orderIndex = orderIndexById.find(productionId);
        if (orderIndex == orderIndexById.end()) continue;
        WarehouseOrder& production = orderList[orderIndex->second];
        if (remaining <= Money(0)) break;
        if (production.kind != WarehouseOrderKind::SupplierProduction ||
            terminal(production.status) ||
            production.buyerWarehouseId != warehouseId ||
            production.buildingType != buildingType ||
            production.goodIndex != outputGood ||
            production.eligibleCycle > cycle) {
            continue;
        }
        auto backlogContribution = [this](const WarehouseOrder& value,
                                          int good) {
            if (terminal(value.status) ||
                value.kind != WarehouseOrderKind::SupplierProduction ||
                !value.productionPlanned || value.eligibleCycle > cycle) {
                return Money(0);
            }
            const InventoryPolicy& policy =
                buildingStock(value.buyerWarehouseId,
                              value.buildingType, good).policy;
            if (policy.targetStock > Money(0) ||
                policy.reorderPoint > Money(0)) {
                return Money(0);
            }
            return std::max(
                Money(0),
                value.inputRequired[static_cast<std::size_t>(good)] -
                    value.inputConsumed[static_cast<std::size_t>(good)] -
                    value.inputReserved[static_cast<std::size_t>(good)]);
        };
        std::array<Money, NUM_GOODS> previousBacklog{};
        for (int good = 0; good < NUM_GOODS; ++good)
            previousBacklog[static_cast<std::size_t>(good)] =
                backlogContribution(production, good);
        auto syncBacklog = [&]() {
            for (int good = 0; good < NUM_GOODS; ++good) {
                const std::size_t index = static_cast<std::size_t>(good);
                const Money currentBacklog =
                    backlogContribution(production, good);
                const Money delta = currentBacklog - previousBacklog[index];
                if (delta != Money(0)) {
                    InventoryState& input =
                        buildingStock(warehouseId, buildingType, good);
                    input.backlog = nonNegative(input.backlog + delta);
                    previousBacklog[index] = currentBacklog;
                }
            }
        };
        const Money outstanding = std::max(
            Money(0), production.requested - production.received);
        Money current = std::min(remaining, outstanding);
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money perBatch =
                recipe->inputs[static_cast<std::size_t>(good)];
            if (perBatch <= Money(0)) continue;
            InventoryState& input =
                buildingStock(warehouseId, buildingType, good);
            const bool managedBuffer =
                input.policy.targetStock > Money(0) ||
                input.policy.reorderPoint > Money(0);
            if (managedBuffer) {
                const Money possibleOutput =
                    input.available() * recipe->outputPerBatch / perBatch;
                current = std::min(current, possibleOutput);
                continue;
            }
            const Money inputOutstanding = std::max(
                Money(0),
                production.inputRequired[static_cast<std::size_t>(good)] -
                    production.inputConsumed[static_cast<std::size_t>(good)] -
                    production.inputReserved[static_cast<std::size_t>(good)]);
            const Money newlyReserved =
                std::min(input.available(), inputOutstanding);
            input.reserved += newlyReserved;
            production.inputReserved[static_cast<std::size_t>(good)] +=
                newlyReserved;
            const Money possibleOutput =
                production.inputReserved[static_cast<std::size_t>(good)] *
                recipe->outputPerBatch / perBatch;
            current = std::min(current, possibleOutput);
        }
        syncBacklog();
        if (current <= Money(0)) continue;
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money perBatch =
                recipe->inputs[static_cast<std::size_t>(good)];
            if (perBatch <= Money(0)) continue;
            Money& reservedInput =
                production.inputReserved[static_cast<std::size_t>(good)];
            Money& consumedInput =
                production.inputConsumed[static_cast<std::size_t>(good)];
            const Money remainingRequirement = std::max(
                Money(0),
                production.inputRequired[static_cast<std::size_t>(good)] -
                    consumedInput);
            InventoryState& input =
                buildingStock(warehouseId, buildingType, good);
            const bool managedBuffer =
                input.policy.targetStock > Money(0) ||
                input.policy.reorderPoint > Money(0);
            Money used = std::min(
                current * perBatch / recipe->outputPerBatch,
                remainingRequirement);
            if (managedBuffer) {
                used = consumeBuildingInput(
                    warehouseId, buildingType, good, used);
            } else {
                used = std::min(used, reservedInput);
                input.onHand = std::max(Money(0), input.onHand - used);
                input.reserved =
                    std::max(Money(0), input.reserved - used);
                input.reserved = std::min(input.reserved, input.onHand);
                reservedInput =
                    std::max(Money(0), reservedInput - used);
                currentFlow(warehouseId)
                    .buildingConsumed[static_cast<std::size_t>(good)] +=
                    used;
            }
            consumedInput = std::min(
                production.inputRequired[static_cast<std::size_t>(good)],
                consumedInput + used);
            const Money requiredInput =
                production.inputRequired[static_cast<std::size_t>(good)];
            const Money inputRemaining =
                std::max(Money(0), requiredInput - consumedInput);
            if (inputRemaining <= Money(1e-12)) {
                consumedInput = requiredInput;
                reservedInput = Money(0);
            } else {
                reservedInput = std::min(
                    reservedInput, inputRemaining);
            }
        }
        production.shipped = std::min(
            production.accepted, production.shipped + current);
        production.received = std::min(
            production.accepted, production.received + current);
        if (production.requested - production.received <= Money(1e-9)) {
            production.received = production.requested;
            production.shipped = production.accepted;
        }
        if (production.parentOrderId != NO_WAREHOUSE_ORDER) {
            WarehouseOrder* parent =
                findMutableOrder(production.parentOrderId);
            if (parent != nullptr &&
                parent->kind == WarehouseOrderKind::WarehouseReplenishment &&
                parent->buyerWarehouseId == warehouseId) {
                parent->received += current;
                InventoryState& destination =
                    warehouseStock(warehouseId, outputGood);
                destination.confirmedInbound = std::max(
                    Money(0), destination.confirmedInbound - current);
                recomputeOrderStatus(*parent);
            } else if (parent != nullptr &&
                       (parent->kind ==
                            WarehouseOrderKind::RemotePurchase ||
                        parent->kind ==
                            WarehouseOrderKind::WarehouseReplenishment) &&
                       parent->sellerWarehouseId == warehouseId) {
                const Money reservable = std::min(
                    current,
                    std::max(
                        Money(0), parent->accepted - parent->shipped -
                                      parent->reserved));
                parent->reserved += reservable;
                warehouseStock(warehouseId, outputGood).reserved +=
                    reservable;
                recomputeOrderStatus(*parent);
            }
        }
        completed += current;
        remaining -= current;
        recomputeOrderStatus(production);
        syncBacklog();
    }
    if (remaining > Money(0)) {
        const Money uncommitted = produceUncommitted(remaining);
        completed += uncommitted;
        remaining -= uncommitted;
    }
    if (completed > Money(0)) {
        addProductionOutput(warehouseId, outputGood, completed);
        touchRevision();
    }
    return completed;
}

void WarehouseNetwork::finishCycle() {
    completedRouteUsage.swap(currentRouteUsage);
    currentRouteUsage.clear();
    completedRailwayRevenue.swap(currentRailwayRevenue);
    currentRailwayRevenue.clear();
    completedWarehouseProfit.swap(currentWarehouseProfit);
    currentWarehouseProfit.clear();
    completedUsageCycle = cycle;
    completedFlowByWarehouse.clear();
    for (const auto& [warehouseId, warehouse] : warehouses) {
        (void)warehouse;
        WarehouseCycleFlow completed = currentFlowByWarehouse[warehouseId];
        completed.cycle = cycle;
        completedFlowByWarehouse[warehouseId] = completed;
        currentFlowByWarehouse[warehouseId] = WarehouseCycleFlow{};
        currentFlowByWarehouse[warehouseId].cycle = cycle + 1;
    }
    completedTradeFlowsThisWindow.erase(
        std::remove_if(completedTradeFlowsThisWindow.begin(),
                       completedTradeFlowsThisWindow.end(),
                       [this](const WarehouseTradeFlow& flow) {
                           return flow.cycle < cycle - 51;
                       }),
        completedTradeFlowsThisWindow.end());
    ++cycle;
    touchRevision();
    recomputeBacklogViews();
}

void WarehouseNetwork::beginLogisticsBatch() {
    ++logisticsBatchDepth;
}

void WarehouseNetwork::endLogisticsBatch() {
    if (logisticsBatchDepth <= 0) return;
    --logisticsBatchDepth;
    if (logisticsBatchDepth == 0) recomputeBacklogViews();
}

void WarehouseNetwork::runLogisticsCycle() {
    beginLogisticsBatch();
    receive();
    refreshProductionForecasts();
    processProductionOrders();
    allocateLocal();
    routeShortages();
    confirmAndReserve();
    dispatch();
    advanceTransit();
    finishCycle();
    endLogisticsBatch();
}
Money WarehouseNetwork::consumeBuildingInput(WarehouseId warehouseId,
                                              int buildingType,
                                              int goodIndex,
                                              Money quantity) {
    InventoryState& input = buildingStock(warehouseId, buildingType, goodIndex);
    const Money consumed = std::min(input.available(),
                                    nonNegative(quantity));
    input.onHand -= consumed;
    currentFlow(warehouseId)
        .buildingConsumed[static_cast<std::size_t>(goodIndex)] += consumed;
    return consumed;
}

void WarehouseNetwork::addProductionOutput(WarehouseId warehouseId,
                                            int goodIndex,
                                            Money quantity) {
    if (goodIndex == CONSTR_GOOD_INDEX ||
        goodIndex == TRANSPORT_CAPACITY_GOOD_INDEX) return;
    const Money produced = nonNegative(quantity);
    InventoryState& output = warehouseStock(warehouseId, goodIndex);
    output.onHand += produced;
    currentFlow(warehouseId)
        .produced[static_cast<std::size_t>(goodIndex)] += produced;

    // Logistics allocation runs before market production. Reserve newly
    // completed output for already-open local building-input commitments now,
    // otherwise same-cycle consumer withdrawals can repeatedly drain the
    // stock before the next allocation phase and starve the production chain.
    const std::vector<WarehouseOrderId> phaseOrderIds(
        openOrderIds.begin(), openOrderIds.end());
    for (const WarehouseOrderId orderId : phaseOrderIds) {
        WarehouseOrder* demandPtr = findMutableOrder(orderId);
        if (demandPtr == nullptr) continue;
        WarehouseOrder& demand = *demandPtr;
        if (demand.kind != WarehouseOrderKind::BuildingMaterialDemand ||
            terminal(demand.status) || demand.eligibleCycle > cycle ||
            demand.buyerWarehouseId != warehouseId ||
            demand.goodIndex != goodIndex) {
            continue;
        }
        const Money stillNeeded = std::max(
            Money(0), demand.requested - demand.accepted);
        const Money local = std::min(output.available(), stillNeeded);
        if (local <= Money(0)) continue;
        output.reserved += local;
        demand.locallyAllocated += local;
        demand.reserved += local;
        demand.accepted += local;
        buildingStock(warehouseId, demand.buildingType, goodIndex)
            .confirmedInbound += local;
        recomputeOrderStatus(demand);
    }
}
