#pragma once
#include "raylib.h"
#include "market_simulation.h"

struct UIState {
    int currentPanel;          // 0=商品市场, 1=建筑列表, 2=建造队列
    int selectedGood;          // 当前选中的商品索引
    int selectedBuilding;      // 当前选中的建筑索引
    bool paused;
    int simulationSpeed;       // 0 表示暂停，1/2/5 表示实际速度
    Rectangle speedBtns[4];    // 暂停、1x、2x、5x 按钮
    Rectangle panelBtns[3];    // 三个主面板切换按钮
};

void InitUIState(UIState* state);
Font LoadFontFromSystem();
void HandleInput(UIState* state, MarketSimulation& sim);
void DrawUI(const UIState* state, const MarketSimulation& sim, Font font);