#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

void WarehouseNetwork::recomputeOrderStatus(WarehouseOrder& orderValue) {
    if (orderValue.status == WarehouseOrderStatus::Cancelled) {
        releaseEpisodeIfTerminal(orderValue);
        syncOpenOrderIndex(orderValue);
        return;
    }
    const Money completionTarget =
        orderValue.kind == WarehouseOrderKind::WarehouseReplenishment ||
                orderValue.kind == WarehouseOrderKind::RemotePurchase
            ? orderValue.accepted
            : orderValue.requested;
    if (completionTarget > Money(0) &&
        orderValue.received + Money(1e-9) >= completionTarget) {
        orderValue.received = completionTarget;
        orderValue.status = WarehouseOrderStatus::Fulfilled;
        releaseEpisodeIfTerminal(orderValue);
    } else if (orderValue.received > Money(0)) {
        orderValue.status = WarehouseOrderStatus::PartiallyFulfilled;
    } else if (orderValue.shipped > Money(0)) {
        orderValue.status = WarehouseOrderStatus::InTransit;
    } else if (orderValue.accepted >= orderValue.requested) {
        orderValue.status = WarehouseOrderStatus::Confirmed;
    } else if (orderValue.accepted > Money(0) ||
               orderValue.locallyAllocated > Money(0)) {
        orderValue.status = WarehouseOrderStatus::PartiallyConfirmed;
    } else {
        orderValue.status = WarehouseOrderStatus::AwaitingSupply;
    }
    syncOpenOrderIndex(orderValue);
}

void WarehouseNetwork::syncOpenOrderIndex(const WarehouseOrder& orderValue) {
    if (terminal(orderValue.status)) {
        openOrderIds.erase(orderValue.id);
        if (orderValue.kind == WarehouseOrderKind::SupplierProduction &&
            validBuilding(orderValue.buildingType) &&
            validGood(orderValue.goodIndex)) {
            const std::uint64_t key = productionOrderKey(
                orderValue.buyerWarehouseId, orderValue.buildingType,
                orderValue.goodIndex);
            auto it = activeProductionOrderIdsByKey.find(key);
            if (it != activeProductionOrderIdsByKey.end()) {
                it->second.erase(orderValue.id);
                if (it->second.empty())
                    activeProductionOrderIdsByKey.erase(it);
            }
        }
    } else {
        openOrderIds.insert(orderValue.id);
        if (orderValue.kind == WarehouseOrderKind::SupplierProduction &&
            validBuilding(orderValue.buildingType) &&
            validGood(orderValue.goodIndex)) {
            activeProductionOrderIdsByKey[
                productionOrderKey(orderValue.buyerWarehouseId,
                                    orderValue.buildingType,
                                    orderValue.goodIndex)].insert(
                                        orderValue.id);
        }
    }
}

