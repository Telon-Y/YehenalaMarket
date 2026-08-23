#include "national_market.h"

#include <algorithm>

NationalMarket::NationalMarket(int id, int countryId)
    : id(id), countryId(countryId) {}

bool NationalMarket::containsLocalMarket(int localMarketId) const {
    return std::find(localMarketIds.begin(), localMarketIds.end(), localMarketId) !=
           localMarketIds.end();
}

void NationalMarket::addLocalMarket(int localMarketId) {
    if (!containsLocalMarket(localMarketId))
        localMarketIds.push_back(localMarketId);
}

void NationalMarket::removeLocalMarket(int localMarketId) {
    localMarketIds.erase(
        std::remove(localMarketIds.begin(), localMarketIds.end(), localMarketId),
        localMarketIds.end());
}
