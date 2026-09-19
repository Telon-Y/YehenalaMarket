#include "world.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace {
std::string DefaultCountryTag(const std::string& key) {
    static const std::unordered_map<std::string, std::string> tags = {
        {"china", "CHI"}, {"japan", "JAP"}, {"russia", "RUS"},
        {"indochina", "IDC"}, {"east_indies", "EIN"}, {"india", "IND"},
        {"turkey", "TUR"}, {"iran", "PER"}, {"prussia", "PRU"},
        {"west_german_states", "WGE"}, {"bavaria", "BAV"},
        {"austria", "AUS"}, {"france", "FRA"}, {"britain", "GBR"},
        {"low_countries_confederation", "LCO"}, {"switzerland", "SWI"},
        {"iberia", "IBE"}, {"italy", "ITA"}, {"norway", "NOR"},
        {"sweden", "SWE"}, {"finland", "FIN"}, {"egypt", "EGY"},
        {"tripolitania", "TRP"}, {"algeria", "ALG"}, {"morocco", "MOR"},
        {"west_africa", "WAF"}, {"central_africa", "CAF"},
        {"south_africa", "SAF"}, {"madagascar", "MAD"}, {"canada", "CAN"},
        {"united_states", "USA"}, {"mexico", "MEX"}, {"cuba", "CUB"},
        {"brazil", "BRA"}, {"argentina", "ARG"}, {"chile", "CHL"},
        {"bolivia", "BOL"}, {"peru", "PEU"}, {"new_granada", "GRA"},
        {"venezuela", "VEN"}, {"guiana", "GUI"}, {"australia", "AST"},
    };
    const auto it = tags.find(key);
    return it == tags.end() ? std::string() : it->second;
}
std::string StableFallbackTag(const std::string& identity, unsigned salt = 0) {
    std::uint32_t hash = 2166136261u ^ salt;
    for (unsigned char value : identity) {
        hash ^= value;
        hash *= 16777619u;
    }
    std::string tag(3, 'A');
    for (char& value : tag) {
        value = static_cast<char>('A' + hash % 26u);
        hash = hash / 26u + 0x9e3779b9u;
    }
    return tag;
}


std::string NormalizeCountryTag(std::string tag) {
    std::string result;
    for (unsigned char value : tag) {
        if (std::isalnum(value)) result.push_back(
            static_cast<char>(std::toupper(value)));
    }
    if (result.size() > 3) result.resize(3);
    while (result.size() < 3) result.push_back('X');
    return result;
}

template <typename T>
T& getByIndex(std::vector<std::unique_ptr<T>>& values, int index,
              const char* message) {
    if (index < 0 || index >= static_cast<int>(values.size()))
        throw std::out_of_range(message);
    return *values[static_cast<size_t>(index)];
}

template <typename T>
const T& getByIndex(const std::vector<std::unique_ptr<T>>& values, int index,
                    const char* message) {
    if (index < 0 || index >= static_cast<int>(values.size()))
        throw std::out_of_range(message);
    return *values[static_cast<size_t>(index)];
}
}

int World::createContinent(const std::string& name, const std::string& key) {
    if (!key.empty() &&
        continentIndexByKey.find(key) != continentIndexByKey.end()) {
        return -1;
    }

    const int id = nextContinentId++;
    const int index = static_cast<int>(continents.size());
    continents.push_back(std::make_unique<Continent>(id, key, name));
    continentIndexById[id] = index;
    if (!key.empty()) continentIndexByKey[key] = index;
    return id;
}

Continent& World::getContinent(int index) {
    return getByIndex(continents, index, "continent index out of range");
}

const Continent& World::getContinent(int index) const {
    return getByIndex(continents, index, "continent index out of range");
}

Continent& World::getContinentById(int continentId) {
    auto it = continentIndexById.find(continentId);
    if (it == continentIndexById.end())
        throw std::out_of_range("continent id not found");
    return *continents[static_cast<size_t>(it->second)];
}

const Continent& World::getContinentById(int continentId) const {
    auto it = continentIndexById.find(continentId);
    if (it == continentIndexById.end())
        throw std::out_of_range("continent id not found");
    return *continents[static_cast<size_t>(it->second)];
}

Continent* World::findContinentByKey(const std::string& key) {
    auto it = continentIndexByKey.find(key);
    return it == continentIndexByKey.end()
               ? nullptr
               : continents[static_cast<size_t>(it->second)].get();
}

const Continent* World::findContinentByKey(const std::string& key) const {
    auto it = continentIndexByKey.find(key);
    return it == continentIndexByKey.end()
               ? nullptr
               : continents[static_cast<size_t>(it->second)].get();
}

