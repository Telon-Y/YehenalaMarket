// ==================== local_market.cpp ====================
// Core state, snapshots, flow traces, and compatibility metrics.
#include "local_market.h"
#include "local_market_internal.h"
#include "world.h"
#include "country.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>

using namespace std;

LocalMarket::LocalMarket(int id, const std::string& name)
    : marketId(id), marketName(name), warehouse(id) {
    for (int i = 0; i < NUM_GOODS; ++i)
        goodIndex[commodityNames[i]] = i;

    initPriceState(priceState);

    latestConsumerTarget.fill(Money(0));
    latestRawConsumerTarget.fill(Money(0));
    smoothedConsumerDemand.fill(Money(0));
    smoothedWarehouseDemand.fill(Money(0));
    latestPlannedConsumerDemand.fill(Money(0));
    latestConsumerActual.fill(Money(0));
    latestPotentialIn.fill(Money(0));
    latestRealOut.fill(Money(0));
    latestBuildingOutput.fill(Money(0));
    tradeBalance.fill(Money(0));
    pendingTradeRevenue.fill(Money(0));
    pendingRailwayRevenue = Money(0);
    pendingWarehouseProfit = Money(0);
    totalLaborers = 0.0;
    totalEngineers = 0.0;
    totalCapitalists = 0.0;

    Money initMoney = Money(500000000.0);
    classCash[LABORER]    = initMoney * Money(0.4);
    classCash[ENGINEER]   = initMoney * Money(0.4);
    classCash[CAPITALIST] = initMoney * Money(0.2);
    investmentPool = Money(200000000.0);
    bld.syncBankLevels(investmentPool);
    totalDebt = Money(0);
    loanBalance.fill(Money(0));
    classLastSpending.fill(Money(0));
    buildingLoanCount.fill(0);
    loanDelinquentWeeks.fill(0);
    investmentLoanDelinquentWeeks = 0;
    playerCash = Money(0);
    buildTransferTotal = Money(0);

    initialTotalMoneySupply = Money(0);
    for (int t = 0; t < TYPE_COUNT; ++t) initialTotalMoneySupply += bld.getCashPools()[t];
    for (int c = 0; c < CLASS_COUNT; ++c) initialTotalMoneySupply += classCash[c];
    initialTotalMoneySupply += investmentPool;  // Part of initial money supply.
    totalMoneySupply = initialTotalMoneySupply;

    buildingWages.fill(Money(averageWage));
    buildingBonuses.fill(Money(0));
    satisfactionFilter.seed(0.75);

    maxLabor = laborPopulation * LABOR_FORCE_PARTICIPATION;
    dependentPopulation = laborPopulation - maxLabor;
    initialLaborPopulation = laborPopulation;

    double initialEmployed = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        const double fullEmployment =
            bld.getBuildingCounts()[t] *
            bld.getTemplates()[t].laborPerUnit;
        actualEmployment[t] = fullEmployment;
        targetEmployment[t] = fullEmployment;
        actualEmploymentRate[t] = fullEmployment > 0.0 ? 1.0 : 0.0;
        initialEmployed += fullEmployment;
    }
    double initialRemaining = maxLabor - initialEmployed;
    if (initialRemaining < 0.0) initialRemaining = 0.0;
    int initialIdleFarms = 10000 - (bld.getBuildingCounts()[FARM_GRAIN] + bld.getBuildingCounts()[COTTON]);
    if (initialIdleFarms < 0) initialIdleFarms = 0;
    subsistencePop = std::min(initialRemaining, (double)initialIdleFarms * 5000.0);
    if (subsistencePop < 0.0) subsistencePop = 0.0;

    // Issue securities when a building completes.
    bld.onBuildingCompleted = [this](int typeIdx, int count, OwnerType owner) {
        this->issueSecurities(typeIdx, count, owner);
    };
    bld.onBuildingsRemoving = [this](int typeIdx, int count) {
        this->settleLoansBeforeDemolish(typeIdx, count);
    };
    for (int t = 0; t < TYPE_COUNT; ++t)
        issueSecurities(t, bld.getBuildingCounts()[t], OWNER_INITIAL);
    standaloneWarehouseNetwork.attachWarehouse(warehouse);
    logisticsNetwork = &standaloneWarehouseNetwork;
    initializeWarehousePolicies();
    syncWarehouseProducers();
}

