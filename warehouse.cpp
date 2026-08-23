#include "warehouse.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_set>

Money InventoryState::available() const {
    return std::max(Money(0), onHand - reserved);
}

Money InventoryState::committedOutbound() const {
    return reserved + backlog;
}

Money InventoryState::position() const {
    // Accepted orders are a contract signal, not physical supply. Only
    // stock that is already on hand or has actually left its source belongs
    // in the reorder position; otherwise an unshipped confirmation can
    // suppress the replenishment episode that would create that shipment.
    return onHand + physicalInTransit - committedOutbound();
}

Money InventoryState::replenishmentQuantity() const {
    if (position() >= policy.reorderPoint) return Money(0);
    const Money quantity =
        std::max(Money(0), policy.targetStock - position());
    // Decimal arithmetic still accumulates tiny division/multiplication
    // residues.  Treat them as settled here so a dust-sized replenishment
    // order cannot hold an inventory episode open forever.
    return quantity <= Money(1e-9) ? Money(0) : quantity;
}

Money InventoryState::planReplenishmentQuantity() {
    constexpr double kWarehouseSmoothingGain = 0.45;
    constexpr int kReleaseCoverageWeeks = 4;

    const Money raw = replenishmentQuantity();
    lastRawReplenishment = raw;
    if (policy.baseStockReplenishment) {
        lastPlannedReplenishment = raw;
        return raw;
    }
    const Money weeklyRate = std::max(Money(0), policy.weeklyDemand);
    const Money releaseLimit = weeklyRate > Money(0)
        ? weeklyRate * Money(kReleaseCoverageWeeks) : raw;
    const Money boundedRequest = std::min(raw, releaseLimit);
    Money planned = replenishmentFilter.update(
        boundedRequest, kWarehouseSmoothingGain);
    planned = std::min(raw, std::max(Money(0), planned));
    lastPlannedReplenishment = planned <= Money(1e-9)
        ? Money(0) : planned;
    return lastPlannedReplenishment;
}

InventoryState& Warehouse::stock(int goodIndex) {
    if (goodIndex < 0 || goodIndex >= NUM_GOODS)
        throw std::out_of_range("good index out of range");
    return goods[static_cast<std::size_t>(goodIndex)];
}

const InventoryState& Warehouse::stock(int goodIndex) const {
    if (goodIndex < 0 || goodIndex >= NUM_GOODS)
        throw std::out_of_range("good index out of range");
    return goods[static_cast<std::size_t>(goodIndex)];
}

InventoryState& Warehouse::buildingInput(int buildingType, int goodIndex) {
    if (buildingType < 0 || buildingType >= TYPE_COUNT)
        throw std::out_of_range("building type out of range");
    if (goodIndex < 0 || goodIndex >= NUM_GOODS)
        throw std::out_of_range("good index out of range");
    return buildingInputs[static_cast<std::size_t>(buildingType)]
                         [static_cast<std::size_t>(goodIndex)];
}

const InventoryState& Warehouse::buildingInput(int buildingType,
                                                int goodIndex) const {
    if (buildingType < 0 || buildingType >= TYPE_COUNT)
        throw std::out_of_range("building type out of range");
    if (goodIndex < 0 || goodIndex >= NUM_GOODS)
        throw std::out_of_range("good index out of range");
    return buildingInputs[static_cast<std::size_t>(buildingType)]
                         [static_cast<std::size_t>(goodIndex)];
}

std::array<Money, NUM_GOODS> Warehouse::onHandSnapshot() const {
    std::array<Money, NUM_GOODS> result{};
    for (int good = 0; good < NUM_GOODS; ++good)
        result[static_cast<std::size_t>(good)] = stock(good).onHand;
    return result;
}

Money WarehouseNetwork::lastCompletedRouteUsage(int routeId) const {
    const auto usage = completedRouteUsage.find(routeId);
    return usage == completedRouteUsage.end()
        ? Money(0)
        : usage->second;
}

Money WarehouseNetwork::lastCompletedRailwayRevenue(int routeId) const {
    const auto value = completedRailwayRevenue.find(routeId);
    return value == completedRailwayRevenue.end() ? Money(0) : value->second;
}

