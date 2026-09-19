#pragma once

#include "raylib.h"
#include "world.h"

struct DebugUIState {
    int currentPanel = 0;
    int selectedMarket = 0;
    int selectedCountryId = -1;
    int selectedGood = 0;
    int selectedBuilding = 0;
    int goodsScroll = 0;
    int buildingScroll = 0;
    int constructionScroll = 0;
    int orderScroll = 0;
    bool paused = false;
    int simulationSpeed = 1;
    bool singleStepRequested = false;
    int frameCounter = 0;
    WarehouseAudit cachedAudit{};
    bool cachedInventoryBalanced = true;
    std::string constructionMessage;
    bool constructionSucceeded = false;
    bool constructionHistoryVisible = false;
    std::uint64_t selectedConstructionProjectId = 0;
};

enum class DebugUIMode {
    FullScreen,
    EmbeddedLocalMarket
};

void InitDebugUIState(DebugUIState* state);
void HandleDebugUIInput(DebugUIState* state, World& world);
bool ConsumeDebugSingleStep(DebugUIState* state);
void DrawDebugUI(DebugUIState* state, World& world, Font font,
                 double elapsedSeconds);

void SetDebugMarketForProvince(DebugUIState* state, const World& world,
                               int provinceId);
bool HandleEmbeddedLocalMarketInput(DebugUIState* state, World& world,
                                    int provinceId, Rectangle bounds);
void DrawEmbeddedLocalMarketUI(DebugUIState* state, World& world, Font font,
                               int provinceId, Rectangle bounds,
                               double elapsedSeconds);