void WarehouseNetwork::releaseEpisodeIfTerminal(
    WarehouseOrder& orderValue) {
    if (!terminal(orderValue.status)) return;

    if (orderValue.escrowed > Money(0) &&
        orderValue.contractPrice > Money(0)) {
        refundEscrow(orderValue,
                     orderValue.escrowed / orderValue.contractPrice);
    }


    // Reservations are physical claims. A terminal order must not leave any
    // of them behind, including when a cancellation or quantity tolerance
    // closes an order before the normal dispatch/receipt path reaches zero.
    if (orderValue.reserved > Money(0)) {
        InventoryState* reservedStock = nullptr;
        if (orderValue.kind == WarehouseOrderKind::BuildingMaterialDemand &&
            orderValue.buyerWarehouseId >= 0 &&
            validGood(orderValue.goodIndex)) {
            reservedStock = &warehouseStock(orderValue.buyerWarehouseId,
                                            orderValue.goodIndex);
        } else if ((orderValue.kind == WarehouseOrderKind::RemotePurchase ||
                    orderValue.kind ==
                        WarehouseOrderKind::WarehouseReplenishment) &&
                   orderValue.sellerWarehouseId >= 0 &&
                   validGood(orderValue.goodIndex)) {
            reservedStock = &warehouseStock(orderValue.sellerWarehouseId,
                                            orderValue.goodIndex);
        }
        if (reservedStock != nullptr) {
            reservedStock->reserved = std::max(
                Money(0), reservedStock->reserved - orderValue.reserved);
            reservedStock->reserved = std::min(
                reservedStock->reserved, reservedStock->onHand);
        }
        orderValue.reserved = Money(0);
    }

    if (orderValue.kind == WarehouseOrderKind::SupplierProduction &&
        orderValue.buyerWarehouseId >= 0 &&
        validBuilding(orderValue.buildingType)) {
        for (int good = 0; good < NUM_GOODS; ++good) {
            Money& reserved = orderValue.inputReserved[
                static_cast<std::size_t>(good)];
            if (reserved <= Money(0)) continue;
            InventoryState& input = buildingStock(
                orderValue.buyerWarehouseId, orderValue.buildingType, good);
            input.reserved = std::max(Money(0), input.reserved - reserved);
            input.reserved = std::min(input.reserved, input.onHand);
            reserved = Money(0);
        }
    }
    std::string episode;
    if (orderValue.kind == WarehouseOrderKind::WarehouseReplenishment) {
        episode = warehouseEpisodeKey(orderValue.buyerWarehouseId,
                                      orderValue.goodIndex);
    } else if (orderValue.kind == WarehouseOrderKind::BuildingMaterialDemand) {
        episode = buildingEpisodeKey(orderValue.buyerWarehouseId,
                                     orderValue.buildingType,
                                     orderValue.goodIndex);
    }
    if (episode.empty()) return;
    auto it = activeEpisodeByKey.find(episode);
    if (it != activeEpisodeByKey.end() && it->second == orderValue.id)
        activeEpisodeByKey.erase(it);
}

void WarehouseNetwork::recomputeBacklogViews() {
    if (logisticsBatchDepth > 0) return;
    ++backlogRebuilds;
    for (auto& [id, record] : warehouses) {
        (void)id;
        for (InventoryState& state : record->goods) state.backlog = Money(0);
        for (auto& byGood : record->buildingInputs)
            for (InventoryState& state : byGood) state.backlog = Money(0);
    }
    // Aggregate each active order once.  Production input backlog is folded
    // into the same pass as warehouse/building commitments.
    for (const WarehouseOrderId orderId : openOrderIds) {
        const auto index = orderIndexById.find(orderId);
        if (index == orderIndexById.end()) continue;
        const WarehouseOrder& value = orderList[index->second];
        if (terminal(value.status)) continue;

        const Money outstanding = std::max(Money(0),
                                           value.requested - value.received);
        if (outstanding > Money(0)) {
            if (value.kind == WarehouseOrderKind::BuildingMaterialDemand) {
                const Money unreservedCommitment = std::max(
                    Money(0), value.accepted - value.received - value.reserved);
                warehouseStock(value.buyerWarehouseId, value.goodIndex).backlog +=
                    unreservedCommitment;
            } else if ((value.kind == WarehouseOrderKind::WarehouseReplenishment ||
                        value.kind == WarehouseOrderKind::RemotePurchase) &&
                       value.sellerWarehouseId >= 0 &&
                       !value.productionPlanned) {
                // Once a child production/purchase order exists, that supply
                // already covers the outbound commitment. Counting it again
                // in the seller's stock position creates duplicate demand.
                const Money unreservedCommitment = std::max(
                    Money(0), value.accepted - value.shipped - value.reserved);
                warehouseStock(value.sellerWarehouseId, value.goodIndex).backlog +=
                    unreservedCommitment;
            }
        }

        if (value.kind != WarehouseOrderKind::SupplierProduction ||
            !value.productionPlanned || value.eligibleCycle > cycle) {
            continue;
        }
        for (int good = 0; good < NUM_GOODS; ++good) {
            InventoryState& input = buildingStock(
                value.buyerWarehouseId, value.buildingType, good);
            if (input.policy.targetStock > Money(0) ||
                input.policy.reorderPoint > Money(0)) {
                continue;
            }
            const Money inputOutstanding = std::max(
                Money(0),
                value.inputRequired[static_cast<std::size_t>(good)] -
                    value.inputConsumed[static_cast<std::size_t>(good)] -
                    value.inputReserved[static_cast<std::size_t>(good)]);
            input.backlog += inputOutstanding;
        }
    }
}
std::vector<const WarehouseOrder*> WarehouseNetwork::ordersForRoot(
    WarehouseOrderId rootDemandId) const {
    std::vector<const WarehouseOrder*> result;
    const auto range = orderIdsByRoot.equal_range(rootDemandId);
    for (auto it = range.first; it != range.second; ++it) {
        const auto index = orderIndexById.find(it->second);
        if (index != orderIndexById.end())
            result.push_back(&orderList[index->second]);
    }
    std::sort(result.begin(), result.end(),
              [](const WarehouseOrder* left, const WarehouseOrder* right) {
                  return left->id < right->id;
              });
    return result;
}