Money LocalMarket::getGDPAtCycle(int cycle) const {
    if (cycle <= 0 || gdpHist.empty()) return Money(0);
    std::size_t end = 1;
    if (cycle >= historyFirstCycle) {
        end = std::min(
            gdpHist.size(),
            static_cast<std::size_t>(cycle - historyFirstCycle + 1));
    }
    const std::size_t begin = end > 52 ? end - 52 : 0;
    Money total = Money(0);
    for (std::size_t index = begin; index < end; ++index)
        total += gdpHist[index];
    const std::size_t count = end - begin;
    return count == 0
        ? Money(0)
        : total * Money(52) / Money(static_cast<int>(count));
}

Money LocalMarket::getGDP() const {
    if (gdpHist.empty()) return Money(0);
    return getGDPAtCycle(stepCount);
}

double LocalMarket::getPopulationAtCycle(int cycle) const {
    if (cycle <= 0) return initialLaborPopulation;
    if (populationHist.empty()) return laborPopulation;
    if (cycle < historyFirstCycle) return populationHist.front();
    const std::size_t index = static_cast<std::size_t>(
        cycle - historyFirstCycle);
    return index < populationHist.size() ? populationHist[index] :
        populationHist.back();
}

void LocalMarket::attachWorldContext(World* world, int id) {
    ownerWorld = world;
    provinceId = id;
}
Money LocalMarket::constructionCapacityForPlan() const {
    const int levels = bld.getBuildingCounts()[CONST_DEPT];
    if (levels <= 0) return Money(0);
    const BuildingTemplate& construction =
        bld.getTemplates()[CONST_DEPT];
    double employmentRate = stepCount > 0
        ? actualEmploymentRate[CONST_DEPT]
        : bld.getEmploymentRatio()[CONST_DEPT];
    // A zero rate after the first cycle is a real labor/material constraint,
    // not an uninitialized value. Only non-finite state may fall back to the
    // setup ratio; otherwise the national plan must honor actual staffing.
    if (!std::isfinite(employmentRate))
        employmentRate = bld.getEmploymentRatio()[CONST_DEPT];
    employmentRate = std::clamp(employmentRate, 0.0, 1.0);
    return Money(levels) * Money(construction.outputRate) *
           Money(employmentRate);
}

Money LocalMarket::constructionSustainableCapacityForPlan() const {
    const Money installed = constructionCapacityForPlan();
    if (installed <= Money(0) || logisticsNetwork == nullptr)
        return installed;

    const BuildingTemplate& construction = bld.getTemplates()[CONST_DEPT];
    Money sustainable = installed;
    for (int good = 0; good < NUM_GOODS; ++good) {
        const double coefficient = construction.inputs[good];
        if (coefficient <= 0.0) continue;

        const int leadCycles = logisticsNetwork->inboundLeadCycles(
            marketId, good);
        const int horizon = std::max(
            1, leadCycles + CONSTRUCTION_INPUT_SAFETY_WEEKS);
        const InventoryState& input = logisticsNetwork->buildingStock(
            marketId, CONST_DEPT, good);
        // Confirmed inbound is deliberately excluded here. It is a contract
        // signal, not physical supply; the supplier forecast or transit ledger
        // must be able to sustain the planned weekly draw.
        const Money physical = std::max(
            Money(0), input.onHand - input.reserved) +
            std::max(Money(0), input.physicalInTransit);
        const Money forecast = logisticsNetwork->forecastSupplyRate(
            marketId, good);
        const Money weeklySupply = forecast + physical / Money(horizon);
        const Money inputCapacity = weeklySupply / Money(coefficient);
        sustainable = std::min(sustainable, std::max(Money(0), inputCapacity));
    }
    return std::clamp(sustainable, Money(0), installed);
}

Money LocalMarket::constructionOutputPlanForPolicy() const {
    return constructionPlanActive
        ? std::max(Money(0), plannedConstructionOutput)
        : constructionCapacityForPlan();
}

Money LocalMarket::constructionInputPlanForPolicy() const {
    return constructionPlanActive
        ? std::max(Money(0), plannedConstructionInputOutput)
        : constructionCapacityForPlan();
}

void LocalMarket::setConstructionOutputPlan(Money output, Money inputOutput,
                                            bool active) {
    constructionPlanActive = active;
    plannedConstructionOutput = active
        ? std::max(Money(0), output) : Money(0);
    plannedConstructionInputOutput = active
        ? std::max(Money(0), inputOutput) : Money(0);
}

