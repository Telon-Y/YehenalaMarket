#pragma once

#include <vector>

class World;

// A country's market is the membership boundary for its local markets.
// Aggregation and domestic trade policies will be added on top of this frame.
class NationalMarket {
public:
    NationalMarket(int id, int countryId);

    int getId() const { return id; }
    int getCountryId() const { return countryId; }
    const std::vector<int>& getLocalMarketIds() const { return localMarketIds; }
    bool containsLocalMarket(int localMarketId) const;

private:
    friend class World;

    void addLocalMarket(int localMarketId);
    void removeLocalMarket(int localMarketId);

    int id;
    int countryId;
    std::vector<int> localMarketIds;
};