int World::createRegion(const std::string& name, int continentId,
                        const std::string& key) {
    auto continentIt = continentIndexById.find(continentId);
    if (continentIt == continentIndexById.end()) return -1;
    if (!key.empty() && regionIndexByKey.find(key) != regionIndexByKey.end())
        return -1;

    const int id = nextRegionId++;
    const int index = static_cast<int>(regions.size());
    regions.push_back(std::make_unique<Region>(id, key, name, continentId));
    regionIndexById[id] = index;
    if (!key.empty()) regionIndexByKey[key] = index;
    continents[static_cast<size_t>(continentIt->second)]->addRegion(id);
    return id;
}

Region& World::getRegion(int index) {
    return getByIndex(regions, index, "region index out of range");
}

const Region& World::getRegion(int index) const {
    return getByIndex(regions, index, "region index out of range");
}

Region& World::getRegionById(int regionId) {
    auto it = regionIndexById.find(regionId);
    if (it == regionIndexById.end())
        throw std::out_of_range("region id not found");
    return *regions[static_cast<size_t>(it->second)];
}

const Region& World::getRegionById(int regionId) const {
    auto it = regionIndexById.find(regionId);
    if (it == regionIndexById.end())
        throw std::out_of_range("region id not found");
    return *regions[static_cast<size_t>(it->second)];
}

Region* World::findRegionByKey(const std::string& key) {
    auto it = regionIndexByKey.find(key);
    return it == regionIndexByKey.end()
               ? nullptr
               : regions[static_cast<size_t>(it->second)].get();
}

const Region* World::findRegionByKey(const std::string& key) const {
    auto it = regionIndexByKey.find(key);
    return it == regionIndexByKey.end()
               ? nullptr
               : regions[static_cast<size_t>(it->second)].get();
}

int World::createCountry(const std::string& name, const std::string& key,
                         const std::string& countryCode) {
    if (!key.empty() && countryIndexByKey.find(key) != countryIndexByKey.end())
        return -1;
    std::string tag = countryCode.empty()
        ? DefaultCountryTag(key) : NormalizeCountryTag(countryCode);
    const std::string identity = key.empty() ? name : key;
    if (tag.empty()) tag = StableFallbackTag(identity);
    unsigned salt = 0;
    while (countryCode.empty() && countryIndexByTag.find(tag) != countryIndexByTag.end()) {
        tag = StableFallbackTag(identity, ++salt);
    }
    if (!tag.empty() && countryIndexByTag.find(tag) != countryIndexByTag.end())
        return -1;
    const int id = nextCountryId++;
    const int index = static_cast<int>(countries.size());
    countries.push_back(
        std::make_unique<Country>(id, name, nextNationalMarketId++, key, tag));
    countryIndexById[id] = index;
    if (!key.empty()) countryIndexByKey[key] = index;
    if (!tag.empty()) countryIndexByTag[tag] = index;
    return id;
}

bool World::removeCountry(int countryId) {
    auto countryIt = countryIndexById.find(countryId);
    if (countryIt == countryIndexById.end()) return false;
    const int index = countryIt->second;
    Country& country = *countries[static_cast<std::size_t>(index)];
    // A country cannot disappear while its provinces still point at it.
    if (!country.getProvinceIds().empty()) return false;
    // Orphaned active projects are invalidated before the country object
    // is removed, so their reservations cannot survive the deletion.
    for (NationalConstructionProject& project : country.constructionState.projects) {
        if (!project.active()) continue;
        country.releaseConstructionBudget(project.reservedBudget);
        project.reservedBudget = Money(0);
        project.status = ConstructionProjectStatus::Invalidated;
    }
    if (country.hasActiveConstruction() ||
        country.getReservedConstructionBudget() > Money(0)) return false;
    if (!country.getKey().empty()) countryIndexByKey.erase(country.getKey());
    if (!country.getCountryCode().empty())
        countryIndexByTag.erase(country.getCountryCode());
    const int lastIndex = static_cast<int>(countries.size()) - 1;
    countryIndexById.erase(countryIt);
    if (index != lastIndex) {
        countries[static_cast<std::size_t>(index)] =
            std::move(countries[static_cast<std::size_t>(lastIndex)]);
        Country& moved = *countries[static_cast<std::size_t>(index)];
        countryIndexById[moved.getId()] = index;
        if (!moved.getKey().empty()) countryIndexByKey[moved.getKey()] = index;
        if (!moved.getCountryCode().empty())
            countryIndexByTag[moved.getCountryCode()] = index;
    }
    countries.pop_back();
    return true;
}

Country& World::getCountry(int index) {
    return getByIndex(countries, index, "country index out of range");
}

const Country& World::getCountry(int index) const {
    return getByIndex(countries, index, "country index out of range");
}

