#include "world_data.h"

#include "world.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace WorldData {
namespace {

const std::array<ContinentDefinition, CONTINENT_COUNT> kContinents = {{
    {"asia", "亚洲"},
    {"europe", "欧洲"},
    {"africa", "非洲"},
    {"americas", "美洲"},
    {"oceania", "大洋洲"},
}};

const std::array<RegionDefinition, REGION_COUNT> kRegions = {{
    {"east_asia", "东亚", "asia"},
    {"west_asia", "西亚", "asia"},
    {"south_asia", "南亚", "asia"},
    {"north_asia", "北亚", "asia"},
    {"east_europe", "东欧", "europe"},
    {"central_europe", "中欧", "europe"},
    {"west_europe", "西欧", "europe"},
    {"south_europe", "南欧", "europe"},
    {"north_europe", "北欧", "europe"},
    {"north_africa", "北非", "africa"},
    {"west_africa", "西非", "africa"},
    {"central_africa", "中非", "africa"},
    {"south_africa", "南非", "africa"},
    {"north_america", "北美", "americas"},
    {"central_america", "中美", "americas"},
    {"south_america", "南美", "americas"},
    {"oceania", "大洋洲", "oceania"},
}};

const std::array<CountryDefinition, COUNTRY_COUNT> kCountries = {{
    {"china", "中国", "CHI"},
    {"japan", "日本", "JAP"},
    {"russia", "俄罗斯", "RUS"},
    {"indochina", "印支半岛", "IDC", "france"},
    {"east_indies", "东印度", "EIN", "low_countries_confederation"},
    {"india", "印度", "IND", "britain"},
    {"turkey", "土耳其", "TUR"},
    {"iran", "伊朗", "PER"},
    {"prussia", "普鲁士", "PRU"},
    {"west_german_states", "西德意志邦", "WGS"},
    {"bavaria", "巴伐利亚", "BAV"},
    {"austria", "奥匈帝国", "AUS"},
    {"france", "法国", "FRA"},
    {"britain", "英国", "GBR"},
    {"low_countries_confederation", "低地邦联", "LCO"},
    {"switzerland", "瑞士", "SWI"},
    {"iberia", "伊比利亚", "IBE"},
    {"italy", "意大利", "ITA"},
    {"norway", "挪威", "NOR", "sweden"},
    {"sweden", "瑞典", "SWE"},
    {"finland", "芬兰", "FIN", "russia"},
    {"egypt", "埃及", "EGY", "turkey"},
    {"tripolitania", "的黎波里", "TRP"},
    {"algeria", "阿尔及利亚", "ALG", "france"},
    {"morocco", "摩洛哥", "MOR"},
    {"west_africa", "西非", "WAF", "france"},
    {"central_africa", "中非", "CAF", "prussia"},
    {"south_africa", "南非", "SAF", "britain"},
    {"madagascar", "马达加斯加", "MAD"},
    {"canada", "加拿大", "CAN", "britain"},
    {"united_states", "美国", "USA"},
    {"mexico", "墨西哥", "MEX"},
    {"cuba", "古巴", "CUB"},
    {"brazil", "巴西", "BRA"},
    {"argentina", "阿根廷", "ARG"},
    {"chile", "智利", "CHL"},
    {"bolivia", "玻利维亚", "BOL"},
    {"peru", "秘鲁", "PEU"},
    {"new_granada", "新格拉纳达", "GRA"},
    {"venezuela", "委内瑞拉", "VEN"},
    {"guiana", "圭亚那", "GUI"},
    {"australia", "澳大利亚", "AST", "britain"},
}};

const std::array<ProvinceDefinition, PROVINCE_COUNT> kProvinces = {{
    {"japanese_islands", "日本列岛", "east_asia", "japan"},
    {"northeast_china", "东北", "east_asia", "china"},
    {"north_china", "华北", "east_asia", "china"},
    {"east_china", "华东", "east_asia", "china"},
    {"south_china", "华南", "east_asia", "china"},
    {"northwest_china", "西北", "east_asia", "china"},
    {"mongolia", "蒙古", "north_asia", "china"},

    {"central_asian_steppe", "中亚草原", "west_asia", "russia"},
    {"turkey", "土耳其", "west_asia", "turkey"},
    {"iran", "伊朗", "west_asia", "iran"},
    {"caucasus", "高加索", "west_asia", "russia"},

    {"indochina", "印支半岛", "south_asia", "indochina"},
    {"east_indies", "东印度", "south_asia", "east_indies"},
    {"north_india", "北印度", "south_asia", "india"},
    {"punjab", "旁遮普", "south_asia", "india"},
    {"south_india", "南印度", "south_asia", "india"},

    {"west_siberia", "西西伯利亚", "north_asia", "russia"},
    {"central_siberia", "中西伯利亚", "north_asia", "russia"},
    {"east_siberia", "东西伯利亚", "north_asia", "russia"},
    {"far_east", "远东", "north_asia", "russia"},

    {"north_russia", "北俄罗斯", "east_europe", "russia"},
    {"central_russia", "中俄罗斯", "east_europe", "russia"},
    {"baltic", "波罗的海", "east_europe", "russia"},
    {"ukraine", "乌克兰", "east_europe", "russia"},
    {"poland", "波兰", "east_europe", "russia"},
    {"prussia", "普鲁士", "central_europe", "prussia"},
    {"west_german_states", "西德意志邦", "central_europe", "west_german_states"},
    {"bavaria", "巴伐利亚", "central_europe", "bavaria"},
    {"austria", "奥地利", "central_europe", "austria"},
    {"czech", "捷克", "central_europe", "austria"},
    {"hungary", "匈牙利", "central_europe", "austria"},

    {"north_france", "北法兰西", "west_europe", "france"},
    {"south_france", "南法兰西", "west_europe", "france"},
    {"england", "英格兰", "west_europe", "britain"},
    {"scotland", "苏格兰", "west_europe", "britain"},
    {"wales", "威尔士", "west_europe", "britain"},
    {"ireland", "爱尔兰", "west_europe", "britain"},
    {"low_countries", "低地", "west_europe", "low_countries_confederation"},
    {"switzerland", "瑞士", "west_europe", "switzerland"},

    {"iberian_peninsula", "伊比利亚半岛", "south_europe", "iberia"},
    {"italian_peninsula", "亚平宁半岛", "south_europe", "italy"},

    {"norway", "挪威", "north_europe", "norway"},
    {"sweden", "瑞典", "north_europe", "sweden"},
    {"finland", "芬兰", "north_europe", "finland"},

    {"egypt", "埃及", "north_africa", "egypt"},
    {"tripolitania", "的黎波里", "north_africa", "tripolitania"},
    {"algeria", "阿尔及利亚", "north_africa", "algeria"},
    {"morocco", "摩洛哥", "north_africa", "morocco"},

    {"west_africa", "西非", "west_africa", "west_africa"},
    {"central_africa", "中非", "central_africa", "central_africa"},

    {"south_africa", "南非", "south_africa", "south_africa"},
    {"madagascar", "马达加斯加", "south_africa", "madagascar"},

    {"canada", "加拿大", "north_america", "canada"},
    {"thirteen_states", "十三州", "north_america", "united_states"},
    {"southern_states", "南方州", "north_america", "united_states"},
    {"great_lakes", "大湖区", "north_america", "united_states"},
    {"great_plains", "大平原", "north_america", "united_states"},
    {"west_coast", "西海岸", "north_america", "united_states"},

    {"mexico", "墨西哥", "central_america", "mexico"},
    {"cuba", "古巴", "central_america", "cuba"},

    {"brazil", "巴西", "south_america", "brazil"},
    {"argentina", "阿根廷", "south_america", "argentina"},
    {"chile", "智利", "south_america", "chile"},
    {"bolivia", "玻利维亚", "south_america", "bolivia"},
    {"peru", "秘鲁", "south_america", "peru"},
    {"new_granada", "格拉纳达", "south_america", "new_granada"},
    {"venezuela", "委内瑞拉", "south_america", "venezuela"},
    {"guiana", "圭亚那", "south_america", "guiana"},
    {"australia", "澳大利亚", "oceania", "australia"},
}};

constexpr std::array<int, TYPE_COUNT> Buildings(
    int grain, int cotton, int coal, int iron, int gold,
    int food, int clothes, int luxuryClothes, int steel, int tools,
    int housing, int construction, int railway,
    int centralBank, int finance, int industrialBank, int savingsBank) {
    std::array<int, TYPE_COUNT> result{};
    result[FARM_GRAIN] = grain;
    result[FOOD_PROC] = food;
    result[COTTON] = cotton;
    result[CLOTHES] = clothes;
    result[LUXURY_CLOTHES] = luxuryClothes;
    result[COAL_MINE] = coal;
    result[IRON_MINE] = iron;
    result[STEEL_MILL] = steel;
    result[TOOL_FACT] = tools;
    result[HOUSING] = housing;
    result[CONST_DEPT] = construction;
    result[GOLD_MINE] = gold;
    result[BANK] = centralBank;
    result[FINANCE] = finance;
    result[INDUSTRIAL_BANK] = industrialBank;
    result[SAVINGS_BANK] = savingsBank;
    result[RAILWAY] = railway;
    return result;
}

constexpr int ResourceCap(int initial, int globalLimit) {
    return initial <= 0
        ? 0
        : (initial * 3 < globalLimit ? initial * 3 : globalLimit);
}

constexpr std::array<int, TYPE_COUNT> ResourceCaps(
    const std::array<int, TYPE_COUNT>& buildings) {
    std::array<int, TYPE_COUNT> result{};
    for (int type = 0; type < TYPE_COUNT; ++type) result[type] = -1;
    result[FARM_GRAIN] = ResourceCap(buildings[FARM_GRAIN], 10000);
    result[COTTON] = ResourceCap(buildings[COTTON], 10000);
    result[COAL_MINE] = ResourceCap(buildings[COAL_MINE], 500);
    result[IRON_MINE] = ResourceCap(buildings[IRON_MINE], 500);
    result[GOLD_MINE] = ResourceCap(buildings[GOLD_MINE], 50);
    return result;
}

constexpr ProvinceScenarioDefinition Scenario(
    const char* key, double population,
    const std::array<int, TYPE_COUNT>& buildings) {
    return {key, population, buildings, ResourceCaps(buildings)};
}

// Population is the estimated resident population around 1880. Building
// levels are normalized simulation capacity, not literal establishment counts.
const std::array<ProvinceScenarioDefinition, PROVINCE_COUNT>
kProvinceScenarios = {{
    Scenario("japanese_islands", 37'000'000.0,
             Buildings(80,18,22,12,2,30,45,8,18,22,55,18,9,0,3,3,2)),
    Scenario("northeast_china", 12'000'000.0,
             Buildings(32,3,8,5,2,5,3,0,1,2,5,1,0,0,0,1,0)),
    Scenario("north_china", 36'000'000.0,
             Buildings(75,18,8,6,1,12,6,1,1,2,14,3,0,0,1,1,1)),
    Scenario("east_china", 130'000'000.0,
             Buildings(230,35,6,3,1,42,20,4,2,5,42,5,0,0,4,4,3)),
    Scenario("south_china", 166'000'000.0,
             Buildings(300,28,9,7,2,48,18,2,1,4,38,4,0,0,2,3,2)),
    Scenario("northwest_china", 22'000'000.0,
             Buildings(42,5,4,3,1,5,2,0,0,1,5,1,0,0,0,0,0)),
    Scenario("mongolia", 700'000.0,
             Buildings(2,0,0,0,1,0,0,0,0,0,1,0,0,0,0,0,0)),

    Scenario("central_asian_steppe", 9'100'000.0,
             Buildings(12,3,4,2,3,2,1,0,0,1,2,1,1,0,0,1,0)),
    Scenario("turkey", 16'500'000.0,
             Buildings(38,9,7,5,1,10,8,2,2,3,12,4,4,1,2,2,1)),
    Scenario("iran", 8'800'000.0,
             Buildings(18,8,1,2,1,3,3,1,0,1,3,1,0,0,1,1,0)),
    Scenario("caucasus", 6'200'000.0,
             Buildings(18,4,4,3,2,4,3,0,1,1,4,1,2,0,1,1,0)),

    Scenario("indochina", 14'200'000.0,
             Buildings(45,6,3,2,1,7,3,0,0,1,5,1,0,1,1,1,0)),
    Scenario("east_indies", 30'100'000.0,
             Buildings(75,2,4,2,2,10,4,1,0,1,10,2,3,1,2,2,1)),
    Scenario("north_india", 103'000'000.0,
             Buildings(210,36,18,12,2,28,18,3,4,7,22,4,4,0,2,3,1)),
    Scenario("punjab", 43'000'000.0,
             Buildings(105,26,6,4,1,12,10,1,1,3,10,2,3,0,1,2,1)),
    Scenario("south_india", 78'000'000.0,
             Buildings(180,55,12,10,5,24,24,4,3,6,20,4,5,0,3,4,2)),

    Scenario("west_siberia", 24'000'000.0,
             Buildings(52,4,16,12,4,8,4,0,4,6,7,2,3,0,1,2,1)),
    Scenario("central_siberia", 1'000'000.0,
             Buildings(3,0,4,3,3,1,0,0,0,1,1,0,0,0,0,0,0)),
    Scenario("east_siberia", 1'000'000.0,
             Buildings(2,0,3,4,5,1,0,0,0,1,1,0,0,0,0,0,0)),
    Scenario("far_east", 500'000.0,
             Buildings(1,0,1,2,3,0,0,0,0,0,1,0,0,0,0,0,0)),

    Scenario("north_russia", 7'000'000.0,
             Buildings(12,1,5,4,1,7,5,1,3,5,8,2,8,1,3,3,2)),
    Scenario("central_russia", 19'100'000.0,
             Buildings(42,7,12,8,2,22,18,3,14,16,30,6,12,0,4,5,4)),
    Scenario("baltic", 4'200'000.0,
             Buildings(12,2,2,1,0,6,5,1,2,3,8,2,7,0,1,2,1)),
    Scenario("ukraine", 18'500'000.0,
             Buildings(65,12,18,12,1,12,8,1,10,8,12,3,8,0,2,2,1)),
    Scenario("poland", 6'500'000.0,
             Buildings(16,4,12,8,1,9,10,2,8,7,10,2,7,0,2,2,1)),

    Scenario("prussia", 27'300'000.0,
             Buildings(58,0,55,28,1,28,35,10,40,38,70,16,65,1,7,7,6)),
    Scenario("west_german_states", 11'500'000.0,
             Buildings(28,0,20,12,1,14,20,5,18,19,35,8,35,0,4,4,3)),
    Scenario("bavaria", 5'300'000.0,
             Buildings(14,0,6,5,0,7,10,2,5,7,15,4,14,1,2,2,2)),
    Scenario("austria", 17'500'000.0,
             Buildings(38,1,20,12,3,18,20,5,14,15,38,9,32,1,5,5,4)),
    Scenario("czech", 10'900'000.0,
             Buildings(20,0,28,18,2,14,22,4,20,18,30,7,30,0,4,4,3)),
    Scenario("hungary", 8'800'000.0,
             Buildings(35,5,8,6,2,8,8,1,3,5,14,3,10,0,1,2,1)),

    Scenario("north_france", 24'000'000.0,
             Buildings(50,0,38,20,1,30,42,16,34,32,68,15,60,1,9,8,7)),
    Scenario("south_france", 14'700'000.0,
             Buildings(38,1,14,10,2,20,22,8,12,15,38,8,28,0,4,4,3)),
    Scenario("england", 24'600'000.0,
             Buildings(35,0,85,45,2,45,75,20,70,68,90,22,80,1,12,12,10)),
    Scenario("scotland", 3'740'000.0,
             Buildings(7,0,22,10,1,7,14,3,14,15,16,4,20,0,3,3,2)),
    Scenario("wales", 1'360'000.0,
             Buildings(4,0,20,10,1,4,5,1,12,8,8,2,12,0,1,1,1)),
    Scenario("ireland", 5'170'000.0,
             Buildings(18,0,2,1,0,5,8,1,1,2,6,1,4,0,1,1,1)),
    Scenario("low_countries", 9'800'000.0,
             Buildings(18,0,25,12,1,18,28,8,22,22,34,8,40,1,7,7,6)),
    Scenario("switzerland", 2'830'000.0,
             Buildings(5,0,1,1,1,5,12,4,2,6,12,3,10,0,5,4,4)),

    Scenario("iberian_peninsula", 21'500'000.0,
             Buildings(60,4,18,14,8,18,20,5,8,10,25,6,20,1,4,4,3)),
    Scenario("italian_peninsula", 29'400'000.0,
             Buildings(75,1,8,5,2,25,38,10,8,14,40,8,22,1,6,6,5)),

    Scenario("norway", 1'920'000.0,
             Buildings(3,0,2,2,1,3,4,1,1,3,7,2,4,1,2,2,1)),
    Scenario("sweden", 4'560'000.0,
             Buildings(10,0,4,20,2,5,8,2,10,12,14,3,8,1,3,3,2)),
    Scenario("finland", 2'040'000.0,
             Buildings(5,0,1,3,1,3,4,0,1,3,6,1,3,1,1,1,1)),

    Scenario("egypt", 6'800'000.0,
             Buildings(20,35,0,1,1,8,10,2,0,1,8,2,4,0,3,2,1)),
    Scenario("tripolitania", 400'000.0,
             Buildings(3,0,0,0,0,1,0,0,0,0,1,0,0,0,0,0,0)),
    Scenario("algeria", 3'500'000.0,
             Buildings(10,2,2,5,1,3,2,0,0,1,4,1,3,1,1,1,0)),
    Scenario("morocco", 4'000'000.0,
             Buildings(14,2,1,2,1,3,3,1,0,1,2,1,0,0,1,1,0)),

    Scenario("west_africa", 30'000'000.0,
             Buildings(60,10,2,3,8,8,5,1,0,1,4,1,0,1,1,1,0)),
    Scenario("central_africa", 20'000'000.0,
             Buildings(30,3,1,4,6,3,1,0,0,0,2,0,0,0,0,0,0)),

    Scenario("south_africa", 4'150'000.0,
             Buildings(12,1,20,8,35,5,4,1,3,3,8,2,6,0,2,2,1)),
    Scenario("madagascar", 2'800'000.0,
             Buildings(10,2,1,2,2,2,2,0,0,0,2,0,0,0,0,0,0)),

    Scenario("canada", 4'330'000.0,
             Buildings(14,0,12,8,4,6,8,2,5,8,16,4,20,0,4,5,4)),
    Scenario("thirteen_states", 21'200'000.0,
             Buildings(48,5,50,25,2,35,55,18,40,45,70,16,65,0,12,12,10)),
    Scenario("southern_states", 9'900'000.0,
             Buildings(25,55,18,10,2,12,14,2,8,8,18,4,18,0,3,4,2)),
    Scenario("great_lakes", 15'800'000.0,
             Buildings(55,3,45,32,2,28,35,7,35,35,50,12,55,0,6,7,5)),
    Scenario("great_plains", 2'240'000.0,
             Buildings(12,1,5,6,8,2,1,0,1,2,5,1,8,0,1,1,1)),
    Scenario("west_coast", 1'210'000.0,
             Buildings(5,1,5,4,15,3,3,1,1,3,5,1,5,0,2,2,1)),

    Scenario("mexico", 10'300'000.0,
             Buildings(28,8,8,15,25,8,8,2,4,5,10,2,6,0,2,2,1)),
    Scenario("cuba", 1'530'000.0,
             Buildings(5,1,0,1,1,4,3,1,0,1,4,1,2,1,1,1,0)),

    Scenario("brazil", 11'800'000.0,
             Buildings(32,8,5,12,25,10,10,2,2,4,10,2,8,1,3,3,2)),
    Scenario("argentina", 2'460'000.0,
             Buildings(10,0,1,1,2,4,4,1,0,2,7,2,6,1,2,2,1)),
    Scenario("chile", 2'350'000.0,
             Buildings(6,1,6,18,10,3,4,1,2,3,6,1,5,0,2,2,1)),
    Scenario("bolivia", 1'190'000.0,
             Buildings(4,1,2,12,8,1,1,0,0,1,2,0,0,0,1,1,0)),
    Scenario("peru", 2'950'000.0,
             Buildings(8,4,3,10,12,3,3,1,1,2,4,1,3,0,2,2,1)),
    Scenario("new_granada", 2'850'000.0,
             Buildings(10,3,3,6,10,4,4,1,1,2,4,1,1,0,2,2,1)),
    Scenario("venezuela", 2'020'000.0,
             Buildings(7,2,1,2,5,2,2,0,0,1,3,1,0,0,1,1,0)),
    Scenario("guiana", 360'000.0,
             Buildings(1,0,0,1,4,0,0,0,0,0,1,0,0,0,1,1,0)),
    Scenario("australia", 2'200'000.0,
             Buildings(8,0,15,10,20,3,5,1,3,4,8,2,12,0,3,3,2)),
}};

bool reportValidationError(std::string* error, const std::string& message) {
    if (error != nullptr) *error = message;
    return false;
}

void requireCreated(int id, const char* kind, const char* key) {
    if (id >= 0) return;
    throw std::logic_error(std::string("failed to create default ") + kind +
                           ": " + key);
}

}  // namespace

