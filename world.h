// ==================== world.h ====================
#pragma once

#include "construction_service.h"
#include "country.h"
#include "province.h"
#include "sim_types.h"
#include "world_geography.h"
#include "warehouse.h"

#include <memory>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

class World {
public:
    static World& Instance();
    static World& DebugFiveMarkets();
    static std::unique_ptr<World> CreateDebugWorldForTesting();
    bool isDebugFiveMarketScenario() const { return debugFiveMarketScenario; }

    int createContinent(const std::string& name,
                        const std::string& key = "");
    Continent& getContinent(int index);
    const Continent& getContinent(int index) const;
    Continent& getContinentById(int continentId);
    const Continent& getContinentById(int continentId) const;
    Continent* findContinentByKey(const std::string& key);
    const Continent* findContinentByKey(const std::string& key) const;
    int getContinentCount() const {
        return static_cast<int>(continents.size());
    }

    int createRegion(const std::string& name, int continentId,
                     const std::string& key = "");
    Region& getRegion(int index);
    const Region& getRegion(int index) const;
    Region& getRegionById(int regionId);
    const Region& getRegionById(int regionId) const;
    Region* findRegionByKey(const std::string& key);
    const Region* findRegionByKey(const std::string& key) const;
    int getRegionCount() const { return static_cast<int>(regions.size()); }

    // ===== 国家与全国市场 =====
    int createCountry(const std::string& name,
                      const std::string& key = "",
                      const std::string& countryCode = "");
    Country& getCountry(int index);
    const Country& getCountry(int index) const;
    Country& getCountryById(int countryId);
    const Country& getCountryById(int countryId) const;
    Country* findCountryByKey(const std::string& key);
    // Removes an empty country after all active projects have been handled.
    bool removeCountry(int countryId);
    bool deleteCountry(int countryId) { return removeCountry(countryId); }
    const Country* findCountryByKey(const std::string& key) const;
    Country* findCountryByTag(const std::string& tag);
    const Country* findCountryByTag(const std::string& tag) const;
    bool setCountryOverlord(int subjectCountryId, int overlordCountryId);
    Country* findCountryByCode(const std::string& code) {
        return findCountryByTag(code);
    }
    const Country* findCountryByCode(const std::string& code) const {
        return findCountryByTag(code);
    }
    int getCountryCount() const { return static_cast<int>(countries.size()); }

    // ===== 省份与本地市场 =====
    int createProvince(const std::string& name, int regionId = -1,
                       int countryId = -1,
                       const std::string& localMarketName = "",
                       const std::string& key = "");
    bool assignProvinceToCountry(int provinceId, int countryId);
    // National construction is the only write path for player-issued work
    // created from a world-owned province.
    int queueNationalConstruction(
        int countryId, int provinceId, int typeIndex, int count,
        std::uint64_t* projectId = nullptr,
        Money alreadyReservedBudget = Money(0));
    ConstructionCommandResult evaluateNationalConstruction(
        int countryId, int provinceId, int typeIndex, int count) const;
    ConstructionCommandResult queueNationalConstructionCommand(
        int countryId, int provinceId, int typeIndex, int count,
        Money alreadyReservedBudget = Money(0));
    ConstructionQuote quoteConstruction(
        const ConstructionRequest& request) const {
        return constructionService.quote(request);
    }
    ConstructionCommandResult submitConstruction(
        const ConstructionRequest& request) {
        return constructionService.submit(request);
    }
    bool pauseConstructionProject(int countryId, ConstructionProjectId id) {
        return constructionService.pause(countryId, id);
    }
    bool resumeConstructionProject(int countryId, ConstructionProjectId id) {
        return constructionService.resume(countryId, id);
    }
    bool setConstructionProjectPriority(
        int countryId, ConstructionProjectId id, int priority) {
        return constructionService.setPriority(countryId, id, priority);
    }
    bool moveConstructionProject(int countryId, ConstructionProjectId id,
                                 bool up, bool toEdge = false) {
        return constructionService.move(countryId, id, up, toEdge);
    }
    bool addConstructionProjectBudget(
        int countryId, ConstructionProjectId id, Money amount) {
        return constructionService.addBudget(countryId, id, amount);
    }
    std::uint64_t createNationalConstructionProject(
        int countryId, int provinceId, int typeIndex, int count);
    std::uint64_t createConstructionProject(
        int countryId, int provinceId, int typeIndex, int count) {
        return createNationalConstructionProject(countryId, provinceId,
                                                 typeIndex, count);
    }
    bool cancelNationalConstructionProject(int countryId,
                                           std::uint64_t projectId);
    bool cancelConstructionProject(int countryId, std::uint64_t projectId) {
        return cancelNationalConstructionProject(countryId, projectId);
    }
    bool switchProvince(int index);
    bool switchProvinceById(int provinceId);
    Province& getCurrentProvince();
    const Province& getCurrentProvince() const;
    Province& getProvince(int index);
    const Province& getProvince(int index) const;
    Province& getProvinceById(int provinceId);
    const Province& getProvinceById(int provinceId) const;
    CountrySnapshot getCountrySnapshot(int countryId) const;
    ProvinceSnapshot getProvinceSnapshot(int provinceId) const;
    std::vector<CountrySnapshot> getCountrySnapshots() const;
    std::vector<ProvinceSnapshot> getProvinceSnapshots(
        const std::vector<int>& provinceIds) const;
    Province& getProvinceByKey(const std::string& key);
    const Province& getProvinceByKey(const std::string& key) const;
    Province* findProvinceByKey(const std::string& key);
    const Province* findProvinceByKey(const std::string& key) const;
    Province* findProvinceByName(const std::string& name);
    const Province* findProvinceByName(const std::string& name) const;
    int getCurrentProvinceIndex() const { return currentProvinceIdx; }
    int getProvinceCount() const { return static_cast<int>(provinces.size()); }

