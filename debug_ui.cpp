#include "debug_ui_internal.h"

#include <algorithm>

using namespace debug_ui;

void InitDebugUIState(DebugUIState* state) {
    if (state == nullptr) return;
    *state = DebugUIState{};
    state->simulationSpeed = 1;
}

bool ConsumeDebugSingleStep(DebugUIState* state) {
    if (state == nullptr || !state->singleStepRequested) return false;
    state->singleStepRequested = false;
    return true;
}

void HandleDebugUIInput(DebugUIState* state, World& world) {
    if (state == nullptr) return;
    const DebugLayout layout = MakeLayout();
    const Vector2 mouse = GetMousePosition();
    const bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (pressed && CheckCollisionPointRec(mouse, layout.pauseButton))
        state->paused = !state->paused;
    if (pressed && CheckCollisionPointRec(mouse, layout.stepButton)) {
        state->paused = true;
        state->singleStepRequested = true;
    }
    const int speeds[3] = {1, 2, 5};
    for (int index = 0; index < 3; ++index) {
        if (pressed &&
            CheckCollisionPointRec(mouse, layout.speedButtons[index])) {
            state->paused = false;
            state->simulationSpeed = speeds[index];
        }
    }
    for (int index = 0; index < world.getMarketCount(); ++index) {
        if (pressed && CheckCollisionPointRec(
                mouse, MarketButton(layout, index, world.getMarketCount()))) {
            state->selectedMarket = index;
            world.switchMarket(index);
            const Country* country = world.getMarket(index).getFiscalCountry();
            state->selectedCountryId = country != nullptr ? country->getId() : -1;
            state->constructionScroll = 0;
            state->orderScroll = 0;
        }
    }
    for (int panel = 0; panel < 4; ++panel) {
        if (pressed &&
            CheckCollisionPointRec(mouse, layout.panelButtons[panel])) {
            state->currentPanel = panel;
        }
    }

    if (state->currentPanel == 0) {
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (pressed &&
                CheckCollisionPointRec(mouse, GoodButton(layout, good))) {
                state->selectedGood = good;
            }
        }
    } else if (state->currentPanel == 1) {
        for (int type = 0; type < TYPE_COUNT; ++type) {
            if (pressed &&
                CheckCollisionPointRec(mouse, BuildingRow(layout, type))) {
                state->selectedBuilding = type;
            }
        }
        LocalMarket& market = world.getMarket(state->selectedMarket);
        if (CanEditBuilding(state->selectedBuilding) && pressed &&
            CheckCollisionPointRec(mouse, BuildingAddButton(layout))) {
            const Country* country = market.getFiscalCountry();
            if (country != nullptr && market.getOwnerWorld() != nullptr) {
                world.queueNationalConstruction(country->getId(),
                                                market.getProvinceId(),
                                                state->selectedBuilding, 1);
            } else {
                market.playerBuild(state->selectedBuilding, 1);
            }
        }
        if (CanEditBuilding(state->selectedBuilding) &&
            market.getBuildingCounts()[state->selectedBuilding] > 0 &&
            pressed &&
            CheckCollisionPointRec(mouse, BuildingRemoveButton(layout))) {
            market.playerDemolish(state->selectedBuilding, 1);
        }
    } else if (state->currentPanel == 2) {
        if (pressed && CheckCollisionPointRec(
                mouse, ConstructionDepartmentButton(layout))) {
            LocalMarket& market = world.getMarket(state->selectedMarket);
            const Country* country = market.getFiscalCountry();
            if (country != nullptr && market.getOwnerWorld() != nullptr) {
                world.queueNationalConstruction(country->getId(),
                                                market.getProvinceId(),
                                                CONST_DEPT, 1);
            } else {
                market.playerBuild(CONST_DEPT, 1);
            }
        }
    }

    const float wheel = GetMouseWheelMove();
    if (state->currentPanel == 2 && wheel != 0.0f) {
        state->constructionScroll = std::max(
            0, state->constructionScroll - static_cast<int>(wheel * 3.0f));
    } else if (state->currentPanel == 3 && wheel != 0.0f) {
        state->orderScroll = std::max(
            0, state->orderScroll - static_cast<int>(wheel * 3.0f));
        state->goodsScroll = std::max(
            0, state->goodsScroll - static_cast<int>(wheel * 3.0f));
    } else if (state->currentPanel == 0 && wheel != 0.0f) {
        state->goodsScroll = std::max(
            0, state->goodsScroll - static_cast<int>(wheel * 3.0f));
    }

    if (IsKeyPressed(KEY_SPACE)) state->paused = !state->paused;
    if (IsKeyPressed(KEY_PERIOD)) {
        state->paused = true;
        state->singleStepRequested = true;
    }
    if (IsKeyPressed(KEY_ONE) || IsKeyPressed(KEY_KP_1)) {
        state->paused = false;
        state->simulationSpeed = 1;
    }
    if (IsKeyPressed(KEY_TWO) || IsKeyPressed(KEY_KP_2)) {
        state->paused = false;
        state->simulationSpeed = 2;
    }
    if (IsKeyPressed(KEY_FIVE) || IsKeyPressed(KEY_KP_5)) {
        state->paused = false;
        state->simulationSpeed = 5;
    }

    state->selectedMarket = std::clamp(
        state->selectedMarket, 0, std::max(0, world.getMarketCount() - 1));
    const Country* selectedCountry = world.getMarket(state->selectedMarket).getFiscalCountry();
    state->selectedCountryId = selectedCountry != nullptr ? selectedCountry->getId() : -1;
    state->selectedGood = std::clamp(state->selectedGood, 0, NUM_GOODS - 1);
    state->selectedBuilding =
        std::clamp(state->selectedBuilding, 0, TYPE_COUNT - 1);
}





