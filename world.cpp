#include "world.h"
#include "world_data.h"

#include <array>
#include <cmath>
#include <vector>

World& World::Instance() {
    static World instance;
    return instance;
}

Money World::totalMoneyInSystem() const {
    Money total = Money(0);
    for (const auto& province : provinces)
        total += province->getLocalMarket().moneyPoolsTotal();
    for (const auto& country : countries) total += country->getTreasury();
    total += warehouseNetwork.escrowBalance();
    return total;
}

Money World::getLaborerCashTotal() const {
    Money total = Money(0);
    for (const auto& province : provinces)
        total += province->getLocalMarket().getClassCash(LABORER);
    return total;
}

World& World::DebugFiveMarkets() {
    static World instance(true);
    return instance;
}

std::unique_ptr<World> World::CreateDebugWorldForTesting() {
    return std::unique_ptr<World>(new World(true));
}

World::World() : World(false) {}

World::World(bool debugFiveMarkets)
    : constructionService(*this),
      constructionSystem(*this),
      debugFiveMarketScenario(debugFiveMarkets) {
    // The domestic-market contract is enforced at the command layer
    // (addTradePath/addRailTradePath). Installing the same ownership rule on the
    // warehouse network stops any internal caller from creating a route that
    // crosses a country border.
    warehouseNetwork.setCountryResolver([this](WarehouseId marketId) {
        const auto indexIt = provinceIndexByMarketId.find(marketId);
        if (indexIt == provinceIndexByMarketId.end()) return -1;
        return provinces[static_cast<std::size_t>(indexIt->second)]
            ->getCountryId();
    });
    if (debugFiveMarkets) {
        populateDebugFiveMarkets();
    } else {
        WorldData::populate(*this);
        populateStandardCountryMarkets();
    }
}