const std::array<ContinentDefinition, CONTINENT_COUNT>& continents() {
    return kContinents;
}

const std::array<RegionDefinition, REGION_COUNT>& regions() {
    return kRegions;
}

const std::array<CountryDefinition, COUNTRY_COUNT>& countries() {
    return kCountries;
}

const std::array<ProvinceDefinition, PROVINCE_COUNT>& provinces() {
    return kProvinces;
}

const std::array<ProvinceScenarioDefinition, PROVINCE_COUNT>&
provinceScenarios() {
    return kProvinceScenarios;
}

bool validateProvinceScenarios(std::string* error) {
    static constexpr std::array<int, 5> resourceTypes = {
        FARM_GRAIN, COTTON, COAL_MINE, IRON_MINE, GOLD_MINE};
    const auto isResourceType = [](int type) {
        return type == FARM_GRAIN || type == COTTON ||
               type == COAL_MINE || type == IRON_MINE ||
               type == GOLD_MINE;
    };

    std::unordered_set<std::string> keys;
    for (std::size_t index = 0; index < kProvinceScenarios.size(); ++index) {
        const ProvinceScenarioDefinition& scenario =
            kProvinceScenarios[index];
        if (scenario.provinceKey == nullptr ||
            scenario.provinceKey[0] == '\0') {
            return reportValidationError(
                error, "province scenario has an empty key");
        }
        if (scenario.provinceKey != std::string(kProvinces[index].key)) {
            return reportValidationError(
                error, "province scenario order/key mismatch: " +
                           std::string(scenario.provinceKey));
        }
        if (!keys.insert(scenario.provinceKey).second) {
            return reportValidationError(
                error, "duplicate province scenario: " +
                           std::string(scenario.provinceKey));
        }
        if (!std::isfinite(scenario.population) ||
            scenario.population < 1000.0) {
            return reportValidationError(
                error, "invalid province population: " +
                           std::string(scenario.provinceKey));
        }
        for (int type = 0; type < TYPE_COUNT; ++type) {
            if (scenario.buildings[type] < 0) {
                return reportValidationError(
                    error, "negative province building level: " +
                               std::string(scenario.provinceKey));
            }
            const int cap = scenario.resourceCaps[type];
            if (isResourceType(type)) {
                if (cap < scenario.buildings[type]) {
                    return reportValidationError(
                        error, "resource cap below initial level: " +
                                   std::string(scenario.provinceKey));
                }
            } else if (cap != -1) {
                return reportValidationError(
                    error, "non-resource building has a resource cap: " +
                               std::string(scenario.provinceKey));
            }
        }
        for (const int type : resourceTypes) {
            if (scenario.resourceCaps[type] < 0) {
                return reportValidationError(
                    error, "missing extractive resource cap: " +
                               std::string(scenario.provinceKey));
            }
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

int scenarioYear() {
    return scenario::kScenarioYear;
}

bool alternateHistory() {
    return scenario::kAlternateHistory;
}

const char* scenarioKey() {
    return scenario::kScenarioKey;
}

bool validateOverlordGraph(const World& world, std::string* error) {
    for (const auto& definition : kCountries) {
        const Country* subject = world.findCountryByKey(definition.key);
        if (subject == nullptr)
            return reportValidationError(error,
                std::string("missing subject country: ") + definition.key);
        if (definition.overlordKey == nullptr) {
            if (!subject->isSovereign())
                return reportValidationError(error,
                    std::string("unexpected overlord for sovereign: ") +
                    definition.key);
            continue;
        }
        const Country* expectedOverlord =
            world.findCountryByKey(definition.overlordKey);
        if (expectedOverlord == nullptr)
            return reportValidationError(error,
                std::string("missing overlord target: ") +
                definition.overlordKey);
        if (subject->getOverlordCountryId() != expectedOverlord->getId())
            return reportValidationError(error,
                std::string("overlord mismatch: ") + definition.key);

        std::unordered_set<int> visited;
        const Country* current = subject;
        while (current != nullptr && !current->isSovereign()) {
            if (!visited.insert(current->getId()).second)
                return reportValidationError(error,
                    std::string("overlord cycle at: ") + definition.key);
            try {
                current = &world.getCountryById(current->getOverlordCountryId());
            } catch (...) {
                return reportValidationError(error,
                    std::string("overlord id missing for: ") + definition.key);
            }
        }
    }
    if (error != nullptr) error->clear();
    return true;
}

void populate(World& world) {
    std::string scenarioError;
    if (!validateProvinceScenarios(&scenarioError))
        throw std::logic_error("invalid province scenario data: " +
                               scenarioError);

    for (const auto& definition : kContinents) {
        requireCreated(world.createContinent(definition.name, definition.key),
                       "continent", definition.key);
    }

    for (const auto& definition : kRegions) {
        const Continent* continent = world.findContinentByKey(definition.continentKey);
        if (continent == nullptr) {
            throw std::logic_error(std::string("unknown default continent: ") +
                                   definition.continentKey);
        }
        requireCreated(world.createRegion(definition.name, continent->getId(),
                                          definition.key),
                       "region", definition.key);
    }

    for (const auto& definition : kCountries) {
        requireCreated(world.createCountry(definition.name, definition.key,
                                     definition.tag == nullptr ? "" : definition.tag),
                       "country", definition.key);
    }
    for (const auto& definition : kCountries) {
        if (definition.overlordKey == nullptr) continue;
        const Country* subject = world.findCountryByKey(definition.key);
        const Country* overlord = world.findCountryByKey(definition.overlordKey);
        if (subject == nullptr || overlord == nullptr ||
            !world.setCountryOverlord(subject->getId(), overlord->getId())) {
            throw std::logic_error(std::string("invalid default overlord link: ") +
                                   definition.key);
        }
    }
    std::string relationError;
    if (!validateOverlordGraph(world, &relationError))
        throw std::logic_error("invalid default overlord graph: " + relationError);


    for (std::size_t index = 0; index < kProvinces.size(); ++index) {
        const auto& definition = kProvinces[index];
        const Region* region = world.findRegionByKey(definition.regionKey);
        const Country* country = world.findCountryByKey(definition.countryKey);
        if (region == nullptr || country == nullptr) {
            throw std::logic_error(std::string("invalid default province links: ") +
                                   definition.key);
        }
        const std::string marketName =
            std::string(definition.name) + "本地市场";
        const int provinceId = world.createProvince(
            definition.name, region->getId(), country->getId(), marketName,
            definition.key);
        requireCreated(provinceId, "province", definition.key);
        const ProvinceScenarioDefinition& scenario =
            kProvinceScenarios[index];
        if (!world.getProvinceById(provinceId).getLocalMarket()
                 .configureProvinceScenarioForSetup(
                     scenario.population, scenario.buildings,
                     scenario.resourceCaps)) {
            throw std::logic_error(
                std::string("failed to configure province scenario: ") +
                definition.key);
        }
    }
}

}  // namespace WorldData