void DrawDebugUI(DebugUIState* state, World& world, Font font,
                 double elapsedSeconds) {
    if (state == nullptr || world.getMarketCount() <= 0) return;
    state->selectedMarket = std::clamp(
        state->selectedMarket, 0, world.getMarketCount() - 1);
    const DebugLayout layout = MakeLayout();
    RefreshAudit(state, world);
    const TransportationSnapshot transport =
        world.getTransportationSnapshot();
    DrawShell(state, world, font, layout, elapsedSeconds, transport);

    BeginScissorMode(static_cast<int>(layout.contentX - 2.0f),
                     static_cast<int>(layout.contentY - 3.0f),
                     static_cast<int>(layout.contentWidth + 4.0f),
                     static_cast<int>(layout.contentHeight + 6.0f));
    switch (state->currentPanel) {
    case 0:
        DrawGoodsPanel(state, world, font, layout, transport);
        break;
    case 1:
        DrawBuildingsPanel(state, world, font, layout);
        break;
    case 2:
        DrawConstructionPanel(state, world, font, layout);
        break;
    default:
        DrawMacroPanel(state, world, font, layout, transport);
        break;
    }
    EndScissorMode();
}

namespace {
int FindMarketIndex(const World& world, int marketId) {
    for (int index = 0; index < world.getMarketCount(); ++index) {
        if (world.getMarket(index).getMarketId() == marketId) return index;
    }
    return -1;
}

void CancelNewestNationalProject(World& world, const Country& country,
                                 int provinceId, int type) {
    const CountrySnapshot snapshot = world.getCountrySnapshot(country.getId());
    for (auto it = snapshot.constructionProjects.rbegin();
         it != snapshot.constructionProjects.rend(); ++it) {
        if ((it->status == static_cast<int>(ConstructionProjectStatus::Queued) ||
             it->status == static_cast<int>(ConstructionProjectStatus::Active)) &&
            it->targetProvinceId == provinceId && it->typeIndex == type) {
            world.cancelNationalConstructionProject(country.getId(), it->id);
            return;
        }
    }
}
}  // namespace

void SetDebugMarketForProvince(DebugUIState* state, const World& world,
                               int provinceId) {
    if (state == nullptr || provinceId < 0 ||
        provinceId >= world.getProvinceCount()) {
        return;
    }
    const Province& province = world.getProvinceById(provinceId);
    const int marketIndex = FindMarketIndex(world, province.getLocalMarketId());
    if (marketIndex < 0) return;
    if (state->selectedMarket != marketIndex) {
        state->selectedMarket = marketIndex;
        state->goodsScroll = 0;
        state->constructionScroll = 0;
        state->orderScroll = 0;
    }
    state->selectedCountryId = province.getCountryId();
}

