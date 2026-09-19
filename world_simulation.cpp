#include "world.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

int World::addTradePath(int sourceMarketId, int targetMarketId, int goodIndex,
                        Money maxVolumePerWeek, Money transportCostPerUnit) {
    if (sourceMarketId == targetMarketId) return -1;
    if (goodIndex < 0 || goodIndex >= NUM_GOODS) return -1;
    if (provinceIndexByMarketId.find(sourceMarketId) ==
        provinceIndexByMarketId.end()) return -1;
    if (provinceIndexByMarketId.find(targetMarketId) ==
        provinceIndexByMarketId.end()) return -1;
    const Province& sourceProvince = *provinces[static_cast<std::size_t>(
        provinceIndexByMarketId.at(sourceMarketId))];
    const Province& targetProvince = *provinces[static_cast<std::size_t>(
        provinceIndexByMarketId.at(targetMarketId))];
    if (sourceProvince.getCountryId() < 0 ||
        sourceProvince.getCountryId() != targetProvince.getCountryId())
        return -1;

    const Money unitPrice =
        getMarketById(sourceMarketId).getPrices()[goodIndex] +
        transportCostPerUnit;
    const int routeId = warehouseNetwork.addRoute(
        sourceMarketId, targetMarketId, goodIndex, maxVolumePerWeek,
        unitPrice, 1);
    if (routeId < 0) return -1;

    const int id = nextTradePathId++;
    TradePath path;
    path.id = id;
    path.routeId = routeId;
    path.sourceMarketId = sourceMarketId;
    path.targetMarketId = targetMarketId;
    path.goodIndex = goodIndex;
    path.maxVolumePerWeek = maxVolumePerWeek;
    path.transportCostPerUnit = transportCostPerUnit;
    path.active = true;
    tradePaths.push_back(path);
    return id;
}

int World::addRailTradePath(int sourceMarketId, int targetMarketId,
                            int goodIndex, Money maxVolumePerWeek,
                            double distanceKm, Money capacityCoefficient) {
    if (sourceMarketId == targetMarketId || goodIndex < 0 ||
        goodIndex >= NUM_GOODS || !std::isfinite(distanceKm) ||
        distanceKm <= 0.0 || !isfinite(capacityCoefficient) ||
        capacityCoefficient < Money(0)) {
        return -1;
    }
    if (provinceIndexByMarketId.find(sourceMarketId) ==
            provinceIndexByMarketId.end() ||
        provinceIndexByMarketId.find(targetMarketId) ==
            provinceIndexByMarketId.end()) {
        return -1;
    }
    const Province& sourceProvince = *provinces[static_cast<std::size_t>(
        provinceIndexByMarketId.at(sourceMarketId))];
    const Province& targetProvince = *provinces[static_cast<std::size_t>(
        provinceIndexByMarketId.at(targetMarketId))];
    if (sourceProvince.getCountryId() < 0 ||
        sourceProvince.getCountryId() != targetProvince.getCountryId())
        return -1;

    const int routeId = warehouseNetwork.addRailRoute(
        sourceMarketId, targetMarketId, goodIndex, maxVolumePerWeek,
        distanceKm, capacityCoefficient);
    if (routeId < 0) return -1;

    TradePath path;
    path.id = nextTradePathId++;
    path.routeId = routeId;
    path.sourceMarketId = sourceMarketId;
    path.targetMarketId = targetMarketId;
    path.goodIndex = goodIndex;
    path.maxVolumePerWeek = maxVolumePerWeek;
    const SupplyRoute& route = warehouseNetwork.routes().back();
    path.transportCostPerUnit = route.transportCostPerUnit;
    path.transportCapacityPerUnit = route.transportCapacityPerUnit;
    path.railwayCapacityPricePerUnit = route.railwayCapacityPricePerUnit;
    path.distanceKm = distanceKm;
    path.railwayRequired = true;
    tradePaths.push_back(path);
    return path.id;
}

