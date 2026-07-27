#pragma once
#include "raylib.h"
#include "world.h"

struct UIState {
    int currentPanel;
    int selectedGood;
    int selectedBuilding;
    bool paused;
    int simulationSpeed;
    Rectangle speedBtns[4];
    Rectangle panelBtns[4];
    int constructionPage;
    bool showInTotal[NUM_GOODS];
};

void InitUIState(UIState* state);
void HandleInput(UIState* state, World& world);
void DrawUI(const UIState* state, World& world, Font font);