bool HandleEmbeddedLocalMarketInput(DebugUIState* state, World& world,
                                    int provinceId, Rectangle bounds) {
    if (state == nullptr || world.getMarketCount() <= 0 ||
        !CheckCollisionPointRec(GetMousePosition(), bounds)) {
        return false;
    }
    SetDebugMarketForProvince(state, world, provinceId);
    const DebugLayout layout =
        MakeLayout(DebugUIMode::EmbeddedLocalMarket, bounds);
    const Vector2 mouse = GetMousePosition();
    const bool pressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    if (pressed && CheckCollisionPointRec(mouse, layout.backButton))
        return false;
    for (int panel = 0; panel < 4; ++panel) {
        if (pressed && CheckCollisionPointRec(mouse, layout.panelButtons[panel])) {
            state->currentPanel = panel;
            state->goodsScroll = 0;
            state->constructionScroll = 0;
            state->orderScroll = 0;
            return true;
        }
    }

    if (state->currentPanel == 0) {
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (pressed && CheckCollisionPointRec(mouse, GoodButton(layout, good))) {
                state->selectedGood = good;
                return true;
            }
        }
    } else if (state->currentPanel == 1) {
        for (int type = 0; type < TYPE_COUNT; ++type) {
            if (pressed &&
                CheckCollisionPointRec(mouse, BuildingRow(layout, type))) {
                state->selectedBuilding = type;
                return true;
            }
        }
        LocalMarket& market = world.getMarket(state->selectedMarket);
        const Country* country = market.getFiscalCountry();
        if (CanEditBuilding(state->selectedBuilding) && pressed &&
            CheckCollisionPointRec(mouse, BuildingAddButton(layout))) {
            if (country != nullptr && market.getOwnerWorld() != nullptr) {
                world.queueNationalConstruction(country->getId(),
                                                market.getProvinceId(),
                                                state->selectedBuilding, 1);
            } else {
                market.playerBuild(state->selectedBuilding, 1);
            }
            return true;
        }
        if (CanEditBuilding(state->selectedBuilding) &&
            market.getBuildingCounts()[state->selectedBuilding] > 0 &&
            pressed && CheckCollisionPointRec(mouse, BuildingRemoveButton(layout))) {
            if (country != nullptr && market.getOwnerWorld() != nullptr)
                CancelNewestNationalProject(*market.getOwnerWorld(), *country,
                                             market.getProvinceId(),
                                             state->selectedBuilding);
            else
                market.playerDemolish(state->selectedBuilding, 1);
            return true;
        }
    } else if (state->currentPanel == 2 && pressed &&
               CheckCollisionPointRec(mouse,
                                      ConstructionDepartmentButton(layout))) {
        LocalMarket& market = world.getMarket(state->selectedMarket);
        const Country* country = market.getFiscalCountry();
        if (country != nullptr && market.getOwnerWorld() != nullptr)
            world.queueNationalConstruction(country->getId(),
                                            market.getProvinceId(), CONST_DEPT, 1);
        else
            market.playerBuild(CONST_DEPT, 1);
        return true;
    }

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f && CheckCollisionPointRec(
            mouse, {layout.contentX, layout.contentY,
                    layout.contentWidth, layout.contentHeight})) {
        if (state->currentPanel == 2)
            state->constructionScroll = std::max(
                0, state->constructionScroll - static_cast<int>(wheel * 3.0f));
        else if (state->currentPanel == 3)
            state->orderScroll = std::max(
                0, state->orderScroll - static_cast<int>(wheel * 3.0f));
        if (state->currentPanel == 3)
            state->goodsScroll = std::max(
                0, state->goodsScroll - static_cast<int>(wheel * 3.0f));
        else if (state->currentPanel == 0)
            state->goodsScroll = std::max(
                0, state->goodsScroll - static_cast<int>(wheel * 3.0f));
        return true;
    }
    return true;
}

void DrawEmbeddedLocalMarketUI(DebugUIState* state, World& world, Font font,
                               int provinceId, Rectangle bounds,
                               double elapsedSeconds) {
    if (state == nullptr || world.getMarketCount() <= 0) return;
    SetDebugMarketForProvince(state, world, provinceId);
    const DebugLayout layout =
        MakeLayout(DebugUIMode::EmbeddedLocalMarket, bounds);
    RefreshAudit(state, world);
    const TransportationSnapshot transport = world.getTransportationSnapshot();
    DrawShell(state, world, font, layout, elapsedSeconds, transport);
    BeginScissorMode(static_cast<int>(layout.contentX - 2.0f),
                     static_cast<int>(layout.contentY - 3.0f),
                     std::max(1, static_cast<int>(layout.contentWidth + 4.0f)),
                     std::max(1, static_cast<int>(layout.contentHeight + 6.0f)));
    switch (state->currentPanel) {
    case 0:
        DrawGoodsPanel(state, world, font, layout, transport);
        break;
    case 1:
        DrawBuildingsPanel(state, world, font, layout);
        break;
    case 2:
        DrawConstructionPanel(state, world, font, layout);
        break;
    default:
        DrawMacroPanel(state, world, font, layout, transport);
        break;
    }
    EndScissorMode();
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
}
