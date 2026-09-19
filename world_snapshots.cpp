#include "world.h"
#include "construction_queue.h"

#include <algorithm>
#include <cmath>
#include <utility>

MarketSnapshot World::getMarketSnapshot(int index) const {
    return getMarket(index).getSnapshot();
}


ProvinceSnapshot World::getProvinceSnapshot(int provinceId) const {
    const Province& province = getProvinceById(provinceId);
    const LocalMarket& market = province.getLocalMarket();
    ProvinceSnapshot snapshot;
    snapshot.provinceId = province.getId();
    snapshot.countryId = province.getCountryId();
    snapshot.cycle = market.getStepCount();
    snapshot.name = province.getName();
    if (province.getCountryId() >= 0)
        snapshot.countryName = getCountryById(province.getCountryId()).getName();
    snapshot.population = market.getPopulation();
    snapshot.dependentPopulation = market.getDependentPopulation();
    snapshot.gdp = market.getGDP();
    snapshot.employmentRate = market.getEmploymentRate();
    snapshot.priceLevel = market.getPriceLevel();
    snapshot.satisfaction = market.getSatisfaction();
    snapshot.totalMoneySupply = market.getTotalMoneySupply();
    snapshot.investmentPool = market.getInvestmentPool();
    snapshot.bankLoanCapacity = market.getBankLoanCapacity();
    snapshot.totalDebt = market.getTotalDebt();
    snapshot.investmentLoanBalance = market.getInvestmentLoanBalance();
    snapshot.gdpHistory = market.getGDPHistory();
    snapshot.populationHistory = market.getPopulationHistory();

    const auto& counts = market.getBuildingCounts();
    const auto& employment = market.getActualEmployment();
    const auto& targetEmployment = market.getTargetEmployment();
    const auto& utilization = market.getActualEmploymentRate();
    const auto& output = market.getLatestBuildingOutput();
    const auto& baseWages = market.getWages();
    const auto& bonusWages = market.getBonuses();
    const auto& profits = market.getAvgProfitRates();
    const auto& supply = market.getCurrentSupplyRatio();
    const auto& capacityUtilization = market.getCapacityUtilization();
    const auto& fundingAvailability = market.getFundingAvailability();
    const auto& materialAvailability = market.getMaterialAvailability();
    const auto& staffedCapacity = market.getStaffedCapacity();
    const auto& productionTarget = market.getProductionTarget();
    const auto localPending = market.getPendingConstructionCounts();
    snapshot.buildings.reserve(TYPE_COUNT);
    for (int type = 0; type < TYPE_COUNT; ++type) {
        BuildingSnapshot building;
        building.typeIndex = type;
        building.count = counts[type];
        building.employment = employment[type];
        building.utilization = utilization[type];
        building.fullEmployment =
            counts[type] * market.getBuildingTemplates()[type].laborPerUnit;
        building.targetEmployment = targetEmployment[type];
        building.actualEmploymentRate = utilization[type];
        building.baseWage = baseWages[type];
        building.bonusWage = bonusWages[type];
        building.effectiveWage = baseWages[type] + bonusWages[type];
        building.recruitmentSatisfaction =
            targetEmployment[type] > 1.0e-9
                ? std::clamp(
                      employment[type] / targetEmployment[type], 0.0, 1.0)
                : 1.0;
        building.capacityUtilization = capacityUtilization[type];
        building.fundingAvailability = fundingAvailability[type];
        building.materialAvailability = materialAvailability[type];
        building.staffedCapacity = staffedCapacity[type];
        building.productionTarget = productionTarget[type];
        building.output = output[type].toDouble();
        building.profitRate = profits[type];
        building.operational = counts[type] > 0 && supply[type] > 0.0;
        building.pending = localPending[type];
        building.resourceCap = market.getResourceCap(type);
        snapshot.buildings.push_back(building);
    }

    snapshot.populationClasses.reserve(CLASS_COUNT);
    for (int classIndex = 0; classIndex < CLASS_COUNT; ++classIndex) {
        PopulationClassSnapshot population;
        population.classIndex = classIndex;
        population.population = market.getClassPopulation(classIndex);
        population.employed = market.getClassEmployment(classIndex);
        population.unemployed = std::max(0.0,
                                         population.population - population.employed);
        population.income = market.getClassCash(classIndex);
        population.demandSatisfaction = market.getSatisfaction();
        snapshot.populationClasses.push_back(population);
    }
    return snapshot;
}

