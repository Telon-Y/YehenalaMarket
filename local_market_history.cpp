// ==================== local_market_history.cpp ====================
// 历史记录、库存变化、证券发行与转让
#include "local_market.h"
#include "local_market_internal.h"
#include <algorithm>
#include <cmath>

using namespace std;

void LocalMarket::recordInventoryChange(const std::array<Money, NUM_GOODS>& supply,
                                        const std::array<Money, NUM_GOODS>& demand) {
    // Production output, material delivery and consumption are all posted
    // directly to Warehouse. Retained for legacy callers without double entry.
    (void)supply;
    (void)demand;
}

void LocalMarket::recordHistory(const std::array<Money, NUM_GOODS>& realOut,
                                const std::array<Money, TYPE_COUNT>& buildingOutput,
                                Money gdp) {
    gdpHist.push_back(gdp);
    buildingHist.push_back(bld.getBuildingCounts());
    outputHist.push_back(realOut);
    priceHist.push_back(priceState.prices);
    cashPoolHist.push_back(bld.getCashPools());
    populationHist.push_back(laborPopulation);
    constexpr size_t MAX_HISTORY_POINTS = 20000;
    if (priceHist.size() > MAX_HISTORY_POINTS) {
        const size_t removeCount = MAX_HISTORY_POINTS / 2;
        historyFirstCycle += static_cast<int>(removeCount);
        priceHist.erase(priceHist.begin(), priceHist.begin() + removeCount);
        outputHist.erase(outputHist.begin(), outputHist.begin() + removeCount);
        buildingHist.erase(buildingHist.begin(), buildingHist.begin() + removeCount);
        gdpHist.erase(gdpHist.begin(), gdpHist.begin() + removeCount);
        cashPoolHist.erase(cashPoolHist.begin(), cashPoolHist.begin() + removeCount);
        populationHist.erase(populationHist.begin(), populationHist.begin() + removeCount);
    }
    latestRealOut = realOut;
    latestBuildingOutput = buildingOutput;
}