std::vector<const WarehouseOrder*> WarehouseNetwork::childrenOf(
    WarehouseOrderId parentOrderId) const {
    std::vector<const WarehouseOrder*> result;
    const auto range = orderIdsByParent.equal_range(parentOrderId);
    for (auto it = range.first; it != range.second; ++it) {
        const auto index = orderIndexById.find(it->second);
        if (index != orderIndexById.end())
            result.push_back(&orderList[index->second]);
    }
    std::sort(result.begin(), result.end(),
              [](const WarehouseOrder* left, const WarehouseOrder* right) {
                  return left->id < right->id;
              });
    return result;
}

int WarehouseNetwork::activeOrders(WarehouseId buyerWarehouseId,
                                   int goodIndex) const {
    int count = 0;
    const auto range = orderIdsByWarehouseGood.equal_range(
        warehouseGoodKey(buyerWarehouseId, goodIndex));
    for (auto it = range.first; it != range.second; ++it) {
        const auto index = orderIndexById.find(it->second);
        if (index != orderIndexById.end() &&
            !terminal(orderList[index->second].status)) {
            ++count;
        }
    }
    return count;
}

Money WarehouseNetwork::economicRootDemand(int goodIndex) const {
    Money total = Money(0);
    for (const WarehouseOrder& value : orderList) {
        if (value.id == value.rootDemandId && value.goodIndex == goodIndex &&
            (value.kind == WarehouseOrderKind::BuildingMaterialDemand ||
             value.kind == WarehouseOrderKind::WarehouseReplenishment)) {
            total += value.requested;
        }
    }
    return total;
}

Money WarehouseNetwork::physicalGoods(int goodIndex) const {
    if (!validGood(goodIndex)) throw std::out_of_range("good index out of range");
    if (goodIndex == CONSTR_GOOD_INDEX ||
        goodIndex == TRANSPORT_CAPACITY_GOOD_INDEX)
        return Money(0);
    Money total = Money(0);
    for (const auto& [id, record] : warehouses) {
        (void)id;
        total += record->stock(goodIndex).onHand;
        for (int building = 0; building < TYPE_COUNT; ++building) {
            total += record->buildingInput(building, goodIndex).onHand;
        }
    }
    for (const Shipment& shipment : shipmentList)
        if (!shipment.delivered && shipment.goodIndex == goodIndex)
            total += shipment.cargo;
    return total;
}

