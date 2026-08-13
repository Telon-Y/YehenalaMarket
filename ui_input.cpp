// ==================== ui_input.cpp ====================
// UI 状态初始化与输入处理
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <vector>

void InitUIState(UIState* state) {
    state->currentPanel = 0;
    state->selectedGood = 0;
    state->selectedBuilding = 0;
    state->paused = false;
    state->simulationSpeed = -1;    // 默认无限速，便于长期调试
    state->constructionPage = 0;

    float x = 10;
    for (int i = 0; i < 5; ++i) {
        state->speedBtns[i] = { x, 7, 90, 40 };
        x += 100;                    // 每个按钮间隔 10 像素
    }

    state->panelBtns[0] = { 10, 55, 170, 40 };
    state->panelBtns[1] = { 10, 100, 170, 40 };
    state->panelBtns[2] = { 10, 145, 170, 40 };
    state->panelBtns[3] = { 10, 190, 170, 40 };

    for (int i = 0; i < NUM_GOODS; ++i)
        state->showInTotal[i] = true;
}

void HandleInput(UIState* state, World& world) {
    LocalMarket& market = world.getCurrentMarket();
    Vector2 mouse = GetMousePosition();
    bool mouseLeft = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    // 索引0=暂停/继续，1=1x，2=2x，3=5x，4=无限
    int speeds[] = {0, 1, 2, 5, -1};

    for (int i = 0; i < 5; ++i) {
        if (CheckCollisionPointRec(mouse, state->speedBtns[i]) && mouseLeft) {
            if (i == 0) {
                state->paused = !state->paused;
            } else {
                state->paused = false;
                state->simulationSpeed = speeds[i];
            }
        }
    }

    for (int i = 0; i < 4; ++i) {
        if (CheckCollisionPointRec(mouse, state->panelBtns[i]) && mouseLeft)
            state->currentPanel = i;
    }

    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            float recY = 235.f + i * 28.f;
            Rectangle checkRec = { 10, recY + 2, 16, 16 };
            if (CheckCollisionPointRec(mouse, checkRec) && mouseLeft)
                state->showInTotal[i] = !state->showInTotal[i];
            Rectangle nameRec = { 30, recY, 200, 24 };
            if (CheckCollisionPointRec(mouse, nameRec) && mouseLeft)
                state->selectedGood = i;
        }
    } else if (state->currentPanel == 1) {
        constexpr int panelX = 240;
        constexpr int panelY = 245;
        const float colOwner = panelX + 860.0f;
        const float btnStartX = colOwner + 80.0f;

        for (int t = 0; t < TYPE_COUNT; ++t) {
            bool canBuild = (t != BANK && t != FINANCE && t != CONST_DEPT &&
                             t != INDUSTRIAL_BANK && t != SAVINGS_BANK);
            float y = panelY + (t + 1) * 36;
            float bx = btnStartX;
            for (int i = 0; i < 3; ++i) {
                Rectangle btn = { bx, y, BTN_W, BTN_H };
                if (canBuild && CheckCollisionPointRec(mouse, btn) && mouseLeft) {
                    int cnt = (i == 0 ? 1 : (i == 1 ? 5 : 10));
                    market.playerBuild(t, cnt);
                }
                bx += BTN_W + BTN_GAP;
            }
            // 所有银行、金融区、建造部门不可拆除
            if (market.getBuildingCounts()[t] > 0 &&
                t != BANK && t != FINANCE && t != CONST_DEPT &&
                t != INDUSTRIAL_BANK && t != SAVINGS_BANK) {
                for (int i = 0; i < 3; ++i) {
                    Rectangle btn = { bx, y, BTN_W, BTN_H };
                    if (CheckCollisionPointRec(mouse, btn) && mouseLeft) {
                        int cnt = (i == 0 ? 1 : (i == 1 ? 5 : 10));
                        market.playerDemolish(t, cnt);
                    }
                    bx += BTN_W + BTN_GAP;
                }
            }
        }
    } else if (state->currentPanel == 2) {
        Rectangle urgentBtn = { 1600, 140, 220, 40 };
        if (CheckCollisionPointRec(mouse, urgentBtn) && mouseLeft) {
            market.playerBuild(CONST_DEPT, 1);
        }
        int totalItems = (int)market.getConstructionQueue().size();
        int totalPages = std::max(1, (int)std::ceil(totalItems / 20.0));
        if (IsKeyPressed(KEY_LEFT)) {
            state->constructionPage--;
            if (state->constructionPage < 0) state->constructionPage = 0;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            state->constructionPage++;
            if (state->constructionPage >= totalPages) state->constructionPage = totalPages - 1;
        }
    }

    float wheel = GetMouseWheelMove();
    if (IsKeyPressed(KEY_UP) || (state->currentPanel == 2 && wheel > 0)) {
        market.setAIProfitThreshold(market.getAIProfitThreshold() + 0.01);
        if (market.getAIProfitThreshold() > 1.0) market.setAIProfitThreshold(1.0);
    }
    if (IsKeyPressed(KEY_DOWN) || (state->currentPanel == 2 && wheel < 0)) {
        market.setAIProfitThreshold(market.getAIProfitThreshold() - 0.01);
        if (market.getAIProfitThreshold() < 0.0) market.setAIProfitThreshold(0.0);
    }
}