Money WarehouseNetwork::lastCompletedWarehouseProfit(int routeId) const {
    const auto value = completedWarehouseProfit.find(routeId);
    return value == completedWarehouseProfit.end() ? Money(0) : value->second;
}

int WarehouseNetwork::lastInventoryReviewCycle(
    WarehouseId warehouseId) const {
    const auto value = lastInventoryReviewByWarehouse.find(warehouseId);
    return value == lastInventoryReviewByWarehouse.end() ? -1 : value->second;
}

std::uint64_t WarehouseNetwork::inventoryReviewCount(
    WarehouseId warehouseId) const {
    const auto value = inventoryReviewsByWarehouse.find(warehouseId);
    return value == inventoryReviewsByWarehouse.end() ? 0 : value->second;
}

std::uint64_t WarehouseNetwork::suppressedInventoryReviewCount(
    WarehouseId warehouseId) const {
    const auto value = suppressedInventoryReviewsByWarehouse.find(warehouseId);
    return value == suppressedInventoryReviewsByWarehouse.end()
        ? 0 : value->second;
}

const InventoryReviewDecision* WarehouseNetwork::inventoryReviewDecision(
    WarehouseId warehouseId, int goodIndex) const {
    if (!validGood(goodIndex)) return nullptr;
    const auto warehouse =
        inventoryReviewDecisionsByWarehouse.find(warehouseId);
    if (warehouse == inventoryReviewDecisionsByWarehouse.end())
        return nullptr;
    const InventoryReviewDecision& decision =
        warehouse->second[static_cast<std::size_t>(goodIndex)];
    return decision.cycle < 0 ? nullptr : &decision;
}

WarehouseCycleFlow WarehouseNetwork::lastCompletedFlow(
    WarehouseId warehouseId) const {
    const auto flow = completedFlowByWarehouse.find(warehouseId);
    if (flow != completedFlowByWarehouse.end()) return flow->second;
    WarehouseCycleFlow empty;
    empty.cycle = completedUsageCycle;
    return empty;
}

WarehouseCycleFlow& WarehouseNetwork::currentFlow(
    WarehouseId warehouseId) {
    WarehouseCycleFlow& flow = currentFlowByWarehouse[warehouseId];
    flow.cycle = cycle;
    return flow;
}

bool WarehouseNetwork::fundOrder(WarehouseOrder& orderValue,
                                 Money quantity) {
    const Money payment =
        nonNegative(quantity) * nonNegative(orderValue.contractPrice);
    if (payment <= Money(0)) return true;
    auto buyer = settlementAccounts.find(orderValue.buyerWarehouseId);
    auto seller = settlementAccounts.find(orderValue.sellerWarehouseId);
    if (buyer == settlementAccounts.end() &&
        seller == settlementAccounts.end()) {
        return true;
    }
    if (buyer == settlementAccounts.end() ||
        seller == settlementAccounts.end() ||
        !buyer->second.debit(
            orderValue.goodIndex, quantity,
            payment + (buyer->second.quoteTax
                ? std::max(Money(0), buyer->second.quoteTax(payment))
                : Money(0)))) {
        return false;
    }
    orderValue.escrowed += payment;
    if (buyer->second.collectTax) buyer->second.collectTax(payment);
    totalEscrow += payment;
    return true;
}

