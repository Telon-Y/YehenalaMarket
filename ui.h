// ui.h
#pragma once
#include "raylib.h"
#include "world.h"

struct UIState {
    int currentPanel;
    int selectedGood;
    int selectedBuilding;
    bool paused;
    int simulationSpeed;        // 1, 2, 5, 或 -1 表示无限速
    Rectangle speedBtns[5];     // 5 个速度按钮（暂停/1x/2x/5x/无限）
    Rectangle panelBtns[4];
    int constructionPage;
    bool showInTotal[NUM_GOODS];
};

void InitUIState(UIState* state);
void HandleInput(UIState* state, World& world);
void DrawUI(const UIState* state, World& world, Font font, double elapsedSeconds);