#include "world.h"
#include "world_data.h"

#include <array>
#include <cmath>
#include <vector>

World& World::Instance() {
    static World instance;
    return instance;
}

World& World::DebugFiveMarkets() {
    static World instance(true);
    return instance;
}

World::World() : World(false) {}

World::World(bool debugFiveMarkets)
    : debugFiveMarketScenario(debugFiveMarkets) {
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
namespace {

const std::array<std::vector<int>, 5> kFiveMarketSpecialties = {{
    {FARM_GRAIN, FOOD_PROC},
    {COTTON, CLOTHES, LUXURY_CLOTHES},
    {COAL_MINE, IRON_MINE},
    {STEEL_MILL, TOOL_FACT},
    {HOUSING, CONST_DEPT, GOLD_MINE},
}};


}  // namespace
void World::populateStandardCountryMarkets() {
    const Money maxRailVolume = Money(1000000);
    const Money railCapacityCoefficient =
        Money(RAIL_DISTANCE_CAPACITY_COEFFICIENT);

    for (int countryIndex = 0; countryIndex < getCountryCount(); ++countryIndex) {
        Country& country = getCountry(countryIndex);
        const auto& provinceIds = country.getProvinceIds();
        if (provinceIds.empty()) continue;

        const std::size_t marketCount = provinceIds.size();
        std::vector<std::vector<int>> groupsForMarket(marketCount);
        if (marketCount >= kFiveMarketSpecialties.size()) {
            for (std::size_t marketIndex = 0; marketIndex < marketCount;
                 ++marketIndex) {
                groupsForMarket[marketIndex].push_back(
                    static_cast<int>(marketIndex % kFiveMarketSpecialties.size()));
            }
        } else {
            // A small country keeps all five specialties by allowing multiple
            // template groups to share a local market.
            for (std::size_t groupIndex = 0;
                 groupIndex < kFiveMarketSpecialties.size(); ++groupIndex) {
                groupsForMarket[groupIndex % marketCount].push_back(
                    static_cast<int>(groupIndex));
            }
        }

        for (std::size_t marketIndex = 0; marketIndex < marketCount;
             ++marketIndex) {
            LocalMarket& market = getProvinceById(
                provinceIds[marketIndex]).getLocalMarket();
            for (int type = FARM_GRAIN; type <= GOLD_MINE; ++type) {
                const OwnerType owner = type == CONST_DEPT
                    ? OWNER_GOVERNMENT : OWNER_INITIAL;
                market.setBuildingCountForSetup(type, 0, owner);
            }
            for (const int groupIndex : groupsForMarket[marketIndex]) {
                for (const int type : kFiveMarketSpecialties[
                         static_cast<std::size_t>(groupIndex)]) {
                    const OwnerType owner = type == CONST_DEPT
                        ? OWNER_GOVERNMENT : OWNER_INITIAL;
                    market.setBuildingCountForSetup(type, 60, owner);
                }
            }
            market.setBuildingCountForSetup(
                RAILWAY, INITIAL_RAILWAY_LEVELS, OWNER_INITIAL);

            for (int good = 0; good < NUM_GOODS; ++good) {
                market.setPriceForSetup(
                    good, Money(referencePrice[good] * 1.20));
            }
            for (const int groupIndex : groupsForMarket[marketIndex]) {
                for (const int type : kFiveMarketSpecialties[
                         static_cast<std::size_t>(groupIndex)]) {
                    const int outputGood =
                        market.getBuildingTemplates()[type].outputGood;
                    if (outputGood >= 0) {
                        market.setPriceForSetup(
                            outputGood,
                            Money(referencePrice[outputGood] * 0.65));
                    }
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
}
