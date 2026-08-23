#include "world.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

namespace {
int FloorAffordableUnits(double ratio) {
    if (!std::isfinite(ratio) || ratio < 1.0) return 0;
    const double floored = std::floor(ratio + 1.0e-9);
    return static_cast<int>(std::min(
        floored, static_cast<double>(std::numeric_limits<int>::max())));
}
}

int World::addTradePath(int sourceMarketId, int targetMarketId, int goodIndex,
                        Money maxVolumePerWeek, Money transportCostPerUnit) {
    if (sourceMarketId == targetMarketId) return -1;
    if (goodIndex < 0 || goodIndex >= NUM_GOODS) return -1;
    if (provinceIndexByMarketId.find(sourceMarketId) ==
        provinceIndexByMarketId.end()) return -1;
    if (provinceIndexByMarketId.find(targetMarketId) ==
        provinceIndexByMarketId.end()) return -1;

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

int World::queueNationalConstruction(
    int countryId, int provinceId, int typeIndex, int count,
    std::uint64_t* projectId, Money alreadyReservedBudget) {
    if (projectId != nullptr) *projectId = 0;
    if (count <= 0 || typeIndex < 0 || typeIndex >= TYPE_COUNT)
        return 0;
    auto countryIt = countryIndexById.find(countryId);
    auto provinceIt = provinceIndexById.find(provinceId);
    if (countryIt == countryIndexById.end() ||
        provinceIt == provinceIndexById.end()) return 0;

    Country& country = *countries[static_cast<std::size_t>(countryIt->second)];
    Province& province = *provinces[static_cast<std::size_t>(provinceIt->second)];
    if (province.getCountryId() != countryId ||
        province.getLocalMarket().getBuildingTemplates()[typeIndex].isFinancial)
        return 0;

    const Money constructionPrice =
        province.getLocalMarket().getPrices()[CONSTR_GOOD_INDEX];
    Money unitBudget = constructionPrice * Money(buildingCost[typeIndex]);
    if (!isfinite(unitBudget) || unitBudget <= Money(0)) return 0;

    if (!isfinite(alreadyReservedBudget) || alreadyReservedBudget < Money(0))
        return 0;

    const bool preReserved = alreadyReservedBudget > Money(0);
    int affordable = count;
    if (preReserved) {
        if (country.getReservedConstructionBudget() < alreadyReservedBudget)
            return 0;
        affordable = std::min(
            affordable, FloorAffordableUnits((alreadyReservedBudget / unitBudget).toDouble()));
    } else {
        affordable = std::min(
            affordable, FloorAffordableUnits((country.getAvailableTreasury() / unitBudget).toDouble()));
    }
    if (affordable <= 0) return 0;

    // A national order is atomic: the requested quantity either receives a
    // complete reservation or is rejected. This keeps the project identity
    // and the requested quantity stable across retries.
    if (affordable < count) return 0;
    const Money totalBudget = unitBudget * Money(affordable);
    if (preReserved && alreadyReservedBudget > totalBudget)
        country.releaseConstructionBudget(alreadyReservedBudget - totalBudget);
    if (!preReserved && !country.reserveConstructionBudget(totalBudget))
        return 0;
    if (preReserved && country.getReservedConstructionBudget() < totalBudget)
        return 0;

    NationalConstructionProject project;
    project.id = nextNationalConstructionProjectId++;
    project.payerCountryId = countryId;
    project.payerCountryTag = country.getCountryCode();
    project.targetProvinceId = provinceId;
    project.typeIndex = typeIndex;
    project.quantity = affordable;
    project.totalBudget = totalBudget;
    project.unitPrice = unitBudget / Money(buildingCost[typeIndex]);
    project.reservedBudget = totalBudget;
    project.totalConstruction = Money(buildingCost[typeIndex]) *
                                Money(affordable);
    project.remainingConstruction = project.totalConstruction;
    const LocalMarket& targetMarket = province.getLocalMarket();
    if (targetMarket.getBuildingCounts()[typeIndex] > 0) {
        project.expectedProfitPriority =
            targetMarket.getActualUnitProfits()[typeIndex].toDouble();
    } else {
        project.expectedProfitPriority =
            targetMarket.getSmoothedProfitRate()[typeIndex];
    }
    project.createdStep = province.getLocalMarket().getStepCount();
    if (!country.enqueueConstructionProject(std::move(project))) {
        country.releaseConstructionBudget(totalBudget);
        return 0;
    }
    if (projectId != nullptr)
        *projectId = country.getConstructionQueue().back().id;
    return affordable;
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
    auto countryIt = countryIndexById.find(countryId);
    if (countryIt == countryIndexById.end() || projectId == 0) return false;
    Country& country = *countries[static_cast<std::size_t>(countryIt->second)];
    for (NationalConstructionProject& project : country.constructionQueue) {
        if (project.id != projectId || !project.active()) continue;
        country.releaseConstructionBudget(project.reservedBudget);
        project.reservedBudget = Money(0);
        project.status = ConstructionProjectStatus::Cancelled;
        return true;
    }
    return false;
}

void World::prepareNationalConstructionPlans() {
    for (const auto& province : provinces)
        province->getLocalMarket().setConstructionOutputPlan(
            Money(0), Money(0), false);

    for (const auto& countryPtr : countries) {
        struct PlanRef {
            LocalMarket* market = nullptr;
            Money capacity = Money(0);
            Money sustainable = Money(0);
        };
        std::vector<PlanRef> refs;
        Money totalCapacity = Money(0);
        Money totalSustainable = Money(0);
        for (const int provinceId : countryPtr->getProvinceIds()) {
            const auto provinceIt = provinceIndexById.find(provinceId);
            if (provinceIt == provinceIndexById.end()) continue;
            LocalMarket& market = provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            const Money capacity = std::max(
                Money(0), market.constructionCapacityForPlan());
            const Money sustainable = std::min(
                capacity,
                std::max(Money(0),
                         market.constructionSustainableCapacityForPlan()));
            refs.push_back({&market, capacity, sustainable});
            totalCapacity += capacity;
            totalSustainable += sustainable;
        }
        if (refs.empty()) continue;

        // The queue is the single demand signal. Each project contributes at
        // most 30 construction units per building this cycle and cannot ask
        // for more than its still-reserved budget can settle.
        Money queueDemand = Money(0);
        for (const NationalConstructionProject& project :
             countryPtr->getConstructionQueue()) {
            if (!project.active()) continue;
            const Money price = project.unitPrice > Money(0)
                ? project.unitPrice : Money(0.01);
            const Money budgetCapacity = project.reservedBudget / price;
            const Money projectLimit =
                Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
                Money(std::max(1, project.quantity));
            queueDemand += std::min({project.remainingConstruction,
                                     projectLimit, budgetCapacity});
        }

        // Keep a bounded public standing capacity when no national project is
        // queued. This preserves a recoverable construction department, while
        // avoiding the old full-capacity/25%-inventory mismatch.
        const Money requested = queueDemand > Money(0)
            ? queueDemand
            : Money(COUNTRY_BASE_CONSTRUCTION_CAPACITY);
        // Material feasibility limits this cycle's output, but it must not
        // erase the queue-derived input forecast. Keeping those plans
        // separate lets an empty buffer place the shipments needed to recover.
        const Money inputTarget = std::min(requested, totalCapacity);
        const Money target = std::min(inputTarget, totalSustainable);
        Money remaining = std::max(Money(0), target);
        Money inputRemaining = std::max(Money(0), inputTarget);
        for (std::size_t index = 0; index < refs.size(); ++index) {
            PlanRef& ref = refs[index];
            Money allocation = Money(0);
            if (index + 1 == refs.size()) {
                allocation = remaining;
            } else if (totalSustainable > Money(0)) {
                allocation = target * ref.sustainable / totalSustainable;
            }
            allocation = std::clamp(allocation, Money(0), ref.capacity);

            Money inputAllocation = Money(0);
            if (index + 1 == refs.size()) {
                inputAllocation = inputRemaining;
            } else if (totalCapacity > Money(0)) {
                inputAllocation =
                    inputTarget * ref.capacity / totalCapacity;
            }
            inputAllocation = std::clamp(
                inputAllocation, Money(0), ref.capacity);
            ref.market->setConstructionOutputPlan(
                allocation, inputAllocation, true);
            remaining = std::max(Money(0), remaining - allocation);
            inputRemaining = std::max(
                Money(0), inputRemaining - inputAllocation);
        }
    }
}

void World::processNationalConstruction() {
    for (const auto& countryPtr : countries) {
        Country& country = *countryPtr;
        struct IndustrialSource {
            LocalMarket* market = nullptr;
            Money remaining = Money(0);
            std::size_t stateIndex = 0;
        };

        NationalConstructionPoolState& pool = country.constructionPoolState;
        pool = NationalConstructionPoolState{};
        std::vector<IndustrialSource> industrialSources;
        for (const int provinceId : country.getProvinceIds()) {
            const auto provinceIt = provinceIndexById.find(provinceId);
            if (provinceIt == provinceIndexById.end()) continue;
            LocalMarket& source = provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            const Money capacity = std::max(
                Money(0), source.getAvailableNationalConstructionCapacity());
            const Money available = std::min(capacity,
                                             source.getLastConstrProduced());
            // Construction power is a special, non-storable good. Only output
            // generated by this province during the current cycle can enter
            // the industrial national pool; warehouse stock is irrelevant.
            pool.industrialCapacity += available;
            NationalConstructionPoolSource sourceState;
            sourceState.provinceId = provinceId;
            sourceState.available = available;
            pool.industrialSources.push_back(sourceState);
            pool.industrialAvailable += available;
            if (available > Money(0)) {
                industrialSources.push_back({
                    &source, available, pool.industrialSources.size() - 1});
            }
        }

        // The base allocation is national, non-storable and non-transferable:
        // it fills an insufficient industrial capacity, but never replaces
        // stocked industrial construction goods above the floor.
        pool.baseSupplement = std::max(
            Money(0), Money(COUNTRY_BASE_CONSTRUCTION_CAPACITY) -
                          pool.industrialCapacity);
        Money industrialRemaining = pool.industrialAvailable;
        Money baseRemaining = pool.baseSupplement;

        for (NationalConstructionProject& project : country.constructionQueue) {
            if (!project.active()) continue;
            auto provinceIt = provinceIndexById.find(project.targetProvinceId);
            if (provinceIt == provinceIndexById.end() ||
                provinces[static_cast<std::size_t>(provinceIt->second)]
                        ->getCountryId() != country.getId()) {
                country.releaseConstructionBudget(project.reservedBudget);
                project.reservedBudget = Money(0);
                project.status = ConstructionProjectStatus::Blocked;
                continue;
            }

            LocalMarket& market = provinces[static_cast<std::size_t>(
                provinceIt->second)]->getLocalMarket();
            // The reservation locks the quoted construction price. A later
            // market-price change must not consume more budget than the
            // project identity reserved at enqueue time.
            const Money price = project.unitPrice > Money(0)
                ? project.unitPrice
                : std::max(Money(0.01),
                           market.getPrices()[CONSTR_GOOD_INDEX]);
            const Money budgetCapacity = project.reservedBudget / price;
            const Money treasuryCapacity = country.getTreasury() / price;
            // A national project may represent several buildings. Each
            // building has the same per-cycle construction throughput cap as
            // a local construction order.
            const Money projectCycleLimit =
                Money(CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE) *
                Money(std::max(1, project.quantity));
            Money requested = std::min({project.remainingConstruction,
                                        projectCycleLimit,
                                        industrialRemaining + baseRemaining,
                                        budgetCapacity, treasuryCapacity});
            if (requested <= Money(0)) continue;

            if (!country.canSettleConstructionPayment(requested * price))
                continue;

            struct ConsumedSource {
                LocalMarket* market = nullptr;
                Money amount = Money(0);
                std::size_t stateIndex = 0;
            };
            std::vector<ConsumedSource> consumedSources;
            Money industrialUsed = Money(0);
            Money remainingIndustrialRequest =
                std::min(requested, industrialRemaining);
            for (IndustrialSource& source : industrialSources) {
                if (remainingIndustrialRequest <= Money(0)) break;
                const Money planned = std::min(
                    source.remaining, remainingIndustrialRequest);
                const Money consumed =
                    source.market->consumeNationalConstruction(planned);
                if (consumed <= Money(0)) continue;
                source.remaining = std::max(Money(0), source.remaining - consumed);
                remainingIndustrialRequest -= consumed;
                industrialUsed += consumed;
                consumedSources.push_back(
                    {source.market, consumed, source.stateIndex});
            }
            const Money baseUsed = std::min(
                baseRemaining, std::max(Money(0), requested - industrialUsed));
            const Money used = industrialUsed + baseUsed;
            if (used <= Money(0)) continue;

            const Money payment = used * price;
            if (!country.settleConstructionPayment(payment)) {
                for (const ConsumedSource& source : consumedSources)
                    source.market->rollbackNationalConstruction(source.amount);
                continue;
            }

            industrialRemaining = std::max(
                Money(0), industrialRemaining - industrialUsed);
            baseRemaining = std::max(Money(0), baseRemaining - baseUsed);
            pool.industrialUsed += industrialUsed;
            pool.baseUsed += baseUsed;
            pool.baseExpenditure += baseUsed * price;
            for (const ConsumedSource& source : consumedSources) {
                NationalConstructionPoolSource& sourceState =
                    pool.industrialSources[source.stateIndex];
                sourceState.available = std::max(
                    Money(0), sourceState.available - source.amount);
                sourceState.used += source.amount;
                source.market->recordNationalConstructionSale(
                    source.amount, source.amount * price);
            }
            if (baseUsed > Money(0)) {
                market.recordNationalConstructionBase(
                    baseUsed, baseUsed * price);
            }
            project.reservedBudget = std::max(Money(0),
                                               project.reservedBudget - payment);
            project.paidBudget += payment;
            project.remainingConstruction = std::max(
                Money(0), project.remainingConstruction - used);
            project.status = ConstructionProjectStatus::Active;
            project.lastSettledStep = market.getStepCount();

            if (project.remainingConstruction <= Money(1e-9)) {
                market.completeNationalConstruction(project.typeIndex,
                                                    project.quantity);
                country.releaseConstructionBudget(project.reservedBudget);
                project.reservedBudget = Money(0);
                project.status = ConstructionProjectStatus::Completed;
            }
        }
        // Terminal projects are no longer live queue entries. Removing them
        // here prevents completed work from appearing as pending forever.
        country.pruneFinishedConstructionProjects();
    }
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

    constexpr int maxProjectsPerCountry = 200;
    constexpr int maxProjectsPerType = 50;
    for (const auto& countryPtr : countries) {
        Country& country = *countryPtr;
        const auto& provinceIds = country.getProvinceIds();
        if (provinceIds.empty()) continue;

        bool due = false;
        for (const int provinceId : provinceIds) {
            const LocalMarket& market =
                getProvinceById(provinceId).getLocalMarket();
            if (market.getStepCount() % AI_INTERVAL == 0) {
                due = true;
                break;
            }
        }
        if (!due) continue;

        int activeProjects = 0;
        std::array<int, TYPE_COUNT> activeProjectsByType{};
        for (const NationalConstructionProject& project :
             country.getConstructionQueue()) {
            if (!project.active()) continue;
            ++activeProjects;
            if (project.typeIndex >= 0 && project.typeIndex < TYPE_COUNT)
                ++activeProjectsByType[project.typeIndex];
        }
        // National projects do not live in a province's legacy private
        // construction queue. Track their quantities explicitly so the
        // candidate list cannot be reused repeatedly within this same AI
        // pass against unchanged building counts.
        std::unordered_map<std::uint64_t, int> pendingUnits;
        const auto candidateKey = [](int provinceId, int typeIndex) {
            return (static_cast<std::uint64_t>(
                        static_cast<std::uint32_t>(provinceId)) << 32) |
                   static_cast<std::uint32_t>(typeIndex);
        };
        for (const NationalConstructionProject& project :
             country.getConstructionQueue()) {
            if (!project.active() || project.typeIndex < 0 ||
                project.typeIndex >= TYPE_COUNT) {
                continue;
            }
            pendingUnits[candidateKey(project.targetProvinceId,
                                       project.typeIndex)] += project.quantity;
        }
        int placedProjects = 0;
        while (placedProjects < maxProjectsPerCountry &&
               activeProjects < maxProjectsPerCountry) {
            std::vector<CandidateRef> candidates;
            for (const int provinceId : provinceIds) {
                LocalMarket& market =
                    getProvinceById(provinceId).getLocalMarket();
                for (const AIExpansionCandidate& candidate :
                     market.getAIExpansionCandidates()) {
                    if (candidate.maxUnits <= 0) continue;
                    candidates.push_back({&market, provinceId, candidate});
                }
            }

            std::sort(candidates.begin(), candidates.end(),
                      [](const CandidateRef& left, const CandidateRef& right) {
                const auto isProductionExpansion =
                    [](const CandidateRef& ref) {
                    if (ref.market == nullptr || ref.candidate.typeIndex < 0 ||
                        ref.candidate.typeIndex >= TYPE_COUNT)
                        return false;
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

            bool approved = false;
            for (const CandidateRef& ref : candidates) {
                const std::uint64_t key =
                    candidateKey(ref.provinceId, ref.candidate.typeIndex);
                const int queuedUnits = pendingUnits[key];
                const int availableCandidateUnits =
                    ref.candidate.maxUnits - queuedUnits;
                if (availableCandidateUnits <= 0) continue;
                if (ref.market == nullptr ||
                    ref.candidate.unitConstructionCost <= Money(0) ||
                    ref.candidate.typeIndex < 0 ||
                    ref.candidate.typeIndex >= TYPE_COUNT ||
                    activeProjectsByType[ref.candidate.typeIndex] >=
                        maxProjectsPerType) {
                    continue;
                }
                const bool privateExpansion =
                    ref.market->getBuildingCounts()[ref.candidate.typeIndex] > 0;
                const Money startup = privateExpansion
                    ? expansionStartupCapital(ref.candidate.typeIndex)
                    : Money(0);
                if (startup > Money(0) &&
                    (!isfinite(ref.market->getInvestmentPool()) ||
                     ref.market->getInvestmentPool() < startup))
                    continue;
                const Money constructionPrice =
                    ref.market->getPrices()[CONSTR_GOOD_INDEX];
                Money unitBudget =
                    constructionPrice * ref.candidate.unitConstructionCost;
                if (unitBudget < Money(0.01)) unitBudget = Money(0.01);
                if (!isfinite(unitBudget) || unitBudget <= Money(0))
                    continue;

                const Money available = country.getAvailableTreasury();
                const double affordableDouble =
                    (available / unitBudget).toDouble();
                if (!std::isfinite(affordableDouble) ||
                    affordableDouble < 1.0) {
                    continue;
                }
                const int affordable = std::min(
                    availableCandidateUnits,
                    FloorAffordableUnits(affordableDouble));
                if (affordable <= 0) continue;

                const Money reservation = unitBudget * Money(affordable);
                if (!country.reserveConstructionBudget(reservation))
                    continue;
                const int actual = ref.market->placeGovernmentExpansion(
                    ref.candidate.typeIndex, affordable, unitBudget);
                if (actual < affordable) {
                    country.releaseConstructionBudget(
                        unitBudget * Money(affordable - actual));
                }
                if (actual > 0) {
                    if (startup > Money(0))
                        ref.market->payAIExpansionStartup(ref.candidate.typeIndex);
                    ++placedProjects;
                    ++activeProjects;
                    ++activeProjectsByType[ref.candidate.typeIndex];
                    pendingUnits[key] += actual;
                    approved = true;
                    break;
                }
                country.releaseConstructionBudget(reservation);
            }
            if (!approved) break;
        }
    }
}
void World::stepAll(bool runAI) {
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
}