void WarehouseNetwork::settleReceived(WarehouseOrder& orderValue,
                                      Money quantity) {
    if (orderValue.escrowed <= Money(0) ||
        orderValue.contractPrice <= Money(0)) {
        return;
    }
    const Money payment = std::min(
        orderValue.escrowed,
        nonNegative(quantity) * orderValue.contractPrice);
    auto seller = settlementAccounts.find(orderValue.sellerWarehouseId);
    auto buyer = settlementAccounts.find(orderValue.buyerWarehouseId);
    if (seller == settlementAccounts.end()) return;
    orderValue.escrowed =
        std::max(Money(0), orderValue.escrowed - payment);
    totalEscrow = std::max(Money(0), totalEscrow - payment);

    Money productPayment = nonNegative(quantity) *
                           orderValue.sourceUnitPrice;
    Money railwayPayment = nonNegative(quantity) *
                           orderValue.railwayChargePerUnit;
    Money warehousePayment = nonNegative(quantity) *
                             orderValue.warehouseMarginPerUnit;
    const Money splitTotal = productPayment + railwayPayment +
                             warehousePayment;
    if (splitTotal > Money(0) && splitTotal != payment) {
        const Money scale = payment / splitTotal;
        productPayment *= scale;
        railwayPayment *= scale;
        warehousePayment = std::max(
            Money(0), payment - productPayment - railwayPayment);
    } else if (splitTotal <= Money(0)) {
        productPayment = payment;
    }

    seller->second.credit(orderValue.goodIndex, quantity, productPayment);
    if (railwayPayment > Money(0) && seller->second.creditLogistics)
        seller->second.creditLogistics(railwayPayment, Money(0));
    if (warehousePayment > Money(0) && buyer != settlementAccounts.end() &&
        buyer->second.creditLogistics) {
        buyer->second.creditLogistics(Money(0), warehousePayment);
    }
    if (orderValue.routeId >= 0) {
        currentRailwayRevenue[orderValue.routeId] += railwayPayment;
        currentWarehouseProfit[orderValue.routeId] += warehousePayment;
    }
}

void WarehouseNetwork::refundEscrow(WarehouseOrder& orderValue,
                                    Money quantity) {
    if (orderValue.escrowed <= Money(0) ||
        orderValue.contractPrice <= Money(0)) {
        return;
    }
    const Money payment = std::min(
        orderValue.escrowed,
        nonNegative(quantity) * orderValue.contractPrice);
    if (payment <= Money(0)) return;

    const Money refundedQuantity = payment / orderValue.contractPrice;
    const auto buyer = settlementAccounts.find(orderValue.buyerWarehouseId);
    if (buyer != settlementAccounts.end() && buyer->second.refund) {
        buyer->second.refund(orderValue.goodIndex, refundedQuantity, payment);
    }
    orderValue.escrowed = std::max(Money(0), orderValue.escrowed - payment);
    totalEscrow = std::max(Money(0), totalEscrow - payment);
    if (orderValue.escrowed <= Money(1e-9)) orderValue.escrowed = Money(0);
    if (totalEscrow <= Money(1e-9)) totalEscrow = Money(0);
}

Money WarehouseNetwork::releaseOrderReservation(WarehouseOrder& orderValue,
                                                Money quantity) {
    const Money released = std::min(orderValue.reserved,
                                    nonNegative(quantity));
    if (released <= Money(0)) return Money(0);

    InventoryState* stock = nullptr;
    if (orderValue.kind == WarehouseOrderKind::BuildingMaterialDemand &&
        orderValue.buyerWarehouseId >= 0 && validGood(orderValue.goodIndex)) {
        stock = &warehouseStock(orderValue.buyerWarehouseId,
                                orderValue.goodIndex);
    } else if ((orderValue.kind == WarehouseOrderKind::RemotePurchase ||
                orderValue.kind ==
                    WarehouseOrderKind::WarehouseReplenishment) &&
               orderValue.sellerWarehouseId >= 0 &&
               validGood(orderValue.goodIndex)) {
        stock = &warehouseStock(orderValue.sellerWarehouseId,
                                orderValue.goodIndex);
    }
    if (stock != nullptr) {
        stock->reserved = std::max(Money(0), stock->reserved - released);
        stock->reserved = std::min(stock->reserved, stock->onHand);
    }
    orderValue.reserved =
        std::max(Money(0), orderValue.reserved - released);
    return released;
}