CountrySnapshot World::getCountrySnapshot(int countryId) const {
    const Country& country = getCountryById(countryId);
    CountrySnapshot snapshot;
    snapshot.countryId = country.getId();
    snapshot.key = country.getKey();
    snapshot.countryCode = country.getCountryCode();
    snapshot.tag = snapshot.countryCode;
    snapshot.name = country.getName();
    snapshot.provinceIds = country.getProvinceIds();
    snapshot.treasury = country.getTreasury();
    snapshot.reservedConstructionBudget = country.getReservedConstructionBudget();
    snapshot.availableTreasury = country.getAvailableTreasury();
    const NationalConstructionPoolState& pool =
        country.getConstructionPoolState();
    snapshot.industrialConstructionCapacity = pool.industrialCapacity;
    snapshot.industrialConstructionAvailable = pool.industrialAvailable;
    snapshot.baseConstructionSupplement = pool.baseSupplement;
    snapshot.industrialConstructionUsed = pool.industrialUsed;
    snapshot.baseConstructionUsed = pool.baseUsed;
    snapshot.totalConstructionCapacity =
        NationalConstructionTotalCapacity(pool.industrialCapacity);
    snapshot.totalConstructionAvailable =
        pool.industrialAvailable +
        std::max(Money(0), pool.baseSupplement - pool.baseUsed);
    snapshot.totalConstructionUsed =
        pool.industrialUsed + pool.baseUsed;
    snapshot.baseConstructionExpenditure = pool.baseExpenditure;
    std::vector<ProvinceSnapshot> provinceSnapshots;
    provinceSnapshots.reserve(country.getProvinceIds().size());
    for (const int provinceId : country.getProvinceIds())
        provinceSnapshots.push_back(getProvinceSnapshot(provinceId));
    if (!provinceSnapshots.empty()) {
        double satisfactionPopulation = 0.0;
        double satisfactionWeight = 0.0;
        snapshot.cycle = provinceSnapshots.front().cycle;
        for (const ProvinceSnapshot& province : provinceSnapshots)
            snapshot.cycle = std::min(snapshot.cycle, province.cycle);
        for (std::size_t index = 0; index < provinceSnapshots.size(); ++index) {
            const ProvinceSnapshot& province = provinceSnapshots[index];
            const int provinceId = country.getProvinceIds()[index];
            const LocalMarket& market = getProvinceById(provinceId).getLocalMarket();
            snapshot.population += province.cycle == snapshot.cycle
                ? province.population : market.getPopulationAtCycle(snapshot.cycle);
            snapshot.gdp += province.cycle == snapshot.cycle
                ? province.gdp : market.getGDPAtCycle(snapshot.cycle);
            if (std::isfinite(province.population) &&
                province.population > 0.0 &&
                std::isfinite(province.satisfaction)) {
                satisfactionPopulation +=
                    province.population * province.satisfaction;
                satisfactionWeight += province.population;
            }
        }
        if (satisfactionWeight > 0.0) {
            snapshot.averageSatisfaction = std::clamp(
                satisfactionPopulation / satisfactionWeight, 0.0, 1.0);
        }
    }
    const auto makeConstructionSnapshot = [](
        const ConstructionProject& project, bool historical) {
        ConstructionProjectSnapshot value;
        value.id = project.id;
        value.clientRequestId = project.clientRequestId;
        value.sequence = project.sequence;
        value.payerCountryId = project.payerCountryId;
        value.payerCountryTag = project.payerCountryTag;
        value.targetProvinceId = project.targetProvinceId;
        value.typeIndex = project.typeIndex;
        value.quantity = project.quantity;
        value.completedUnits = project.completedUnits;
        value.totalBudget = project.totalBudget;
        value.unitPrice = project.unitPrice;
        value.maximumUnitPrice = project.maximumUnitPrice;
        value.reservedBudget = project.reservedBudget;
        value.paidBudget = project.paidBudget;
        value.startupCapitalPerUnit = project.startupCapitalPerUnit;
        value.reservedStartupCapital = project.reservedStartupCapital;
        value.paidStartupCapital = project.paidStartupCapital;
        value.totalConstruction = project.totalConstruction;
        value.remainingConstruction = project.remainingConstruction;
        value.currentUnitProgress = project.currentUnitProgress;
        value.expectedProfitPriority = project.expectedProfitPriority;
        value.progress = project.totalConstruction > Money(0)
            ? (project.totalConstruction - project.remainingConstruction) /
                  project.totalConstruction
            : Money(0);
        value.priority = project.priority;
        value.fundingKind = static_cast<int>(project.funding.kind);
        value.ownerType = static_cast<int>(project.owner.type);
        value.createdStep = project.createdStep;
        value.lastSettledStep = project.lastSettledStep;
        value.finishedStep = project.finishedStep;
        value.status = static_cast<int>(project.status);
        value.blockReason = static_cast<int>(project.blockReason);
        value.historical = historical;
        return value;
    };

    snapshot.constructionProjects.reserve(
        country.getConstructionProjects().size());
    for (const ConstructionProject& project :
         country.getConstructionProjects()) {
        snapshot.constructionProjects.push_back(
            makeConstructionSnapshot(project, false));
    }
    snapshot.constructionHistory.reserve(
        country.getConstructionHistory().size());
    for (const ConstructionProject& project :
         country.getConstructionHistory()) {
        snapshot.constructionHistory.push_back(
            makeConstructionSnapshot(project, true));
    }
    std::stable_sort(
        snapshot.constructionProjects.begin(),
        snapshot.constructionProjects.end(),
        ConstructionQueueOrder{});
    std::stable_sort(
        snapshot.constructionHistory.begin(),
        snapshot.constructionHistory.end(),
        [](const ConstructionProjectSnapshot& left,
           const ConstructionProjectSnapshot& right) {
            if (left.finishedStep != right.finishedStep)
                return left.finishedStep > right.finishedStep;
            return left.id > right.id;
        });    return snapshot;
}

