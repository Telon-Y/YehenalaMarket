// ui.h
#pragma once
#include "raylib.h"
#include "debug_ui.h"
#include <array>
#include "world.h"

enum class UIView {
    WorldMap,
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
    int selectedCountryId;
    int countryTab;
    int countrySelectedProvinceId;
    int provinceTab;
    std::array<int, 3> provincePageScroll;
    bool panelConsumesInput;
    int countryProvinceScroll;
    float mapScrollX;
    float mapZoom;
    bool mapInputEnabled;
    bool constructionPanelOpen;
    int constructionListScroll;
    int selectedBuilding;
    bool paused;
    int simulationSpeed;
    Rectangle speedBtns[5];
    Rectangle backButton;
    int selectedTransportRouteId;
    WarehouseOrderId selectedTransportOrderId;
    int transportShipmentScroll;
    int transportOrderScroll;
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
