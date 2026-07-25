#pragma once
#include "raylib.h"
#include "market_simulation.h"

struct UIState {
    int currentPanel;          // 0=商品市场, 1=建筑列表, 2=建造队列
    int selectedGood;
    int selectedBuilding;
    bool paused;
    int simulationSpeed;
    Rectangle speedBtns[4];
    Rectangle panelBtns[3];
    int constructionPage;
    bool showInTotal[NUM_GOODS];  // 总表显示控制
};

void InitUIState(UIState* state);
void HandleInput(UIState* state, MarketSimulation& sim);
void DrawUI(const UIState* state, const MarketSimulation& sim, Font font);