void WarehouseNetwork::shrinkOrderTree(WarehouseOrder& orderValue,
                                       Money desiredRequested) {
    if (terminal(orderValue.status)) return;

    const Money oldRequested = orderValue.requested;
    const Money lockedQuantity =
        std::max(orderValue.received, orderValue.shipped);
    const Money targetRequested = std::min(
        oldRequested,
        std::max(nonNegative(desiredRequested), lockedQuantity));
    if (targetRequested + Money(1e-9) >= oldRequested) return;

    auto finalize = [this](WarehouseOrder& value) {
        if (value.requested <= Money(1e-9) &&
            value.accepted <= Money(1e-9) &&
            value.reserved <= Money(1e-9) &&
            value.shipped <= Money(1e-9) &&
            value.received <= Money(1e-9)) {
            value.status = WarehouseOrderStatus::Cancelled;
        }
        recomputeOrderStatus(value);
    };

    auto shrinkSameGoodSupply = [this, &orderValue](
                                     Money cancellation,
                                     bool includeRemote,
                                     bool includeProduction) {
        Money remaining = nonNegative(cancellation);
        const auto range = orderIdsByParent.equal_range(orderValue.id);
        for (auto it = range.first;
             it != range.second && remaining > Money(1e-9); ++it) {
            WarehouseOrder* child = findMutableOrder(it->second);
            if (child == nullptr || terminal(child->status) ||
                child->goodIndex != orderValue.goodIndex) {
                continue;
            }
            const bool isRemote =
                child->kind == WarehouseOrderKind::RemotePurchase;
            const bool isProduction =
                child->kind == WarehouseOrderKind::SupplierProduction;
            if ((!includeRemote || !isRemote) &&
                (!includeProduction || !isProduction)) {
                continue;
            }
            const Money before = child->requested;
            const Money lowerBound =
                std::max(child->received, child->shipped);
            const Money childTarget = std::max(
                lowerBound, std::max(Money(0), before - remaining));
            shrinkOrderTree(*child, childTarget);
            remaining = std::max(
                Money(0), remaining -
                    std::max(Money(0), before - child->requested));
        }
    };

    if (orderValue.kind == WarehouseOrderKind::BuildingMaterialDemand) {
        orderValue.requested = targetRequested;

        const Money retainedLocalFloor =
            std::min(orderValue.locallyAllocated, targetRequested);
        Money remoteAllowance = std::max(
            Money(0), targetRequested - retainedLocalFloor);
        const auto childRange =
            orderIdsByParent.equal_range(orderValue.id);
        for (auto it = childRange.first; it != childRange.second; ++it) {
            WarehouseOrder* child = findMutableOrder(it->second);
            if (child == nullptr ||
                child->kind != WarehouseOrderKind::RemotePurchase ||
                child->goodIndex != orderValue.goodIndex) {
                continue;
            }
            const Money lowerBound =
                std::max(child->received, child->shipped);
            const Money childTarget = std::max(
                lowerBound, std::min(child->requested, remoteAllowance));
            shrinkOrderTree(*child, childTarget);
            remoteAllowance = std::max(
                Money(0), remoteAllowance - child->accepted);
        }

        Money remoteAccepted = Money(0);
        for (auto it = childRange.first; it != childRange.second; ++it) {
            WarehouseOrder* child = findMutableOrder(it->second);
            if (child != nullptr &&
                child->kind == WarehouseOrderKind::RemotePurchase &&
                child->goodIndex == orderValue.goodIndex) {
                remoteAccepted += child->accepted;
            }
        }

        const Money localDelivered =
            std::min(orderValue.locallyAllocated, orderValue.received);
        const Money retainedLocal = std::min(
            orderValue.locallyAllocated,
            std::max(localDelivered,
                     std::max(Money(0),
                              targetRequested - remoteAccepted)));
        const Money localCancellation = std::max(
            Money(0), orderValue.locallyAllocated - retainedLocal);
        if (localCancellation > Money(0)) {
            releaseOrderReservation(orderValue, localCancellation);
            orderValue.locallyAllocated = retainedLocal;
        }

        const Money previousAccepted = orderValue.accepted;
        orderValue.accepted = std::max(
            orderValue.received, orderValue.locallyAllocated + remoteAccepted);
        orderValue.requested = std::max(orderValue.requested,
                                        orderValue.accepted);
        const Money acceptanceRemoved = std::max(
            Money(0), previousAccepted - orderValue.accepted);
        if (acceptanceRemoved > Money(0) &&
            orderValue.buyerWarehouseId >= 0 &&
            validBuilding(orderValue.buildingType) &&
            validGood(orderValue.goodIndex)) {
            InventoryState& input = buildingStock(
                orderValue.buyerWarehouseId, orderValue.buildingType,
                orderValue.goodIndex);
            input.confirmedInbound = std::max(
                Money(0), input.confirmedInbound - acceptanceRemoved);
        }

        const Money supplyCancellation = std::max(
            Money(0), oldRequested - orderValue.requested);
        shrinkSameGoodSupply(supplyCancellation, false, true);
        finalize(orderValue);
        touchRevision();
        return;
    }

    if (orderValue.kind == WarehouseOrderKind::SupplierProduction) {
        orderValue.requested = targetRequested;
        orderValue.accepted = std::max(
            lockedQuantity, std::min(orderValue.accepted, targetRequested));

        const ProductionRecipe* recipe = findProducer(
            orderValue.buyerWarehouseId, orderValue.goodIndex);
        if (recipe != nullptr &&
            recipe->buildingType == orderValue.buildingType &&
            recipe->outputPerBatch > Money(0)) {
            const auto childRange =
                orderIdsByParent.equal_range(orderValue.id);
            for (int good = 0; good < NUM_GOODS; ++good) {
                const std::size_t index = static_cast<std::size_t>(good);
                const Money perBatch = recipe->inputs[index];
                if (perBatch <= Money(0)) continue;

                const Money previousRequired = orderValue.inputRequired[index];
                const Money targetRequired = std::max(
                    orderValue.inputConsumed[index],
                    targetRequested * perBatch / recipe->outputPerBatch);
                const Money retainedReservation = std::max(
                    Money(0), targetRequired - orderValue.inputConsumed[index]);
                const Money releasedReservation = std::max(
                    Money(0), orderValue.inputReserved[index] -
                                  retainedReservation);
                if (releasedReservation > Money(0)) {
                    InventoryState& input = buildingStock(
                        orderValue.buyerWarehouseId,
                        orderValue.buildingType, good);
                    input.reserved = std::max(
                        Money(0), input.reserved - releasedReservation);
                    input.reserved = std::min(input.reserved, input.onHand);
                    orderValue.inputReserved[index] = std::max(
                        Money(0), orderValue.inputReserved[index] -
                                      releasedReservation);
                }
                orderValue.inputRequired[index] = targetRequired;

                Money inputCancellation = std::max(
                    Money(0), previousRequired - targetRequired);
                for (auto it = childRange.first;
                     it != childRange.second &&
                         inputCancellation > Money(1e-9); ++it) {
                    WarehouseOrder* child = findMutableOrder(it->second);
                    if (child == nullptr || terminal(child->status) ||
                        child->kind !=
                            WarehouseOrderKind::BuildingMaterialDemand ||
                        child->goodIndex != good) {
                        continue;
                    }
                    const Money before = child->requested;
                    const Money lowerBound =
                        std::max(child->received, child->shipped);
                    const Money childTarget = std::max(
                        lowerBound,
                        std::max(Money(0), before - inputCancellation));
                    shrinkOrderTree(*child, childTarget);
                    inputCancellation = std::max(
                        Money(0), inputCancellation -
                            std::max(Money(0),
                                     before - child->requested));
                }
            }
        }
        finalize(orderValue);
        touchRevision();
        return;
    }

    orderValue.requested = targetRequested;
    const Money previousAccepted = orderValue.accepted;
    const Money retainedAccepted = std::max(
        lockedQuantity, std::min(orderValue.accepted, targetRequested));
    const Money cancelledAccepted = std::max(
        Money(0), previousAccepted - retainedAccepted);
    releaseOrderReservation(orderValue, cancelledAccepted);
    orderValue.accepted = retainedAccepted;
    if (cancelledAccepted > Money(0) &&
        orderValue.buyerWarehouseId >= 0 &&
        validGood(orderValue.goodIndex)) {
        InventoryState& destination = warehouseStock(
            orderValue.buyerWarehouseId, orderValue.goodIndex);
        destination.confirmedInbound = std::max(
            Money(0), destination.confirmedInbound - cancelledAccepted);
        refundEscrow(orderValue, cancelledAccepted);
    }

    const Money requestCancellation =
        std::max(Money(0), oldRequested - orderValue.requested);
    const Money unconfirmedCancellation = std::max(
        Money(0), requestCancellation - cancelledAccepted);
    shrinkSameGoodSupply(
        cancelledAccepted +
            unconfirmedCancellation,
        true, true);
    finalize(orderValue);
    touchRevision();
}
