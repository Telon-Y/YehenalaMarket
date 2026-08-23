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
    state->selectedCountryId = -1;
    state->countryTab = 0;
    state->provinceTab = 0;
    state->provincePageScroll.fill(0);
    state->panelConsumesInput = false;
    state->countrySelectedProvinceId = -1;
    state->countryProvinceScroll = 0;
    state->mapScrollX = 0.0f;
    state->mapZoom = 1.0f;
    state->mapInputEnabled = true;
    state->constructionPanelOpen = false;
    state->constructionListScroll = 0;
    state->selectedBuilding = 0;
    state->paused = true;
    state->simulationSpeed = 1;
    state->selectedTransportRouteId = -1;
    state->selectedTransportOrderId = NO_WAREHOUSE_ORDER;
    state->transportShipmentScroll = 0;
    state->transportOrderScroll = 0;
    const UILayout layout = CurrentUILayout();
    for (int i = 0; i < 5; ++i) {
        state->speedBtns[i] = layout.speedButtons[static_cast<std::size_t>(i)];
    }
    state->backButton = layout.localBackButton;
}

void HandleProvinceDetailInput(UIState* state, World& world) {
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
    // The embedded market desk owns every interaction inside the left
    // two-fifths panel; do not let legacy province hit tests run there.
    if (CheckCollisionPointRec(mouse, layout.provincePanel)) return;
    if (provinceId < 0) return;
    const ProvinceSnapshot snapshot = world.getProvinceSnapshot(provinceId);
    const Rectangle content = layout.provincePageContent;
    const ProvinceBuildingGridLayout buildingGrid =
        ComputeProvinceBuildingGridLayout(content.width, content.height,
                                          static_cast<int>(snapshot.buildings.size()));
    const int rowCount = state->provinceTab == 1
        ? buildingGrid.totalRows
        : state->provinceTab == 2
            ? static_cast<int>(snapshot.populationClasses.size()) : 0;
    const int provinceRowHeight = state->provinceTab == 1
        ? static_cast<int>(buildingGrid.cardSize + buildingGrid.gap)
        : state->provinceTab == 2 ? 44 : 30;
    const float provinceListReserve = state->provinceTab == 1
        ? buildingGrid.listTopOffset + buildingGrid.listBottomOffset : 80.0f;
    const int visibleRows = state->provinceTab == 1
        ? buildingGrid.visibleRows
        : std::max(1, static_cast<int>(
            (content.height - provinceListReserve) / provinceRowHeight));
    const int maxScroll = state->provinceTab == 1
        ? buildingGrid.maxScroll : std::max(0, rowCount - visibleRows);
    int& scroll = state->provincePageScroll[static_cast<std::size_t>(state->provinceTab)];
    const float wheel = GetMouseWheelMove();
    if (CheckCollisionPointRec(mouse, content) && wheel != 0.0f)
        scroll += wheel > 0.0f ? -1 : 1;
    if (IsKeyPressed(KEY_UP)) --scroll;
    if (IsKeyPressed(KEY_DOWN)) ++scroll;
    scroll = std::clamp(scroll, 0, maxScroll);

    if (state->provinceTab == 1 && left) {
        const float gap = buildingGrid.gap;
        const float cardWidth = buildingGrid.cardSize;
        const float cardHeight = cardWidth;
        const int columns = buildingGrid.columns;
        const float listTop = content.y + buildingGrid.listTopOffset;
        const int row = static_cast<int>((mouse.y - listTop) /
                                         (cardHeight + gap));
        const int column = static_cast<int>((mouse.x - content.x) /
                                             (cardWidth + gap));
        const int index = (scroll + row) * columns + column;
        if (row >= 0 && column >= 0 && column < columns &&
            index >= 0 && index < static_cast<int>(snapshot.buildings.size())) {
            const Rectangle card = {content.x + column * (cardWidth + gap),
                listTop + row * (cardHeight + gap), cardWidth, cardHeight};
            if (!CheckCollisionPointRec(mouse, card)) return;
            const int type = snapshot.buildings[static_cast<std::size_t>(index)].typeIndex;
            const Rectangle minus = {card.x + card.width - 66.0f,
                                      card.y + card.height - 34.0f, 26.0f, 26.0f};
            const Rectangle plus = {card.x + card.width - 34.0f,
                                    card.y + card.height - 34.0f, 26.0f, 26.0f};
            const Province& province = world.getProvinceById(provinceId);
            if (CheckCollisionPointRec(mouse, plus)) {
                world.queueNationalConstruction(province.getCountryId(),
                                                provinceId, type, 1);
                OpenBuildingDetail(state, provinceId, type);
                return;
            }
            if (CheckCollisionPointRec(mouse, minus)) {
                const CountrySnapshot country =
                    world.getCountrySnapshot(province.getCountryId());
                for (auto it = country.constructionProjects.rbegin();
                     it != country.constructionProjects.rend(); ++it) {
                    if ((it->status == static_cast<int>(ConstructionProjectStatus::Queued) ||
                         it->status == static_cast<int>(ConstructionProjectStatus::Active)) &&
                        it->targetProvinceId == provinceId &&
                        it->typeIndex == type) {
                        world.cancelNationalConstructionProject(country.countryId,
                                                                it->id);
                        return;
                    }
                }
                return;
            }
            OpenBuildingDetail(state, provinceId, type);
            return;
        }
    }
    if (state->provinceTab == 1 && left &&
        CheckCollisionPointRec(mouse, {content.x, content.y + content.height - 38.0f,
                                       content.width, 32.0f})) {
        const Province& province = world.getProvinceById(provinceId);
        state->selectedCountryId = province.getCountryId();
        state->countrySelectedProvinceId = provinceId;
        state->countryTab = 1;
        state->view = UIView::CountryOverview;
        state->provinceReturnView = UIView::CountryOverview;
    }
}

void HandleInput(UIState* state, World& world) {
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