ConstructionCommandResult World::evaluateNationalConstruction(
    int countryId, int provinceId, int typeIndex, int count) const {
    ConstructionRequest request;
    request.countryId = countryId;
    request.targetProvinceId = provinceId;
    request.typeIndex = typeIndex;
    request.quantity = count;
    request.funding = {
        ConstructionFundingKind::CountryTreasury, countryId, -1};
    request.owner = {OWNER_GOVERNMENT, countryId, provinceId};
    const ConstructionQuote quoted = constructionService.quote(request);
    ConstructionCommandResult result;
    result.error = quoted.error;
    result.acceptedCount = quoted.acceptedCount;
    result.unitBudget =
        quoted.constructionPointsPerUnit * quoted.observedUnitPrice;
    result.totalBudget = quoted.totalBudget;
    return result;
}
ConstructionCommandResult World::queueNationalConstructionCommand(
    int countryId, int provinceId, int typeIndex, int count,
    Money alreadyReservedBudget) {
    if (alreadyReservedBudget > Money(0)) {
        ConstructionCommandResult rejected;
        rejected.error = ConstructionCommandError::InvalidReservation;
        return rejected;
    }
    ConstructionRequest request;
    request.playerInitiated = true;
    request.countryId = countryId;
    request.targetProvinceId = provinceId;
    request.typeIndex = typeIndex;
    request.quantity = count;
    request.funding = {
        ConstructionFundingKind::CountryTreasury, countryId, -1};
    request.owner = {OWNER_GOVERNMENT, countryId, provinceId};
    return constructionService.submit(request);
}
int World::queueNationalConstruction(
    int countryId, int provinceId, int typeIndex, int count,
    std::uint64_t* projectId, Money alreadyReservedBudget) {
    if (projectId != nullptr) *projectId = 0;
    const ConstructionCommandResult result =
        queueNationalConstructionCommand(
            countryId, provinceId, typeIndex, count,
            alreadyReservedBudget);
    if (projectId != nullptr) *projectId = result.projectId;
    return result.acceptedCount;
}

std::uint64_t World::createNationalConstructionProject(
    int countryId, int provinceId, int typeIndex, int count) {
    std::uint64_t id = 0;
    if (queueNationalConstruction(countryId, provinceId, typeIndex, count,
                                  &id, Money(0)) <= 0) return 0;
    return id;
}

bool World::cancelNationalConstructionProject(int countryId,
                                               std::uint64_t projectId) {
    return constructionService.cancel(countryId, projectId);
}
void World::prepareNationalConstructionPlans() {
    constructionSystem.preparePlans();
}
void World::processNationalConstruction() {
    constructionSystem.processCycle();
}
void World::executeTrade() {
    warehouseNetwork.beginLogisticsBatch();
    warehouseNetwork.routeShortages();
    warehouseNetwork.confirmAndReserve();
    warehouseNetwork.dispatch();
    warehouseNetwork.endLogisticsBatch();
}