Money LocalMarket::consumeNationalConstruction(Money constructionUnits) {
    if (!isfinite(constructionUnits) || constructionUnits <= Money(0))
        return Money(0);
    // Construction power is generated and purchased in the same cycle. It is
    // not a warehouse good, so consume only the uncommitted current output.
    const Money available = getAvailableNationalConstructionCapacity();
    const Money consumed = std::min(available, constructionUnits);
    nationalConstructionUsedSinceStep += consumed;
    if (flowTraceActive)
        latestFlow.constructionUse[CONSTR_GOOD_INDEX] += consumed;
    return consumed;
}

void LocalMarket::rollbackNationalConstruction(Money constructionUnits) {
    if (!isfinite(constructionUnits) || constructionUnits <= Money(0))
        return;
    // There is no inventory to restore for the special construction good.
    nationalConstructionUsedSinceStep = std::max(
        Money(0), nationalConstructionUsedSinceStep - constructionUnits);
    if (flowTraceActive)
        latestFlow.constructionUse[CONSTR_GOOD_INDEX] = std::max(
            Money(0), latestFlow.constructionUse[CONSTR_GOOD_INDEX] -
                          constructionUnits);
}

Money LocalMarket::getAvailableNationalConstructionCapacity() const {
    if (bld.getBuildingCounts()[CONST_DEPT] <= 0) return Money(0);
    // Construction power is a special, non-storable good. lastConstrProduced
    // is the actual post-input output of this cycle; only its uncommitted
    // remainder can be purchased by national construction.
    const Money produced = std::max(Money(0), lastConstrProduced);
    return std::max(Money(0), produced - localConstructionUsedThisCycle -
                                      nationalConstructionUsedSinceStep);
}

void LocalMarket::recordNationalConstructionSale(Money constructionUnits,
                                                  Money payment) {
    if (!isfinite(constructionUnits) || constructionUnits <= Money(0) ||
        !isfinite(payment) || payment <= Money(0)) {
        return;
    }
    // The treasury payment is transferred to the government-owned
    // construction department immediately. Revenue is posted to the next
    // market statement so GDP and profit statistics use the same cycle as
    // other construction sales.
    bld.addCash(CONST_DEPT, payment);
    // Publish usage only after the country payment succeeds. The capacity
    // probe above is rolled back when settlement fails.
    lastConstrUsed += constructionUnits;
    pendingNationalConstructionRevenue += payment;
}

void LocalMarket::recordNationalConstructionBase(Money constructionUnits,
                                                  Money payment) {
    if (!isfinite(constructionUnits) || constructionUnits <= Money(0) ||
        !isfinite(payment) || payment <= Money(0)) {
        return;
    }
    // Base capacity is virtual and never removes warehouse stock, but it is
    // still paid at the project's locked construction price and appears in
    // the construction department's statement for accounting consistency.
    bld.addCash(CONST_DEPT, payment);
    lastConstrUsed += constructionUnits;
    pendingNationalConstructionRevenue += payment;
}

void LocalMarket::completeNationalConstruction(int typeIndex, int count,
                                                OwnerType owner) {
    if (count <= 0) return;
    bld.addCompletedBuildings(typeIndex, count, owner);
    syncBuildingInputPolicies();
    syncWarehouseProducers();
}

