#pragma once

#include <string>
#include <vector>

class World;

class Continent {
public:
    Continent(int id, std::string key, std::string name);

    int getId() const { return id; }
    const std::string& getKey() const { return key; }
    const std::string& getName() const { return name; }
    const std::vector<int>& getRegionIds() const { return regionIds; }
    bool containsRegion(int regionId) const;

private:
    friend class World;

    void addRegion(int regionId);

    int id;
    std::string key;
    std::string name;
    std::vector<int> regionIds;
};

class Region {
public:
    Region(int id, std::string key, std::string name, int continentId);

    int getId() const { return id; }
    const std::string& getKey() const { return key; }
    const std::string& getName() const { return name; }
    int getContinentId() const { return continentId; }
    const std::vector<int>& getProvinceIds() const { return provinceIds; }
    bool containsProvince(int provinceId) const;

private:
    friend class World;

    void addProvince(int provinceId);

    int id;
    std::string key;
    std::string name;
    int continentId;
    std::vector<int> provinceIds;
};