void World::runNationalExpansionAI() {
    struct CandidateRef {
        LocalMarket* market = nullptr;
        int provinceId = -1;
        AIExpansionCandidate candidate;
    };

    constexpr int maxNewProjectsPerCountry = 20;
    constexpr int maxActiveProjectsPerCountry = 200;
    constexpr int maxProjectsPerType = 50;
    for (const auto& countryPtr : countries) {
        Country& country = *countryPtr;
        const auto& provinceIds = country.getProvinceIds();
        if (provinceIds.empty()) continue;

        const int currentCycle =
            getProvinceById(provinceIds.front()).getLocalMarket().getStepCount();
        const auto lastCycle =
            lastExpansionAICycleByCountry.find(country.getId());
        if (lastCycle != lastExpansionAICycleByCountry.end() &&
            currentCycle - lastCycle->second < AI_INTERVAL) {
            continue;
        }
        lastExpansionAICycleByCountry[country.getId()] = currentCycle;

        int activeProjects = 0;
        std::array<int, TYPE_COUNT> activeProjectsByType{};
        for (const NationalConstructionProject& project :
             country.getConstructionQueue()) {
            if (!project.active()) continue;
            ++activeProjects;
            if (project.typeIndex >= 0 && project.typeIndex < TYPE_COUNT)
                ++activeProjectsByType[project.typeIndex];
        }
        std::unordered_map<int, std::array<int, TYPE_COUNT>>
            pendingUnitsByProvince;
        Money totalRemainingConstruction = Money(0);
        for (const NationalConstructionProject& project :
             country.getConstructionQueue()) {
            if (!project.live() || project.typeIndex < 0 ||
                project.typeIndex >= TYPE_COUNT) {
                continue;
            }
            pendingUnitsByProvince[project.targetProvinceId][project.typeIndex] +=
                project.remainingUnits();
            totalRemainingConstruction += project.remainingConstruction;
        }
        if (activeProjects >= maxActiveProjectsPerCountry) continue;

        std::vector<CandidateRef> candidates;
        candidates.reserve(provinceIds.size() * TYPE_COUNT);
        for (const int provinceId : provinceIds) {
            LocalMarket& market =
                getProvinceById(provinceId).getLocalMarket();
            const auto pendingIt = pendingUnitsByProvince.find(provinceId);
            const std::array<int, TYPE_COUNT> emptyPending{};
            const auto& pendingCounts = pendingIt == pendingUnitsByProvince.end()
                ? emptyPending : pendingIt->second;
            for (const AIExpansionCandidate& candidate :
                 market.getAIExpansionCandidates(
                     pendingCounts, totalRemainingConstruction)) {
                if (candidate.maxUnits > 0)
                    candidates.push_back({&market, provinceId, candidate});
            }
        }

        std::sort(candidates.begin(), candidates.end(),
                  [](const CandidateRef& left, const CandidateRef& right) {
            const auto isProductionExpansion = [](const CandidateRef& ref) {
                if (ref.market == nullptr || ref.candidate.typeIndex < 0 ||
                    ref.candidate.typeIndex >= TYPE_COUNT) {
                    return false;
                }
                const BuildingTemplate& building =
                    ref.market->getBuildingTemplates()[ref.candidate.typeIndex];
                return ref.market->getBuildingCounts()[ref.candidate.typeIndex] > 0 &&
                       !building.isDevelopment() && !building.isFinancial;
            };
            const bool leftExpansion = isProductionExpansion(left);
            const bool rightExpansion = isProductionExpansion(right);
            if (leftExpansion != rightExpansion)
                return leftExpansion > rightExpansion;
            if (left.candidate.priority != right.candidate.priority)
                return left.candidate.priority > right.candidate.priority;
            if (left.provinceId != right.provinceId)
                return left.provinceId < right.provinceId;
            return left.candidate.typeIndex < right.candidate.typeIndex;
        });

        int placedProjects = 0;
        for (const CandidateRef& ref : candidates) {
            if (placedProjects >= maxNewProjectsPerCountry ||
                activeProjects >= maxActiveProjectsPerCountry) {
                break;
            }
            if (ref.market == nullptr ||
                ref.candidate.unitConstructionCost <= Money(0) ||
                ref.candidate.typeIndex < 0 ||
                ref.candidate.typeIndex >= TYPE_COUNT) {
                continue;
            }
            const int availableUnits = std::min({
                ref.candidate.maxUnits,
                maxNewProjectsPerCountry - placedProjects,
                maxActiveProjectsPerCountry - activeProjects,
                maxProjectsPerType -
                    activeProjectsByType[ref.candidate.typeIndex]});
            if (availableUnits <= 0) continue;

            const bool privateExpansion =
                ref.market->getBuildingCounts()[ref.candidate.typeIndex] > 0;
            for (int unit = 0; unit < availableUnits; ++unit) {
                ConstructionRequest request;
                request.countryId = country.getId();
                request.targetProvinceId = ref.provinceId;
                request.typeIndex = ref.candidate.typeIndex;
                request.quantity = 1;
                request.priority = static_cast<int>(std::clamp(
                    ref.candidate.priority * 1000.0,
                    static_cast<double>(std::numeric_limits<int>::min()),
                    static_cast<double>(std::numeric_limits<int>::max())));
                if (privateExpansion) {
                    request.funding = {
                        ConstructionFundingKind::ProvinceInvestmentPool,
                        country.getId(), ref.provinceId};
                    request.owner = {
                        OWNER_FINANCE, country.getId(), ref.provinceId};
                } else {
                    request.funding = {
                        ConstructionFundingKind::CountryTreasury,
                        country.getId(), -1};
                    request.owner = {
                        OWNER_GOVERNMENT, country.getId(), ref.provinceId};
                }
                if (!constructionService.submit(request)) break;

                ++placedProjects;
                ++activeProjects;
                ++activeProjectsByType[ref.candidate.typeIndex];
                ++pendingUnitsByProvince[ref.provinceId]
                                            [ref.candidate.typeIndex];
                totalRemainingConstruction +=
                    Money(buildingCost[ref.candidate.typeIndex]);
            }
        }
    }
}
void World::stepAll(bool runAI) {
    // Money audit. Seigniorage is the only path that creates money, so any
    // other change in the total is money that moved without a receiver.
    const Money moneyBefore = totalMoneyInSystem();
    if (moneyOpeningTotal <= Money(0) && moneyCreatedTotal <= Money(0) &&
        moneyResidual == Money(0)) {
        moneyOpeningTotal = moneyBefore;
    }
    Money createdThisCycle = Money(0);
    prepareNationalConstructionPlans();
    for (auto& province : provinces) {
        province->getLocalMarket().beginFlowTrace(
            warehouseNetwork.currentCycle());
    }
    warehouseNetwork.beginLogisticsBatch();
    warehouseNetwork.receive();
    for (auto& province : provinces)
        province->getLocalMarket().prepareWarehouseCycle();
    warehouseNetwork.refreshProductionForecasts();
    for (auto& province : provinces)
        province->getLocalMarket().planWarehouseReplenishments();
    warehouseNetwork.processProductionOrders();
    warehouseNetwork.allocateLocal();
    warehouseNetwork.routeShortages();
    warehouseNetwork.confirmAndReserve();
    warehouseNetwork.dispatch();
    warehouseNetwork.receive();

    for (auto& province : provinces) {
        LocalMarket& market = province->getLocalMarket();
        market.step();
        if (runAI && market.getStepCount() % AI_INTERVAL == 0 &&
            market.getFiscalCountry() == nullptr)
            market.aiBuild();
    }
    if (runAI) runNationalExpansionAI();
    processNationalConstruction();
    warehouseNetwork.advanceTransit();
    warehouseNetwork.finishCycle();
    warehouseNetwork.endLogisticsBatch();
    for (auto& province : provinces) {
        LocalMarket& market = province->getLocalMarket();
        market.finalizeFlowTrace(
            warehouseNetwork.lastCompletedFlow(market.getMarketId()));
    }

    for (const auto& province : provinces)
        createdThisCycle +=
            province->getLocalMarket().getSeigniorageThisCycle();
    const Money moneyAfter = totalMoneyInSystem();
    const Money cycleResidual = (moneyAfter - moneyBefore) - createdThisCycle;
    moneyCreatedTotal += createdThisCycle;
    moneyResidual += cycleResidual;
    moneyLastCycleResidual = cycleResidual;
    if (cycleResidual.abs() > moneyWorstCycleResidual.abs()) {
        moneyWorstCycleResidual = cycleResidual;
        moneyWorstCycle = warehouseNetwork.currentCycle();
    }
}
