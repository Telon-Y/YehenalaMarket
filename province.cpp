#include "province.h"

#include <utility>

Province::Province(int id, std::string name, int regionId, int countryId,
                   int localMarketId, std::string localMarketName,
                   std::string key)
    : id(id),
      key(std::move(key)),
      name(std::move(name)),
      regionId(regionId),
      countryId(countryId),
      localMarket(std::make_unique<LocalMarket>(localMarketId,
                                                std::move(localMarketName))) {}