WarehouseAudit WarehouseNetwork::audit() const {
    auto nearMoney = [](Money left, Money right) {
        const double a = left.toDouble();
        const double b = right.toDouble();
        const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
        return std::fabs(a - b) <= 1e-9 * scale;
    };
    const auto warehouseGoodKey = [](WarehouseId warehouseId, int good) {
        return (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(warehouseId)) << 32) |
               static_cast<std::uint32_t>(good);
    };
    const auto buildingGoodKey = [](WarehouseId warehouseId, int building,
                                    int good) {
        return (static_cast<std::uint64_t>(
                    static_cast<std::uint32_t>(warehouseId)) << 32) |
               (static_cast<std::uint64_t>(
                    static_cast<std::uint16_t>(building)) << 16) |
               static_cast<std::uint16_t>(good);
    };
    std::unordered_map<std::uint64_t, Money> expectedWarehouseReserved;
    std::unordered_map<std::uint64_t, Money> expectedWarehouseInbound;
    std::unordered_map<std::uint64_t, Money> expectedWarehouseTransit;
    std::unordered_map<std::uint64_t, Money> expectedBuildingReserved;
    std::unordered_map<std::uint64_t, Money> expectedBuildingInbound;
    std::unordered_map<std::uint64_t, Money> expectedBuildingBacklog;
    std::unordered_map<std::uint64_t, Money> expectedBuildingTransit;
    // Build all audit expectations in one order pass. The previous version
    // rescanned the full order list for every warehouse/good/building tuple.
    for (const WarehouseOrder& value : orderList) {
        if (terminal(value.status)) continue;
        if (validGood(value.goodIndex)) {
            const auto warehouseGood = warehouseGoodKey(
                value.sellerWarehouseId, value.goodIndex);
            if ((value.kind == WarehouseOrderKind::RemotePurchase ||
                 value.kind == WarehouseOrderKind::WarehouseReplenishment) &&
                value.sellerWarehouseId >= 0) {
                expectedWarehouseReserved[warehouseGood] += value.reserved;
            }
            if (value.kind == WarehouseOrderKind::BuildingMaterialDemand &&
                value.buyerWarehouseId >= 0) {
                expectedWarehouseReserved[warehouseGoodKey(
                    value.buyerWarehouseId, value.goodIndex)] += value.reserved;
            }
            if ((value.kind == WarehouseOrderKind::RemotePurchase ||
                 value.kind == WarehouseOrderKind::WarehouseReplenishment) &&
                value.buyerWarehouseId >= 0) {
                expectedWarehouseInbound[warehouseGoodKey(
                    value.buyerWarehouseId, value.goodIndex)] +=
                    std::max(Money(0), value.accepted - value.received);
            }
        }
        if (validBuilding(value.buildingType) && value.buyerWarehouseId >= 0) {
            if (value.kind == WarehouseOrderKind::BuildingMaterialDemand &&
                validGood(value.goodIndex)) {
                expectedBuildingInbound[buildingGoodKey(
                    value.buyerWarehouseId, value.buildingType,
                    value.goodIndex)] +=
                    std::max(Money(0), value.accepted - value.received);
            }
            if (value.kind == WarehouseOrderKind::SupplierProduction &&
                value.eligibleCycle <= cycle) {
                for (int good = 0; good < NUM_GOODS; ++good) {
                    const auto key = buildingGoodKey(
                        value.buyerWarehouseId, value.buildingType, good);
                    const InventoryPolicy& policy = buildingStock(
                        value.buyerWarehouseId, value.buildingType, good).policy;
                    if (policy.targetStock > Money(0) ||
                        policy.reorderPoint > Money(0)) {
                        continue;
                    }
                    expectedBuildingReserved[key] +=
                        value.inputReserved[static_cast<std::size_t>(good)];
                    expectedBuildingBacklog[key] += std::max(
                        Money(0),
                        value.inputRequired[static_cast<std::size_t>(good)] -
                            value.inputConsumed[static_cast<std::size_t>(good)] -
                            value.inputReserved[static_cast<std::size_t>(good)]);
                }
            }
        }
    }
    // A remote child of a building demand is physically committed to that
    // building from dispatch until the demand receipt phase moves it out of
    // the warehouse. Derive the view from cumulative order quantities so the
    // audit remains valid even when the remote child itself is terminal.
    for (const WarehouseOrder& demand : orderList) {
        if (demand.kind != WarehouseOrderKind::BuildingMaterialDemand ||
            demand.buyerWarehouseId < 0 ||
            !validBuilding(demand.buildingType) ||
            !validGood(demand.goodIndex)) {
            continue;
        }
        Money remoteShipped = Money(0);
        const auto childRange = orderIdsByParent.equal_range(demand.id);
        for (auto childIt = childRange.first;
             childIt != childRange.second; ++childIt) {
            const auto childIndex = orderIndexById.find(childIt->second);
            if (childIndex == orderIndexById.end()) continue;
            const WarehouseOrder& child = orderList[childIndex->second];
            if (child.kind == WarehouseOrderKind::RemotePurchase)
                remoteShipped += child.shipped;
        }
        const Money remoteDelivered = std::max(
            Money(0), demand.received - demand.locallyAllocated);
        expectedBuildingTransit[buildingGoodKey(
            demand.buyerWarehouseId, demand.buildingType,
            demand.goodIndex)] += std::max(
                Money(0), remoteShipped - remoteDelivered);
    }
    for (const Shipment& shipment : shipmentList) {
        if (!shipment.delivered && shipment.destinationWarehouseId >= 0 &&
            validGood(shipment.goodIndex)) {
            expectedWarehouseTransit[warehouseGoodKey(
                shipment.destinationWarehouseId, shipment.goodIndex)] +=
                shipment.cargo;
        }
    }
    for (const auto& [warehouseId, record] : warehouses) {
        auto validState = [&nearMoney](const InventoryState& state) {
            return isfinite(state.onHand) && isfinite(state.reserved) &&
                   isfinite(state.confirmedInbound) &&
                   isfinite(state.physicalInTransit) &&
                   isfinite(state.backlog) &&
                   isfinite(state.lastRawReplenishment) &&
                   isfinite(state.lastPlannedReplenishment) &&
                   isfinite(state.policy.targetStock) &&
                   isfinite(state.policy.reorderPoint) &&
                   isfinite(state.policy.weeklyDemand) &&
                   (state.onHand >= Money(0) ||
                    nearMoney(state.onHand, Money(0))) &&
                   (state.reserved >= Money(0) ||
                    nearMoney(state.reserved, Money(0))) &&
                   (state.reserved <= state.onHand ||
                    nearMoney(state.reserved, state.onHand)) &&
                   (state.confirmedInbound >= Money(0) ||
                    nearMoney(state.confirmedInbound, Money(0))) &&
                   (state.physicalInTransit >= Money(0) ||
                    nearMoney(state.physicalInTransit, Money(0))) &&
                   (state.backlog >= Money(0) ||
                     nearMoney(state.backlog, Money(0))) &&
                   (state.lastRawReplenishment >= Money(0) ||
                    nearMoney(state.lastRawReplenishment, Money(0))) &&
                   (state.lastPlannedReplenishment >= Money(0) ||
                    nearMoney(state.lastPlannedReplenishment, Money(0))) &&
                   (state.policy.targetStock >= Money(0) ||
                    nearMoney(state.policy.targetStock, Money(0))) &&
                   (state.policy.reorderPoint >= Money(0) ||
                    nearMoney(state.policy.reorderPoint, Money(0))) &&
                   (state.policy.weeklyDemand >= Money(0) ||
                    nearMoney(state.policy.weeklyDemand, Money(0)));
        };
        for (const InventoryState& state : record->goods)
            if (!validState(state)) return {false, "invalid warehouse stock"};
        for (int building = 0; building < TYPE_COUNT; ++building) {
            for (int good = 0; good < NUM_GOODS; ++good) {
                const InventoryState& state =
                    record->buildingInput(building, good);
                if (!validState(state)) {
                    return {
                        false,
                        "invalid building input stock warehouse=" +
                            std::to_string(warehouseId) + " building=" +
                            std::to_string(building) + " good=" +
                            std::to_string(good) + " onHand=" +
                            state.onHand.toString(18) + " reserved=" +
                            state.reserved.toString(18) + " inbound=" +
                            state.confirmedInbound.toString(18) +
                            " transit=" +
                            state.physicalInTransit.toString(18) +
                            " backlog=" + state.backlog.toString(18)};
                }
            }
        }

        for (int good = 0; good < NUM_GOODS; ++good) {
            const auto key = warehouseGoodKey(warehouseId, good);
            const Money expectedReserved = expectedWarehouseReserved[key];
            const Money expectedInbound = expectedWarehouseInbound[key];
            const Money expectedTransit = expectedWarehouseTransit[key];
            const InventoryState& state = record->stock(good);
            const auto detail = [warehouseId, good](
                const char* label, Money actual, Money expected) {
                return std::string(label) + " warehouse=" +
                    std::to_string(warehouseId) + " good=" +
                    std::to_string(good) + " actual=" +
                    std::to_string(actual.toDouble()) + " expected=" +
                    std::to_string(expected.toDouble());
            };
            if (!nearMoney(state.reserved, expectedReserved))
                return {false, detail("warehouse reservation view mismatch",
                                      state.reserved, expectedReserved)};
            if (!nearMoney(state.confirmedInbound, expectedInbound))
                return {false, detail(
                    "warehouse confirmed inbound mismatch",
                    state.confirmedInbound, expectedInbound)};
            if (!nearMoney(state.physicalInTransit, expectedTransit))
                return {false, detail("warehouse transit view mismatch",
                                      state.physicalInTransit,
                                      expectedTransit)};
        }
        for (int building = 0; building < TYPE_COUNT; ++building) {
            for (int good = 0; good < NUM_GOODS; ++good) {
                const auto key = buildingGoodKey(warehouseId, building, good);
                const Money expectedReserved = expectedBuildingReserved[key];
                const Money expectedInbound = expectedBuildingInbound[key];
                const Money expectedBacklog = expectedBuildingBacklog[key];
                const Money expectedTransit = expectedBuildingTransit[key];
                const InventoryState& input =
                    record->buildingInput(building, good);
                if (!nearMoney(input.reserved, expectedReserved))
                    return {false,
                            "building input reservation view mismatch"};
                if (!nearMoney(input.confirmedInbound, expectedInbound))
                    return {false,
                            "building input confirmed inbound mismatch"};
                if (!nearMoney(input.backlog, expectedBacklog))
                    return {false, "building input backlog view mismatch"};
                if (!nearMoney(input.physicalInTransit, expectedTransit))
                    return {false, "building input transit view mismatch"};
            }
        }
    }

    for (const auto& [warehouseId, reviewCount] :
         inventoryReviewsByWarehouse) {
        const auto reviewed = lastInventoryReviewByWarehouse.find(warehouseId);
        if (reviewed == lastInventoryReviewByWarehouse.end() ||
            reviewed->second < 0 || reviewed->second > cycle ||
            reviewCount > static_cast<std::uint64_t>(cycle + 1)) {
            return {false, "inventory review frequency invariant failed"};
        }
    }
    for (const SupplyRoute& route : routeList) {
        if (!std::isfinite(route.distanceKm) || route.distanceKm < 0.0 ||
            !isfinite(route.unitPrice) ||
            !isfinite(route.sourceUnitPrice) ||
            !isfinite(route.destinationUnitPrice) ||
            !isfinite(route.transportCostPerUnit) ||
            !isfinite(route.transportCapacityPerUnit) ||
            !isfinite(route.railwayCapacityPricePerUnit) ||
            !isfinite(route.railwayChargePerUnit) ||
            !isfinite(route.railwayMarkupPerUnit) ||
            !isfinite(route.warehouseMarginPerUnit) ||
            route.unitPrice < Money(0) ||
            route.transportCostPerUnit < Money(0) ||
            route.transportCapacityPerUnit < Money(0) ||
            route.railwayCapacityPricePerUnit < Money(0) ||
            route.railwayChargePerUnit < Money(0) ||
            route.railwayMarkupPerUnit < Money(0) ||
            route.warehouseMarginPerUnit < Money(0)) {
            return {false, "invalid route economics"};
        }
        if (!route.dynamicPricing) continue;
        if (!nearMoney(route.railwayMarkupPerUnit, Money(0)) ||
            !nearMoney(route.warehouseMarginPerUnit, Money(0))) {
            return {false, "legacy rail markup or warehouse margin is non-zero"};
        }
        if (!nearMoney(route.transportCostPerUnit,
                       route.transportCapacityPerUnit *
                           route.railwayCapacityPricePerUnit) ||
            !nearMoney(route.railwayChargePerUnit,
                       route.transportCapacityPerUnit *
                           route.railwayCapacityPricePerUnit)) {
            return {false, "rail capacity charge decomposition mismatch"};
        }
        if (!nearMoney(route.unitPrice,
                       route.sourceUnitPrice +
                           route.railwayChargePerUnit)) {
            return {false, "route contract price decomposition mismatch"};
        }
        if (route.profitable &&
            (!route.railwayAvailable ||
             route.destinationUnitPrice <=
                 route.sourceUnitPrice + route.railwayChargePerUnit)) {
            return {false, "unprofitable rail route marked tradable"};
        }
    }

    std::unordered_set<std::string> keys;
    Money expectedEscrow = Money(0);
    for (const WarehouseOrder& value : orderList) {
        if (value.rootDemandId == NO_WAREHOUSE_ORDER ||
            orderIndexById.find(value.rootDemandId) == orderIndexById.end())
            return {false, "order root is missing"};
        if (value.parentOrderId != NO_WAREHOUSE_ORDER &&
            orderIndexById.find(value.parentOrderId) == orderIndexById.end())
            return {false, "order parent is missing"};
        if (value.parentOrderId != NO_WAREHOUSE_ORDER &&
            value.eligibleCycle <= value.createdCycle)
            return {false, "derived order is eligible in its creation cycle"};
        if (!value.idempotencyKey.empty() &&
            !keys.insert(value.idempotencyKey).second)
            return {false, "duplicate idempotency key"};
        const bool indexedAsOpen =
            openOrderIds.find(value.id) != openOrderIds.end();
        if (indexedAsOpen == terminal(value.status))
            return {false, "active order index mismatch"};
        const auto quantityError = [&value](const char* invariant) {
            return std::string("invalid order cumulative quantities: ") +
                invariant + " order=" + std::to_string(value.id) +
                " requested=" + value.requested.toString(18) +
                " accepted=" + value.accepted.toString(18) +
                " reserved=" + value.reserved.toString(18) +
                " shipped=" + value.shipped.toString(18) +
                " received=" + value.received.toString(18) +
                " escrowed=" + value.escrowed.toString(18);
        };
        if (value.requested < Money(0) || value.accepted < Money(0) ||
            value.reserved < Money(0) || value.shipped < Money(0) ||
            value.received < Money(0) || value.escrowed < Money(0)) {
            return {false, quantityError("negative value")};
        }
        if (!isfinite(value.contractPrice) ||
            !isfinite(value.sourceUnitPrice) ||
            !isfinite(value.destinationUnitPrice) ||
            !isfinite(value.transportCostPerUnit) ||
            !isfinite(value.transportCapacityPerUnit) ||
            !isfinite(value.railwayCapacityPricePerUnit) ||
            !isfinite(value.railwayChargePerUnit) ||
            !isfinite(value.railwayMarkupPerUnit) ||
            !isfinite(value.warehouseMarginPerUnit)) {
            return {false, "non-finite order economics"};
        }
        if (value.routeId >= 0 && value.profitableTrade) {
            if (!nearMoney(value.railwayMarkupPerUnit, Money(0)) ||
                !nearMoney(value.warehouseMarginPerUnit, Money(0)) ||
                !nearMoney(value.contractPrice,
                           value.sourceUnitPrice +
                               value.railwayChargePerUnit)) {
                return {false, "order contract price decomposition mismatch"};
            }
            if (!nearMoney(value.transportCostPerUnit,
                           value.transportCapacityPerUnit *
                               value.railwayCapacityPricePerUnit) ||
                !nearMoney(value.railwayChargePerUnit,
                           value.transportCapacityPerUnit *
                               value.railwayCapacityPricePerUnit)) {
                return {false, "order rail capacity decomposition mismatch"};
            }
            const SupplyRoute* route = nullptr;
            for (const SupplyRoute& candidate : routeList)
                if (candidate.id == value.routeId) {
                    route = &candidate;
                    break;
                }
            if (route == nullptr)
                return {false, "order route is missing"};
            if (route->dynamicPricing &&
                value.destinationUnitPrice <=
                    value.sourceUnitPrice + value.railwayChargePerUnit) {
                return {false, "unprofitable remote order was confirmed"};
            }
        }
        if (value.accepted > value.requested &&
            !nearMoney(value.accepted, value.requested))
            return {false, quantityError("accepted > requested")};
        if (value.reserved + value.shipped > value.accepted &&
            !nearMoney(value.reserved + value.shipped, value.accepted))
            return {false, quantityError("reserved + shipped > accepted")};
        if (value.shipped > value.accepted &&
            !nearMoney(value.shipped, value.accepted))
            return {false, quantityError("shipped > accepted")};
        if (((value.kind == WarehouseOrderKind::WarehouseReplenishment &&
              value.routeId >= 0) ||
             value.kind == WarehouseOrderKind::RemotePurchase) &&
            value.received > value.shipped &&
            !nearMoney(value.received, value.shipped)) {
            return {false, quantityError("received > shipped")};
        }
        if ((value.kind == WarehouseOrderKind::BuildingMaterialDemand ||
             value.kind == WarehouseOrderKind::SupplierProduction) &&
            value.received > value.accepted &&
            !nearMoney(value.received, value.accepted)) {
            return {false, quantityError("received > accepted")};
        }
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Money required =
                value.inputRequired[static_cast<std::size_t>(good)];
            const Money reserved =
                value.inputReserved[static_cast<std::size_t>(good)];
            const Money consumed =
                value.inputConsumed[static_cast<std::size_t>(good)];
            if (!isfinite(required) || !isfinite(reserved) ||
                !isfinite(consumed) || required < Money(0) ||
                reserved < Money(0) || consumed < Money(0) ||
                (reserved + consumed > required &&
                 !nearMoney(reserved + consumed, required))) {
                return {
                    false,
                    "invalid production input commitment: order=" +
                        std::to_string(value.id) + ", good=" +
                        std::to_string(good) + ", required=" +
                        required.toString(18) + ", reserved=" +
                        reserved.toString(18) + ", consumed=" +
                        consumed.toString(18)};
            }
        }
        expectedEscrow += value.escrowed;
    }
    for (const WarehouseOrderId orderId : openOrderIds) {
        if (orderIndexById.find(orderId) == orderIndexById.end())
            return {false, "active order index contains unknown order"};
    }
    if (!nearMoney(totalEscrow, expectedEscrow))
        return {false, "escrow balance mismatch"};

    for (const Shipment& shipment : shipmentList) {
        if (!isfinite(shipment.cargo) || !isfinite(shipment.capacityUsed) ||
            shipment.cargo < Money(0) || shipment.capacityUsed < Money(0) ||
            orderIndexById.find(shipment.orderId) == orderIndexById.end())
            return {false, "invalid shipment"};
        const auto orderIt = orderIndexById.find(shipment.orderId);
        if (orderIt != orderIndexById.end()) {
            const WarehouseOrder& order = orderList[orderIt->second];
            const SupplyRoute* route = nullptr;
            for (const SupplyRoute& candidate : routeList) {
                if (candidate.id == shipment.routeId) {
                    route = &candidate;
                    break;
                }
            }
            if (route != nullptr && route->dynamicPricing) {
                const Money expectedCapacity =
                    shipment.cargo * route->transportCapacityPerUnit;
                if (!nearMoney(shipment.capacityUsed, expectedCapacity))
                    return {false, "shipment rail capacity usage mismatch"};
            }
            (void)order;
        }
    }
    for (const auto& [episode, orderId] : activeEpisodeByKey) {
        (void)episode;
        auto it = orderIndexById.find(orderId);
        if (it == orderIndexById.end() ||
            terminal(orderList[it->second].status)) {
            return {false, "active episode points to terminal order"};
        }
    }
    return {true, {}};
}