MarketSnapshot LocalMarket::getSnapshot() const {
    MarketSnapshot snap;
    snap.marketId = marketId;
    snap.marketName = marketName;
    snap.prices = priceState.prices;
    snap.inventory = warehouse.onHandSnapshot();
    for (int good = 0; good < NUM_GOODS; ++good) {
        const InventoryState& state = warehouse.stock(good);
        auto& stock = snap.warehouseStock[good];
        stock.onHand = state.onHand;
        stock.reserved = state.reserved;
        stock.available = state.available();
        stock.confirmedInbound = state.confirmedInbound;
        stock.physicalInTransit = state.physicalInTransit;
        stock.backlog = state.backlog;
        stock.position = state.position();
        stock.targetStock = state.policy.targetStock;
        stock.reorderPoint = state.policy.reorderPoint;
        stock.replenishment = state.lastPlannedReplenishment;
        stock.averageDemand = smoothedWarehouseDemand[good];
        stock.demand52 = stock.averageDemand;
        stock.coverageWeeks = stock.averageDemand > Money(1e-9)
            ? (state.onHand / stock.averageDemand).toDouble()
            : 0.0;
        if (logisticsNetwork != nullptr) {
            const InventoryReviewDecision* review =
                logisticsNetwork->inventoryReviewDecision(marketId, good);
            if (review != nullptr) {
                stock.reviewCycle = review->cycle;
                stock.reviewAverageDemand = review->averageDemand;
                stock.reviewTargetStock = review->targetStock;
                stock.reviewReorderPoint = review->reorderPoint;
                stock.reviewOnHand = review->onHand;
                stock.reviewReserved = review->reserved;
                stock.reviewAvailable = review->available;
                stock.reviewPosition = review->inventoryPosition;
                stock.reviewConfirmedInbound = review->confirmedInbound;
                stock.reviewPhysicalInTransit = review->physicalInTransit;
                stock.reviewBacklog = review->backlog;
                stock.reviewCoverageWeeks =
                    review->averageDemand > Money(1e-9)
                    ? (review->onHand / review->averageDemand).toDouble()
                    : 0.0;
                stock.rawReplenishment = review->rawGap;
                stock.plannedRequest = review->plannedRequest;
                stock.linkedOrderId = review->linkedOrderId;
                stock.reviewReason = review->reason;
            }
        }
    }
    if (logisticsNetwork != nullptr) {
        const auto& templates = bld.getTemplates();
        for (int type = 0; type < TYPE_COUNT; ++type) {
            const int outputGood = templates[type].outputGood;
            if (outputGood < 0 || outputGood >= NUM_GOODS) continue;
            snap.productionDemand52[outputGood] +=
                logisticsNetwork->productionDemand52(
                    marketId, type, outputGood);
            snap.productionCommand[outputGood] +=
                logisticsNetwork->productionCommand(
                    marketId, type, outputGood);
            snap.pendingProduction[outputGood] +=
                logisticsNetwork->pendingProduction(
                    marketId, type, outputGood);
            snap.productionPlanCycle = std::max(
                snap.productionPlanCycle,
                logisticsNetwork->productionCommandCycle(
                    marketId, type, outputGood));
        }
        snap.lastInventoryReviewCycle =
            logisticsNetwork->lastInventoryReviewCycle(marketId);
        snap.inventoryReviewCount =
            logisticsNetwork->inventoryReviewCount(marketId);
        snap.suppressedInventoryReviews =
            logisticsNetwork->suppressedInventoryReviewCount(marketId);
    }
    snap.gdp = getGDP();
    snap.population = laborPopulation;
    snap.cycle = logisticsNetwork == nullptr
        ? stepCount
        : logisticsNetwork->currentCycle();
    snap.flow = latestFlow;
    snap.productionResultCycle = latestFlow.cycle;
    return snap;
}

void LocalMarket::beginFlowTrace(int cycle) {
    latestFlow = MarketFlowSnapshot{};
    latestFlow.cycle = cycle;
    latestFlow.openingStock = warehouse.onHandSnapshot();
    flowTraceActive = true;
}

void LocalMarket::finalizeFlowTrace(
    const WarehouseCycleFlow& warehouseFlow) {
    latestFlow.cycle = warehouseFlow.cycle;
    latestFlow.received = warehouseFlow.received;
    latestFlow.dispatched = warehouseFlow.dispatched;
    latestFlow.movedToBuilding = warehouseFlow.movedToBuilding;
    latestFlow.buildingConsumed = warehouseFlow.buildingConsumed;
    latestFlow.closingStock = warehouse.onHandSnapshot();
    latestFlow.inventoryBalanced = true;

    for (int good = 0; good < NUM_GOODS; ++good) {
        // Construction power is a special current-cycle flow. It is reported
        // for diagnostics, but never participates in warehouse conservation.
        if (good == CONSTR_GOOD_INDEX || good == TRANSPORT_CAPACITY_GOOD_INDEX) {
            latestFlow.inventoryResidual[good] = Money(0);
            continue;
        }
        const InventoryState& stock = warehouse.stock(good);
        latestFlow.inTransit[good] = stock.physicalInTransit;
        const Money expected =
            latestFlow.openingStock[good] +
            latestFlow.production[good] +
            latestFlow.received[good] -
            latestFlow.dispatched[good] -
            latestFlow.movedToBuilding[good] -
            latestFlow.directProductionUse[good] -
            latestFlow.consumerUse[good] -
            latestFlow.constructionUse[good];
        latestFlow.inventoryResidual[good] =
            latestFlow.closingStock[good] - expected;
        const double residual =
            std::fabs(latestFlow.inventoryResidual[good].toDouble());
        const double scale = std::max(
            1.0, std::fabs(latestFlow.closingStock[good].toDouble()));
        if (residual > 1.0e-8 * scale)
            latestFlow.inventoryBalanced = false;
    }
    flowTraceActive = false;
}