    // ===== 兼容接口：市场索引与省份索引一一对应 =====
    int createMarket(const std::string& name);
    bool switchMarket(int index);
    LocalMarket& getCurrentMarket();
    const LocalMarket& getCurrentMarket() const;
    LocalMarket& getMarket(int index);
    const LocalMarket& getMarket(int index) const;
    LocalMarket& getMarketById(int marketId);
    const LocalMarket& getMarketById(int marketId) const;
    MarketSnapshot getMarketSnapshot(int index) const;
    const TransportationSnapshot& getTransportationSnapshot(
        int warehouseId = -1) const;
    // Exposed for deterministic GUI/cache regression tests.
    std::uint64_t transportationSnapshotBuildCount() const {
        return transportationSnapshotBuilds;
    }
    std::vector<WarehouseOrderSnapshot> getWarehouseOrderSnapshots(
        int warehouseId = -1) const;
    std::vector<ShipmentSnapshot> getShipmentSnapshots(
        int warehouseId = -1) const;
    WarehouseNetwork& getWarehouseNetwork() { return warehouseNetwork; }
    const WarehouseNetwork& getWarehouseNetwork() const {
        return warehouseNetwork;
    }
    // ===== 资金审计 =====
    // Every pool the system holds: each market's pools and in-flight
    // accumulators, every country treasury, and the warehouse escrow.
    // Seigniorage is the only creation path, so the conserved identity is
    //   total(t) - total(0) == created(0..t) - residual(0..t)
    // and a residual other than zero is money that moved without a receiver.
    Money totalMoneyInSystem() const;
    Money getMoneyOpeningTotal() const { return moneyOpeningTotal; }
    Money getMoneyCreatedTotal() const { return moneyCreatedTotal; }
    Money getMoneyResidual() const { return moneyResidual; }
    Money getMoneyLastCycleResidual() const { return moneyLastCycleResidual; }
    Money getMoneyWorstCycleResidual() const { return moneyWorstCycleResidual; }
    int getMoneyWorstCycle() const { return moneyWorstCycle; }
    Money getMoneyEscrow() const { return warehouseNetwork.escrowBalance(); }
    Money getLaborerCashTotal() const;
    int getCurrentIndex() const { return getCurrentProvinceIndex(); }
    int getMarketCount() const { return getProvinceCount(); }

    // ===== 贸易路径 =====
    int addTradePath(int sourceMarketId, int targetMarketId, int goodIndex,
                     Money maxVolumePerWeek, Money transportCostPerUnit);
    int addRailTradePath(int sourceMarketId, int targetMarketId,
                         int goodIndex, Money maxVolumePerWeek,
                         double distanceKm,
                         Money capacityCoefficient =
                             Money(RAIL_DISTANCE_CAPACITY_COEFFICIENT));
    const std::vector<TradePath>& getTradePaths() const { return tradePaths; }
    void executeTrade();

    // ===== 推进 =====
    void stepAll(bool runAI = true);

private:
    World();
    explicit World(bool debugFiveMarkets);
    friend class ConstructionService;
    friend class ConstructionSystem;
    void populateDebugFiveMarkets();
    void populateStandardCountryMarkets();
    void runNationalExpansionAI();
    std::vector<std::unique_ptr<Continent>> continents;
    std::vector<std::unique_ptr<Region>> regions;
    std::vector<std::unique_ptr<Province>> provinces;
    void prepareNationalConstructionPlans();
    void processNationalConstruction();
    std::uint64_t nextNationalConstructionProjectId = 1;
    // Money audit state, accumulated by stepAll.
    Money moneyOpeningTotal = Money(0);
    Money moneyCreatedTotal = Money(0);
    Money moneyResidual = Money(0);
    Money moneyLastCycleResidual = Money(0);
    Money moneyWorstCycleResidual = Money(0);
    int moneyWorstCycle = -1;
    ConstructionService constructionService;
    ConstructionSystem constructionSystem;
    std::vector<std::unique_ptr<Country>> countries;
    WarehouseNetwork warehouseNetwork;
    std::vector<TradePath> tradePaths;
    std::unordered_map<int, int> continentIndexById;
    std::unordered_map<int, int> regionIndexById;
    std::unordered_map<int, int> provinceIndexById;
    std::unordered_map<int, int> countryIndexById;
    std::unordered_map<int, int> lastExpansionAICycleByCountry;
    std::unordered_map<int, int> provinceIndexByMarketId;
    std::unordered_map<std::string, int> continentIndexByKey;
    std::unordered_map<std::string, int> regionIndexByKey;
    std::unordered_map<std::string, int> countryIndexByKey;
    std::unordered_map<std::string, int> countryIndexByTag;
    std::unordered_map<std::string, int> provinceIndexByKey;
    int nextContinentId = 0;
    int nextRegionId = 0;
    int nextProvinceId = 0;
    int nextCountryId = 0;
    int nextMarketId = 0;
    int nextNationalMarketId = 0;
    int nextTradePathId = 0;
    int currentProvinceIdx = 0;
    bool debugFiveMarketScenario = false;
    mutable std::uint64_t transportationCacheRevision = 0;
    mutable std::uint64_t transportationSnapshotBuilds = 0;
    mutable std::unordered_map<int, TransportationSnapshot>
        transportationSnapshotCache;
};
