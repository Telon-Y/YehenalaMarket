#include "world_data.h"

#include "world.h"

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
    {"mongolia", "Mongolia", "north_asia", "china"},

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


    for (const auto& definition : kProvinces) {
        const Region* region = world.findRegionByKey(definition.regionKey);
        const Country* country = world.findCountryByKey(definition.countryKey);
        if (region == nullptr || country == nullptr) {
            throw std::logic_error(std::string("invalid default province links: ") +
                                   definition.key);
        }
        const std::string marketName =
            std::string(definition.name) + "本地市场";
        requireCreated(world.createProvince(definition.name, region->getId(),
                                            country->getId(), marketName,
                                            definition.key),
                       "province", definition.key);
    }
}

}  // namespace WorldData
