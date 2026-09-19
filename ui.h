// ui.h
#pragma once
#include "raylib.h"
#include "debug_ui.h"
#include <array>
#include "world.h"

enum class UIView {
    WorldMap,
    CommodityMarket,
    ProvinceDetail,
    CountryOverview,
    NationalMarket,
    TransportLogistics
};
enum class MapMode {
    Political,
    Logistics
};


struct UIState {
    MapMode mapMode;
    UIView provinceReturnView;
    UIView view;
    int selectedProvinceId;
    int hoveredProvinceId;
    int playerCountryId;
    int selectedCountryId;
    int selectedGood;
    int goodsScroll;
    int countryTab;
    int countrySelectedProvinceId;
    int provinceTab;
    std::array<int, 3> provincePageScroll;
    bool panelConsumesInput;
    int countryProvinceScroll;
    float mapScrollX;
    float mapScrollY;
    float mapZoom;
    bool mapDragActive;
    bool mapInputEnabled;
    bool constructionPanelOpen;
    bool constructionHistoryVisible;
    int constructionListScroll;
    std::uint64_t selectedConstructionProjectId;
    int selectedBuilding;
    bool paused;
    int simulationSpeed;
    Rectangle speedBtns[5];
    Rectangle backButton;
    int selectedTransportRouteId;
    WarehouseOrderId selectedTransportOrderId;
    int transportShipmentScroll;
    int transportOrderScroll;
    std::string constructionMessage;
    bool constructionSucceeded;
    DebugUIState localMarketUI;
};

void InitUIState(UIState* state);
void HandleInput(UIState* state, World& world);
void DrawUI(UIState* state, World& world, Font font, double elapsedSeconds);

// Pure navigation transitions keep the world/model out of the UI state
// machine. Callers validate IDs against World before entering a page.
void NavigateToCountry(UIState* state, int countryId, int provinceId);
void NavigateToProvince(UIState* state, int provinceId);
void NavigateBack(UIState* state);