std::vector<CountrySnapshot> World::getCountrySnapshots() const {
    std::vector<CountrySnapshot> snapshots;
    snapshots.reserve(countries.size());
    for (const auto& country : countries)
        snapshots.push_back(getCountrySnapshot(country->getId()));
    return snapshots;
}

std::vector<ProvinceSnapshot> World::getProvinceSnapshots(
    const std::vector<int>& provinceIds) const {
    std::vector<ProvinceSnapshot> snapshots;
    snapshots.reserve(provinceIds.size());
    for (const int provinceId : provinceIds)
        if (provinceIndexById.find(provinceId) != provinceIndexById.end())
            snapshots.push_back(getProvinceSnapshot(provinceId));
    return snapshots;
}
const TransportationSnapshot& World::getTransportationSnapshot(
    int warehouseId) const {
    const std::uint64_t modelRevision = warehouseNetwork.stateRevision();
    if (transportationCacheRevision != modelRevision) {
        transportationSnapshotCache.clear();
        transportationCacheRevision = modelRevision;
    }
    const auto cached = transportationSnapshotCache.find(warehouseId);
    if (cached != transportationSnapshotCache.end()) return cached->second;

    TransportationSnapshot result;
    result.cycle = warehouseNetwork.currentCycle();
    result.usageCycle = warehouseNetwork.lastCompletedUsageCycle();
    result.revision = modelRevision;
    result.totalEscrow = Money(0);

    for (int provinceIndex = 0;
         provinceIndex < getProvinceCount(); ++provinceIndex) {
        const Province& province = getProvince(provinceIndex);
        const int id = province.getLocalMarketId();
        if (warehouseId >= 0 && id != warehouseId) continue;
        WarehouseLocationSnapshot location;
        location.warehouseId = id;
        location.provinceId = province.getId();
        location.countryId = province.getCountryId();
        location.provinceName = province.getName();
        location.railwayLevels = province.getLocalMarket()
            .getBuildingCounts()[RAILWAY];
        location.lastInventoryReviewCycle =
            warehouseNetwork.lastInventoryReviewCycle(id);
        location.inventoryReviewCount =
            warehouseNetwork.inventoryReviewCount(id);
        location.suppressedInventoryReviews =
            warehouseNetwork.suppressedInventoryReviewCount(id);
        result.warehouses.push_back(std::move(location));
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (good == CONSTR_GOOD_INDEX ||
                good == TRANSPORT_CAPACITY_GOOD_INDEX) continue;
            const InventoryReviewDecision* review =
                warehouseNetwork.inventoryReviewDecision(id, good);
            if (review == nullptr) continue;
            InventoryReviewDecisionSnapshot value;
            value.cycle = review->cycle;
            value.warehouseId = id;
            value.goodIndex = good;
            value.onHand = review->onHand;
            value.reserved = review->reserved;
            value.available = review->available;
            value.averageDemand = review->averageDemand;
            value.targetStock = review->targetStock;
            value.reorderPoint = review->reorderPoint;
            value.inventoryPosition = review->inventoryPosition;
            value.rawGap = review->rawGap;
            value.plannedRequest = review->plannedRequest;
            value.confirmedInbound = review->confirmedInbound;
            value.physicalInTransit = review->physicalInTransit;
            value.backlog = review->backlog;
            value.linkedOrderId = review->linkedOrderId;
            value.reason = review->reason;
            result.inventoryReviews.push_back(std::move(value));
        }
    }

    std::unordered_map<int, std::size_t> routeIndexById;
    for (const SupplyRoute& source : warehouseNetwork.routes()) {
        if (warehouseId >= 0 &&
            source.sourceWarehouseId != warehouseId &&
            source.destinationWarehouseId != warehouseId) {
            continue;
        }
        RouteSnapshot value;
        value.id = source.id;
        value.sourceWarehouseId = source.sourceWarehouseId;
        value.destinationWarehouseId = source.destinationWarehouseId;
        value.goodIndex = source.goodIndex;
        value.capacityPerCycle = source.capacityPerCycle;
        value.unitPrice = source.unitPrice;
        value.sourceUnitPrice = source.sourceUnitPrice;
        value.destinationUnitPrice = source.destinationUnitPrice;
        value.transportCostPerUnit = source.transportCostPerUnit;
        value.transportCapacityPerUnit = source.transportCapacityPerUnit;
        value.railwayCapacityPricePerUnit = source.railwayCapacityPricePerUnit;
        value.railwayChargePerUnit = source.railwayChargePerUnit;
        value.railwayMarkupPerUnit = source.railwayMarkupPerUnit;
        value.warehouseMarginPerUnit = source.warehouseMarginPerUnit;
        value.distanceKm = source.distanceKm;
        value.transitCycles = source.transitCycles;
        value.active = source.active;
        value.railwayAvailable = source.railwayAvailable;
        value.profitable = source.profitable;
        value.usedCapacity =
            warehouseNetwork.lastCompletedRouteUsage(value.id);
        value.railwayRevenue =
            warehouseNetwork.lastCompletedRailwayRevenue(value.id);
        value.warehouseProfit =
            warehouseNetwork.lastCompletedWarehouseProfit(value.id);
        result.totalRailwayRevenue += value.railwayRevenue;
        result.totalWarehouseProfit += value.warehouseProfit;
        if (value.active && value.distanceKm > 0.0) {
            if (!value.railwayAvailable)
                ++result.railwayBlockedRoutes;
            else if (value.profitable)
                ++result.profitableRoutes;
            else
                ++result.unprofitableRoutes;
        }
        routeIndexById[value.id] = result.routes.size();
        result.totalCapacity += value.active ? value.capacityPerCycle : Money(0);
        result.totalUsedCapacity += value.usedCapacity;
        result.routes.push_back(std::move(value));
    }

    // The UI only needs active orders.  Parent/root IDs remain in every row,
    // so the inspector can keep lineage identifiers without scanning history.
    std::vector<WarehouseOrderId> activeOrderIds(
        warehouseNetwork.activeOrderIds().begin(),
        warehouseNetwork.activeOrderIds().end());
    std::sort(activeOrderIds.begin(), activeOrderIds.end());
    for (const WarehouseOrderId orderId : activeOrderIds) {
        const WarehouseOrder& source = warehouseNetwork.order(orderId);
        if (warehouseId >= 0 &&
            source.buyerWarehouseId != warehouseId &&
            source.sellerWarehouseId != warehouseId) {
            continue;
        }
        WarehouseOrderSnapshot value;
        value.id = source.id;
        value.rootDemandId = source.rootDemandId;
        value.parentOrderId = source.parentOrderId;
        value.kind = source.kind;
        value.status = source.status;
        value.createdCycle = source.createdCycle;
        value.eligibleCycle = source.eligibleCycle;
        value.buyerWarehouseId = source.buyerWarehouseId;
        value.sellerWarehouseId = source.sellerWarehouseId;
        value.routeId = source.routeId;
        value.buildingType = source.buildingType;
        value.goodIndex = source.goodIndex;
        value.requested = source.requested;
        value.locallyAllocated = source.locallyAllocated;
        value.accepted = source.accepted;
        value.reserved = source.reserved;
        value.shipped = source.shipped;
        value.received = source.received;
        value.contractPrice = source.contractPrice;
        value.sourceUnitPrice = source.sourceUnitPrice;
        value.destinationUnitPrice = source.destinationUnitPrice;
        value.transportCostPerUnit = source.transportCostPerUnit;
        value.transportCapacityPerUnit = source.transportCapacityPerUnit;
        value.railwayCapacityPricePerUnit = source.railwayCapacityPricePerUnit;
        value.railwayChargePerUnit = source.railwayChargePerUnit;
        value.railwayMarkupPerUnit = source.railwayMarkupPerUnit;
        value.warehouseMarginPerUnit = source.warehouseMarginPerUnit;
        value.escrowed = source.escrowed;
        value.profitableTrade = source.profitableTrade;
        result.orders.push_back(std::move(value));
        result.totalEscrow += value.escrowed;
    }

    for (const WarehouseOrderSnapshot& order : result.orders) {
        if (order.routeId < 0) continue;
        const auto route = routeIndexById.find(order.routeId);
        if (route == routeIndexById.end()) continue;
        const Money queued = std::max(Money(0), order.accepted - order.shipped);
        result.routes[route->second].queuedVolume += queued;
        result.totalQueuedVolume += queued;
    }

    for (const Shipment& source : warehouseNetwork.shipments()) {
        if (warehouseId >= 0 &&
            source.sourceWarehouseId != warehouseId &&
            source.destinationWarehouseId != warehouseId) {
            continue;
        }
        ShipmentSnapshot value;
        value.id = source.id;
        value.orderId = source.orderId;
        value.routeId = source.routeId;
        value.sourceWarehouseId = source.sourceWarehouseId;
        value.destinationWarehouseId = source.destinationWarehouseId;
        value.goodIndex = source.goodIndex;
        value.cargo = source.cargo;
        value.capacityUsed = source.capacityUsed;
        value.dispatchedCycle = source.dispatchedCycle;
        value.remainingCycles = source.remainingCycles;
        value.delivered = source.delivered;
        result.shipments.push_back(std::move(value));
    }

    for (const WarehouseTradeFlow& source :
         warehouseNetwork.completedTradeFlows()) {
        if (warehouseId >= 0 && source.sourceWarehouseId != warehouseId &&
            source.destinationWarehouseId != warehouseId) {
            continue;
        }
        TradeFlowSnapshot value;
        value.sourceWarehouseId = source.sourceWarehouseId;
        value.destinationWarehouseId = source.destinationWarehouseId;
        value.goodIndex = source.goodIndex;
        value.quantity = source.quantity;
        value.cycle = source.cycle;
        result.tradeFlows.push_back(std::move(value));
    }

    ++transportationSnapshotBuilds;
    auto inserted = transportationSnapshotCache.emplace(
        warehouseId, std::move(result));
    return inserted.first->second;
}

std::vector<WarehouseOrderSnapshot> World::getWarehouseOrderSnapshots(
    int warehouseId) const {
    return getTransportationSnapshot(warehouseId).orders;
}

std::vector<ShipmentSnapshot> World::getShipmentSnapshots(
    int warehouseId) const {
    return getTransportationSnapshot(warehouseId).shipments;
}