void World::populateDebugFiveMarkets() {
    static const std::array<const char*, 5> names = {
        "调试市场 A", "调试市场 B", "调试市场 C",
        "调试市场 D", "调试市场 E"};
    static const std::array<std::vector<int>, 5> specialties = {{
        {FARM_GRAIN, FOOD_PROC},
        {COTTON, CLOTHES, LUXURY_CLOTHES},
        {COAL_MINE, IRON_MINE},
        {STEEL_MILL, TOOL_FACT},
        {HOUSING, CONST_DEPT, GOLD_MINE},
    }};

    // The debug scenario deliberately uses one country so every market shares
    // the same treasury, tax policy, and national construction queue.
    const int debugCountryId = createCountry("调试国", "debug_country", "DBG");
    for (const char* name : names) {
        const int provinceId = createProvince(name, -1, debugCountryId, name);
        if (provinceId < 0) continue;
    }
    if (debugCountryId >= 0) {
        Country& debugCountry = getCountryById(debugCountryId);
        TransactionTaxPolicy policy;
        policy.rate = Money(0.01);
        policy.startStep = 0;
        policy.endStep = -1;
        debugCountry.setTransactionTaxPolicy(policy);
        debugCountry.setTreasuryForSetup(Money(500000000.0));
    }
    for (int marketIndex = 0; marketIndex < getMarketCount(); ++marketIndex) {
        LocalMarket& market = getMarket(marketIndex);
        for (int type = FARM_GRAIN; type <= GOLD_MINE; ++type) {
            const OwnerType owner = (type == CONST_DEPT ||
                                     (type == FARM_GRAIN && marketIndex == 0))
                ? OWNER_GOVERNMENT : OWNER_INITIAL;
            market.setBuildingCountForSetup(type, 0, owner);
        }
        for (const int type : specialties[static_cast<std::size_t>(marketIndex)]) {
            const OwnerType owner = (type == CONST_DEPT ||
                                     (type == FARM_GRAIN && marketIndex == 0))
                ? OWNER_GOVERNMENT : OWNER_INITIAL;
            market.setBuildingCountForSetup(type, 60, owner);
        }
        // The debug markets are intentionally connected by rail so every
        // specialist can replenish imported production inputs.  Dynamic rail
        // routes otherwise remain unprofitable when both endpoints have no
        // railway level, hiding the long-run construction regression.
        market.setBuildingCountForSetup(RAILWAY, INITIAL_RAILWAY_LEVELS,
                                        OWNER_INITIAL);
        // The diagnostic scenario needs observable arbitrage from week one:
        // specialist markets start as low-cost origins, while non-specialist
        // markets expose the higher local willingness to pay. Prices remain
        // fully endogenous after this deterministic seed.
        for (int good = 0; good < NUM_GOODS; ++good)
            market.setPriceForSetup(
                good, Money(referencePrice[good] * 1.20));
        for (const int type : specialties[static_cast<std::size_t>(marketIndex)]) {
            const int outputGood = market.getBuildingTemplates()[type].outputGood;
            if (outputGood >= 0)
                market.setPriceForSetup(
                    outputGood, Money(referencePrice[outputGood] * 0.65));
        }
        market.finalizeDebugSetup();
    }

    // SupplyRoute is directed and commodity-specific. A complete undirected
    // five-market graph is therefore represented by 20 directions per good.
    // The debug scenario is a closed, deliberately specialized economy.  Use
    // a small but non-zero rail tariff so the seeded price spreads remain
    // tradable after the first endogenous price update; the production chain
    // would otherwise deadlock when a 400 km route costs more than the
    // specialist/non-specialist price spread. Production code and external
    // rail routes use the configured distance-cost coefficient.
    const Money kDebugRailCapacityCoefficient =
        Money(RAIL_DISTANCE_CAPACITY_COEFFICIENT);
    for (int source = 0; source < getMarketCount(); ++source) {
        for (int target = 0; target < getMarketCount(); ++target) {
            if (source == target) continue;
            for (int good = 0; good < NUM_GOODS; ++good) {
                if (good == CONSTR_GOOD_INDEX ||
                    good == TRANSPORT_CAPACITY_GOOD_INDEX) continue;
                const double distanceKm =
                    180.0 + 220.0 * std::abs(source - target);
                addRailTradePath(getMarket(source).getMarketId(),
                                 getMarket(target).getMarketId(), good,
                                 Money(1000000), distanceKm,
                                 kDebugRailCapacityCoefficient);
            }
        }
    }
}
void World::populateStandardCountryMarkets() {
    const Money maxRailVolume = Money(1000000);
    const Money railCapacityCoefficient =
        Money(RAIL_DISTANCE_CAPACITY_COEFFICIENT);

    for (int countryIndex = 0; countryIndex < getCountryCount(); ++countryIndex) {
        Country& country = getCountry(countryIndex);
        const auto& provinceIds = country.getProvinceIds();
        if (provinceIds.empty()) continue;

        const std::size_t marketCount = provinceIds.size();
        for (std::size_t marketIndex = 0; marketIndex < marketCount;
             ++marketIndex) {
            LocalMarket& market = getProvinceById(
                provinceIds[marketIndex]).getLocalMarket();
            for (int good = 0; good < NUM_GOODS; ++good) {
                market.setPriceForSetup(
                    good, Money(referencePrice[good] * 1.20));
            }
            const auto& counts = market.getBuildingCounts();
            for (int type = 0; type < TYPE_COUNT; ++type) {
                if (counts[type] <= 0) continue;
                const int outputGood =
                    market.getBuildingTemplates()[type].outputGood;
                if (outputGood >= 0) {
                    market.setPriceForSetup(
                        outputGood,
                        Money(referencePrice[outputGood] * 0.65));
                }
            }
        }

        for (std::size_t sourceIndex = 0; sourceIndex < marketCount;
             ++sourceIndex) {
            for (std::size_t targetIndex = 0; targetIndex < marketCount;
                 ++targetIndex) {
                if (sourceIndex == targetIndex) continue;
                const double distanceKm =
                    180.0 + 220.0 * std::abs(
                        static_cast<int>(sourceIndex) -
                        static_cast<int>(targetIndex));
                const int sourceMarket = getProvinceById(
                    provinceIds[sourceIndex]).getLocalMarketId();
                const int targetMarket = getProvinceById(
                    provinceIds[targetIndex]).getLocalMarketId();
                for (int good = 0; good < NUM_GOODS; ++good) {
                    if (good == CONSTR_GOOD_INDEX ||
                        good == TRANSPORT_CAPACITY_GOOD_INDEX) {
                        continue;
                    }
                    addRailTradePath(sourceMarket, targetMarket, good,
                                     maxRailVolume, distanceKm,
                                     railCapacityCoefficient);
                }
            }
        }

        for (const int provinceId : provinceIds) {
            getProvinceById(provinceId).getLocalMarket().finalizeStandardSetup();
        }
    }

    // Opening inventories are part of the model, not decoration: a market that
    // opens holding weeks of capacity-sized surplus throttles its producers to
    // zero within a few cycles and then never restarts them, because the same
    // throttle has stopped the consumers that would have drawn the surplus down.
    // Evaluate the requirement graph once every market has published its final
    // demand, then size the opening policy and stock on that requirement.
    for (int countryIndex = 0; countryIndex < getCountryCount(); ++countryIndex) {
        for (const int provinceId : getCountry(countryIndex).getProvinceIds()) {
            getProvinceById(provinceId)
                .getLocalMarket()
                .publishInitialFinalDemand();
        }
    }
    warehouseNetwork.refreshProductionForecasts();
    for (int countryIndex = 0; countryIndex < getCountryCount(); ++countryIndex) {
        for (const int provinceId : getCountry(countryIndex).getProvinceIds()) {
            getProvinceById(provinceId)
                .getLocalMarket()
                .reconcileInitialWarehouseDemand();
        }
    }
}
