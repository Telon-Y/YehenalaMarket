#pragma once

#include <array>
#include <cstddef>
#include <string>

#include "constants.h"
#include "scenario_config.h"

class World;

namespace WorldData {

inline constexpr std::size_t CONTINENT_COUNT = 5;
inline constexpr std::size_t REGION_COUNT = 17;
inline constexpr std::size_t COUNTRY_COUNT = 42;
inline constexpr std::size_t PROVINCE_COUNT = 69;

struct ContinentDefinition {
    const char* key;
    const char* name;
};

struct RegionDefinition {
    const char* key;
    const char* name;
    const char* continentKey;
};

struct CountryDefinition {
    const char* key;
    const char* name;
    const char* tag = nullptr;
    const char* overlordKey = nullptr;
};

struct ProvinceDefinition {
    const char* key;
    const char* name;
    const char* regionKey;
    const char* countryKey;
};

struct ProvinceScenarioDefinition {
    const char* provinceKey;
    double population;
    std::array<int, TYPE_COUNT> buildings;
    // -1 means the building is not resource-limited. Extractive buildings
    // use a non-negative province-specific development ceiling.
    std::array<int, TYPE_COUNT> resourceCaps;
};

const std::array<ContinentDefinition, CONTINENT_COUNT>& continents();
const std::array<RegionDefinition, REGION_COUNT>& regions();
const std::array<CountryDefinition, COUNTRY_COUNT>& countries();
const std::array<ProvinceDefinition, PROVINCE_COUNT>& provinces();
const std::array<ProvinceScenarioDefinition, PROVINCE_COUNT>&
provinceScenarios();
bool validateProvinceScenarios(std::string* error = nullptr);

// The map, political relations, and UI documentation all consume the same
// explicit scenario contract. This is intentionally not inferred from a
// country or province name at runtime.
int scenarioYear();
bool alternateHistory();
const char* scenarioKey();
bool validateOverlordGraph(const World& world, std::string* error = nullptr);

void populate(World& world);

}  // namespace WorldData
