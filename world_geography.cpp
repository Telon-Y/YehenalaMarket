#include "world_geography.h"

#include <algorithm>
#include <utility>

Continent::Continent(int id, std::string key, std::string name)
    : id(id), key(std::move(key)), name(std::move(name)) {}

bool Continent::containsRegion(int regionId) const {
    return std::find(regionIds.begin(), regionIds.end(), regionId) !=
           regionIds.end();
}

void Continent::addRegion(int regionId) {
    if (!containsRegion(regionId)) regionIds.push_back(regionId);
}

Region::Region(int id, std::string key, std::string name, int continentId)
    : id(id),
      key(std::move(key)),
      name(std::move(name)),
      continentId(continentId) {}

bool Region::containsProvince(int provinceId) const {
    return std::find(provinceIds.begin(), provinceIds.end(), provinceId) !=
           provinceIds.end();
}

void Region::addProvince(int provinceId) {
    if (!containsProvince(provinceId)) provinceIds.push_back(provinceId);
}
