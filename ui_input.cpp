// ==================== ui_input.cpp ====================
#include "ui_internal.h"

#include <algorithm>
#include <cmath>
#include <vector>

void OpenBuildingDetail(UIState* state, int provinceId, int typeIndex) {
    if (state == nullptr) return;
    state->selectedProvinceId = provinceId;
    state->countrySelectedProvinceId = provinceId;
    state->provinceTab = 1;
    state->selectedBuilding = typeIndex;
    state->provincePageScroll[1] = 0;
    state->localMarketUI.currentPanel = 1;
    state->localMarketUI.selectedBuilding = typeIndex;
    state->localMarketUI.buildingScroll = std::max(0, typeIndex - 2);
    state->localMarketUI.goodsScroll = 0;
    state->localMarketUI.constructionScroll = 0;
    state->localMarketUI.orderScroll = 0;
    state->view = UIView::ProvinceDetail;
}

void InitUIState(UIState* state) {
    InitDebugUIState(&state->localMarketUI);
    state->mapMode = MapMode::Political;
    state->provinceReturnView = UIView::WorldMap;
    state->view = UIView::WorldMap;
    state->selectedProvinceId = -1;
    state->hoveredProvinceId = -1;
    state->playerCountryId = -1;
    state->selectedCountryId = -1;
    state->selectedGood = -1;
    state->goodsScroll = 0;
    state->countryTab = 0;
    state->provinceTab = 0;
    state->provincePageScroll.fill(0);
    state->panelConsumesInput = false;
    state->countrySelectedProvinceId = -1;
    state->countryProvinceScroll = 0;
    state->mapScrollX = 0.0f;
    state->mapScrollY = 0.5f;
    state->mapZoom = 1.0f;
    state->mapDragActive = false;
    state->mapInputEnabled = true;
    state->constructionPanelOpen = false;
    state->constructionHistoryVisible = false;
    state->constructionListScroll = 0;
    state->selectedConstructionProjectId = 0;
    state->selectedBuilding = 0;
    state->paused = true;
    state->simulationSpeed = 1;
    state->selectedTransportRouteId = -1;
    state->selectedTransportOrderId = NO_WAREHOUSE_ORDER;
    state->transportShipmentScroll = 0;
    state->transportOrderScroll = 0;
    state->constructionMessage.clear();
    state->constructionSucceeded = false;
    state->localMarketUI.buildingScroll = 0;
    const UILayout layout = CurrentUILayout();
    for (int i = 0; i < 5; ++i) {
        state->speedBtns[i] = layout.speedButtons[static_cast<std::size_t>(i)];
    }
    state->backButton = layout.localBackButton;
}

void HandleProvinceDetailInput(UIState* state, World& world) {
    const Province* selectedProvince = nullptr;
    for (int index = 0; index < world.getProvinceCount(); ++index) {
        if (world.getProvince(index).getId() == state->selectedProvinceId) {
            selectedProvince = &world.getProvince(index);
            break;
        }
    }
    if (selectedProvince == nullptr || state->playerCountryId < 0 ||
        selectedProvince->getCountryId() != state->playerCountryId) {
        state->view = UIView::WorldMap;
        state->panelConsumesInput = false;
        return;
    }
    state->panelConsumesInput = true;
    const UILayout layout = CurrentUILayout();
    const Vector2 mouse = GetMousePosition();
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsKeyPressed(KEY_ESCAPE) ||
        (left && CheckCollisionPointRec(mouse, layout.localBackButton))) {
        NavigateBack(state);
        return;
    }
    const int provinceId = state->selectedProvinceId;
    if (provinceId >= 0 &&
        HandleEmbeddedLocalMarketInput(&state->localMarketUI, world,
                                       provinceId, layout.provincePanel)) {
        return;
    }
    // The embedded local-market desk is the only province-detail UI.
    // Pointer input outside it belongs to the still-visible world map.
    return;
}

void HandleInput(UIState* state, World& world) {
    // A drag that started on the map owns the pointer until middle-button
    // release, even if the cursor crosses an overlay while moving.
    if (state->mapDragActive) {
        HandleWorldMapInput(state, world);
        return;
    }
    if (state->view == UIView::CommodityMarket) {
        const UIView viewBeforePanelInput = state->view;
        HandleCommodityMarketInput(state, world);
        if (state->view != viewBeforePanelInput) return;
        HandleWorldMapInput(state, world);
        return;
    }
    // The right-side queue is independent of the left country/province desk.
    // It claims clicks and wheel input in its own rectangle before map hit tests.
    if (HandleConstructionListInput(state, world)) return;
    if (state->view == UIView::WorldMap) {
        HandleWorldMapInput(state, world);
        return;
    }
    const UIView viewBeforePanelInput = state->view;
    if (state->view == UIView::TransportLogistics) {
        HandleTransportInput(state, world);
        return;
    } else if (state->view == UIView::CountryOverview ||
               state->view == UIView::NationalMarket) {
        HandleCountryOverviewInput(state, world);
    } else {
        HandleProvinceDetailInput(state, world);
    }
    // A panel command such as Back or a province-row navigation consumes the
    // click for this frame. Do not reinterpret the same mouse event against
    // the map after the view transition has removed the overlay.
    if (state->view != viewBeforePanelInput) return;
    HandleWorldMapInput(state, world);
}