Country& World::getCountryById(int countryId) {
    auto it = countryIndexById.find(countryId);
    if (it == countryIndexById.end())
        throw std::out_of_range("country id not found");
    return *countries[static_cast<size_t>(it->second)];
}

const Country& World::getCountryById(int countryId) const {
    auto it = countryIndexById.find(countryId);
    if (it == countryIndexById.end())
        throw std::out_of_range("country id not found");
    return *countries[static_cast<size_t>(it->second)];
}

Country* World::findCountryByKey(const std::string& key) {
    auto it = countryIndexByKey.find(key);
    return it == countryIndexByKey.end()
               ? nullptr
               : countries[static_cast<size_t>(it->second)].get();
}

const Country* World::findCountryByKey(const std::string& key) const {
    auto it = countryIndexByKey.find(key);
    return it == countryIndexByKey.end()
               ? nullptr
               : countries[static_cast<size_t>(it->second)].get();
}

Country* World::findCountryByTag(const std::string& tag) {
    auto it = countryIndexByTag.find(tag);
    return it == countryIndexByTag.end()
               ? nullptr
               : countries[static_cast<size_t>(it->second)].get();
}

const Country* World::findCountryByTag(const std::string& tag) const {
    auto it = countryIndexByTag.find(tag);
    return it == countryIndexByTag.end()
               ? nullptr
               : countries[static_cast<size_t>(it->second)].get();
}

bool World::setCountryOverlord(int subjectCountryId, int overlordCountryId) {
    auto subjectIt = countryIndexById.find(subjectCountryId);
    auto overlordIt = countryIndexById.find(overlordCountryId);
    if (subjectIt == countryIndexById.end() ||
        overlordIt == countryIndexById.end() ||
        subjectCountryId == overlordCountryId) {
        return false;
    }

    // Follow the proposed chain before storing it. A bounded walk also keeps
    // malformed data from turning colour resolution into an infinite loop.
    int current = overlordCountryId;
    for (std::size_t depth = 0; depth <= countries.size(); ++depth) {
        if (current == subjectCountryId) return false;
        const auto it = countryIndexById.find(current);
        if (it == countryIndexById.end()) return false;
        const int next = countries[static_cast<std::size_t>(it->second)]
                             ->getOverlordCountryId();
        if (next < 0) {
            countries[static_cast<std::size_t>(subjectIt->second)]
                ->setOverlordCountryId(overlordCountryId);
            return true;
        }
        current = next;
    }
    return false;
}

int World::createProvince(const std::string& name, int regionId, int countryId,
                          const std::string& localMarketName,
                          const std::string& key) {
    if (countryId >= 0 && countryIndexById.find(countryId) == countryIndexById.end())
        return -1;
    if (!key.empty() && provinceIndexByKey.find(key) != provinceIndexByKey.end())
        return -1;

    const int provinceId = nextProvinceId++;
    const int marketId = nextMarketId++;
    const int index = static_cast<int>(provinces.size());
    const std::string marketName = localMarketName.empty() ? name : localMarketName;

    provinces.push_back(std::make_unique<Province>(
        provinceId, name, regionId, countryId, marketId, marketName, key));
    if (countryId >= 0)
        provinces.back()->getLocalMarket().attachFiscalCountry(&getCountryById(countryId));
    provinces.back()->getLocalMarket().attachWorldContext(this, provinceId);
    if (!provinces.back()->getLocalMarket()
             .attachWarehouseNetwork(warehouseNetwork)) {
        provinces.pop_back();
        return -1;
    }
    provinceIndexById[provinceId] = index;
    provinceIndexByMarketId[marketId] = index;
    if (!key.empty()) provinceIndexByKey[key] = index;

    auto regionIt = regionIndexById.find(regionId);
    if (regionIt != regionIndexById.end()) {
        regions[static_cast<size_t>(regionIt->second)]->addProvince(provinceId);
    }

    if (countryId >= 0) {
        Country& country = getCountryById(countryId);
        country.addProvince(provinceId);
        country.getNationalMarket().addLocalMarket(marketId);
    }
    return provinceId;
}

