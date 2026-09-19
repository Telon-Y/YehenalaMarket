#pragma once

#include "construction.h"

class LocalMarket;
class World;

class ConstructionService {
public:
    explicit ConstructionService(World& world) : world(&world) {}

    ConstructionQuote quote(const ConstructionRequest& request) const;
    ConstructionCommandResult submit(const ConstructionRequest& request);
    bool cancel(int countryId, ConstructionProjectId projectId);
    bool pause(int countryId, ConstructionProjectId projectId);
    bool resume(int countryId, ConstructionProjectId projectId);
    bool setPriority(int countryId, ConstructionProjectId projectId,
                     int priority);
    bool move(int countryId, ConstructionProjectId projectId,
              bool up, bool toEdge = false);
    bool addBudget(int countryId, ConstructionProjectId projectId,
                   Money amount);
    int invalidateProvince(int countryId, int provinceId);

private:
    World* world = nullptr;
};

class StandaloneConstructionService {
public:
    static ConstructionQuote quote(
        const LocalMarket& market,
        const CountryConstructionState& state,
        const ConstructionRequest& request);
    static ConstructionCommandResult submit(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId& nextProjectId,
        const ConstructionRequest& request);
    static bool cancel(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId);
    static bool pause(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId);
    static bool resume(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId);
    static bool setPriority(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId, int priority);
    static bool move(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId, bool up, bool toEdge = false);
    static bool addBudget(
        LocalMarket& market, CountryConstructionState& state,
        ConstructionProjectId projectId, Money amount);
    static int invalidateAll(
        LocalMarket& market, CountryConstructionState& state);
    static void processCycle(
        LocalMarket& market, CountryConstructionState& state,
        Money availableConstruction, Money unitPrice,
        Money& constructionUsed, Money& constructionRevenue);
};

class ConstructionSystem {
public:
    explicit ConstructionSystem(World& world) : world(&world) {}

    void preparePlans();
    void processCycle();

private:
    World* world = nullptr;
};
