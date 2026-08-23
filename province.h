#pragma once

#include "local_market.h"

#include <memory>
#include <string>

class World;

// A province is the player-facing geographic unit. Each province owns exactly
// one local market for its entire lifetime.
class Province {
public:
    Province(int id, std::string name, int regionId, int countryId,
             int localMarketId, std::string localMarketName,
             std::string key = "");

    int getId() const { return id; }
    const std::string& getKey() const { return key; }
    const std::string& getName() const { return name; }
    int getRegionId() const { return regionId; }
    int getCountryId() const { return countryId; }
    int getLocalMarketId() const { return localMarket->getMarketId(); }

    LocalMarket& getLocalMarket() { return *localMarket; }
    const LocalMarket& getLocalMarket() const { return *localMarket; }

private:
    friend class World;

    void setCountryId(int value) { countryId = value; }

    int id;
    std::string key;
    std::string name;
    int regionId;
    int countryId;
    std::unique_ptr<LocalMarket> localMarket;
};