bool World::assignProvinceToCountry(int provinceId, int countryId) {
    auto provinceIt = provinceIndexById.find(provinceId);
    if (provinceIt == provinceIndexById.end()) return false;
    if (countryId >= 0 && countryIndexById.find(countryId) == countryIndexById.end())
        return false;

    Province& province = *provinces[static_cast<size_t>(provinceIt->second)];
    const int oldCountryId = province.getCountryId();
    if (oldCountryId == countryId) return true;

    if (oldCountryId >= 0) {
        constructionService.invalidateProvince(oldCountryId, provinceId);
        Country& oldCountry = getCountryById(oldCountryId);
        oldCountry.removeProvince(provinceId);
        oldCountry.getNationalMarket().removeLocalMarket(province.getLocalMarketId());
    }
    if (oldCountryId < 0 && countryId >= 0) {
        LocalMarket& market = province.getLocalMarket();
        StandaloneConstructionService::invalidateAll(
            market, market.standaloneConstructionState);
    }

    province.setCountryId(countryId);
    if (countryId >= 0) {
        Country& newCountry = getCountryById(countryId);
        newCountry.addProvince(provinceId);
        province.getLocalMarket().attachFiscalCountry(&newCountry);
        newCountry.getNationalMarket().addLocalMarket(province.getLocalMarketId());
    } else {
        province.getLocalMarket().attachFiscalCountry(nullptr);
    }
    return true;
}

bool World::switchProvince(int index) {
    if (index < 0 || index >= static_cast<int>(provinces.size())) return false;
    currentProvinceIdx = index;
    return true;
}

bool World::switchProvinceById(int provinceId) {
    auto it = provinceIndexById.find(provinceId);
    if (it == provinceIndexById.end()) return false;
    currentProvinceIdx = it->second;
    return true;
}

Province& World::getCurrentProvince() {
    return getProvince(currentProvinceIdx);
}

const Province& World::getCurrentProvince() const {
    return getProvince(currentProvinceIdx);
}

Province& World::getProvince(int index) {
    return getByIndex(provinces, index, "province index out of range");
}

const Province& World::getProvince(int index) const {
    return getByIndex(provinces, index, "province index out of range");
}

Province& World::getProvinceById(int provinceId) {
    auto it = provinceIndexById.find(provinceId);
    if (it == provinceIndexById.end())
        throw std::out_of_range("province id not found");
    return *provinces[static_cast<size_t>(it->second)];
}

const Province& World::getProvinceById(int provinceId) const {
    auto it = provinceIndexById.find(provinceId);
    if (it == provinceIndexById.end())
        throw std::out_of_range("province id not found");
    return *provinces[static_cast<size_t>(it->second)];
}

Province& World::getProvinceByKey(const std::string& key) {
    Province* province = findProvinceByKey(key);
    if (province == nullptr) throw std::out_of_range("province key not found");
    return *province;
}

const Province& World::getProvinceByKey(const std::string& key) const {
    const Province* province = findProvinceByKey(key);
    if (province == nullptr) throw std::out_of_range("province key not found");
    return *province;
}

Province* World::findProvinceByKey(const std::string& key) {
    auto it = provinceIndexByKey.find(key);
    return it == provinceIndexByKey.end()
               ? nullptr
               : provinces[static_cast<size_t>(it->second)].get();
}

const Province* World::findProvinceByKey(const std::string& key) const {
    auto it = provinceIndexByKey.find(key);
    return it == provinceIndexByKey.end()
               ? nullptr
               : provinces[static_cast<size_t>(it->second)].get();
}

Province* World::findProvinceByName(const std::string& name) {
    auto it = std::find_if(provinces.begin(), provinces.end(),
                           [&name](const std::unique_ptr<Province>& province) {
                               return province->getName() == name;
                           });
    return it == provinces.end() ? nullptr : it->get();
}

const Province* World::findProvinceByName(const std::string& name) const {
    auto it = std::find_if(provinces.begin(), provinces.end(),
                           [&name](const std::unique_ptr<Province>& province) {
                               return province->getName() == name;
                           });
    return it == provinces.end() ? nullptr : it->get();
}

int World::createMarket(const std::string& name) {
    const int provinceId = createProvince(name, -1, -1, name);
    return provinceId < 0 ? -1 : getProvinceById(provinceId).getLocalMarketId();
}

bool World::switchMarket(int index) {
    return switchProvince(index);
}

LocalMarket& World::getCurrentMarket() {
    return getCurrentProvince().getLocalMarket();
}

const LocalMarket& World::getCurrentMarket() const {
    return getCurrentProvince().getLocalMarket();
}

LocalMarket& World::getMarket(int index) {
    return getProvince(index).getLocalMarket();
}

const LocalMarket& World::getMarket(int index) const {
    return getProvince(index).getLocalMarket();
}

LocalMarket& World::getMarketById(int marketId) {
    auto it = provinceIndexByMarketId.find(marketId);
    if (it == provinceIndexByMarketId.end())
        throw std::out_of_range("market id not found");
    return provinces[static_cast<size_t>(it->second)]->getLocalMarket();
}

const LocalMarket& World::getMarketById(int marketId) const {
    auto it = provinceIndexByMarketId.find(marketId);
    if (it == provinceIndexByMarketId.end())
        throw std::out_of_range("market id not found");
    return provinces[static_cast<size_t>(it->second)]->getLocalMarket();
}
