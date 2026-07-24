#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>          // 提供 snprintf
#include <cstring>
#include <string>

// 字体加载函数实际未使用，保留但不会调用
Font LoadFontFromSystem() {
    // 直接返回默认字体，避免因路径问题崩溃
    return GetFontDefault();
}

void InitUIState(UIState* state) {
    state->currentPanel = 0;
    state->selectedGood = 0;
    state->selectedBuilding = 0;
    state->paused = false;
    state->simulationSpeed = 1;

    float x = 10;
    for (int i = 0; i < 4; ++i) {
        state->speedBtns[i] = { x, 5, 60, 30 };
        x += 70;
    }
    state->panelBtns[0] = { 10, 45, 130, 30 };
    state->panelBtns[1] = { 10, 85, 130, 30 };
    state->panelBtns[2] = { 10, 125, 130, 30 };
}

void HandleInput(UIState* state, MarketSimulation& sim) {
    Vector2 mouse = GetMousePosition();
    bool mouseLeft = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);

    int speeds[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i) {
        if (CheckCollisionPointRec(mouse, state->speedBtns[i]) && mouseLeft) {
            if (i == 0) state->paused = !state->paused;
            else {
                state->paused = false;
                state->simulationSpeed = speeds[i];
            }
        }
    }

    for (int i = 0; i < 3; ++i) {
        if (CheckCollisionPointRec(mouse, state->panelBtns[i]) && mouseLeft)
            state->currentPanel = i;
    }

    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            Rectangle rec = { 10, 170.f + i * 20.f, 140, 18 };
            if (CheckCollisionPointRec(mouse, rec) && mouseLeft)
                state->selectedGood = i;
        }
    }
    else if (state->currentPanel == 1) {
        for (int t = 0; t < TYPE_COUNT; ++t) {
            float y = 170.f + t * 25.f;
            Rectangle expandBtn = { 800, y, 50, 20 };
            Rectangle demolishBtn = { 860, y, 50, 20 };
            if (CheckCollisionPointRec(mouse, expandBtn) && mouseLeft)
                sim.placeOrder(t);
            if (CheckCollisionPointRec(mouse, demolishBtn) && mouseLeft)
                sim.removeBuilding(t);
        }
    }
    else if (state->currentPanel == 2) {
        Rectangle urgentBtn = { 800, 140, 160, 30 };
        if (CheckCollisionPointRec(mouse, urgentBtn) && mouseLeft) {
            ConstructionOrder order;
            order.typeIndex = CONST_DEPT;
            order.totalCost = buildingCost[CONST_DEPT];
            order.remainingCost = buildingCost[CONST_DEPT];
            sim.constructionQueue.insert(sim.constructionQueue.begin(), order);
        }
    }

    if (IsKeyPressed(KEY_UP) || GetMouseWheelMove() > 0) {
        sim.aiProfitThreshold += 0.01;
        if (sim.aiProfitThreshold > 1.0) sim.aiProfitThreshold = 1.0;
    }
    if (IsKeyPressed(KEY_DOWN) || GetMouseWheelMove() < 0) {
        sim.aiProfitThreshold -= 0.01;
        if (sim.aiProfitThreshold < 0.0) sim.aiProfitThreshold = 0.0;
    }
}

void DrawUI(const UIState* state, const MarketSimulation& sim, Font font) {
    // 顶部栏
    DrawRectangle(0, 0, GetScreenWidth(), 40, LIGHTGRAY);
    const char* speedLabels[] = { "||", "1x", "2x", "5x" };
    int speeds[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i) {
        Rectangle rec = state->speedBtns[i];
        bool active = (i == 0 && state->paused) || (i > 0 && state->simulationSpeed == speeds[i] && !state->paused);
        DrawRectangleRec(rec, active ? SKYBLUE : LIGHTGRAY);
        DrawTextEx(font, speedLabels[i], { rec.x + 5, rec.y + 5 }, 16, 1, BLACK);
    }
    DrawTextEx(font, TextFormat("Week: %d", sim.stepCount), { 300, 10 }, 16, 1, BLACK);
    DrawTextEx(font, TextFormat("AI Profit Threshold: %.2f (UP/DOWN)", sim.aiProfitThreshold), { 450, 10 }, 16, 1, BLACK);

    const char* panelNames[] = { "Goods Market", "Buildings", "Const. Queue" };
    for (int i = 0; i < 3; ++i) {
        Color col = (state->currentPanel == i) ? DARKGRAY : GRAY;
        DrawRectangleRec(state->panelBtns[i], col);
        DrawTextEx(font, panelNames[i], { state->panelBtns[i].x + 5, state->panelBtns[i].y + 5 }, 16, 1, WHITE);
    }

    DrawLine(150, 40, 150, GetScreenHeight(), DARKGRAY);
    DrawLine(0, 165, GetScreenWidth(), 165, DARKGRAY);

    int panelX = 160, panelY = 175;

    // 商品市场
    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            Rectangle rec = { (float)panelX, (float)(panelY + i * 20), 140, 20 };
            DrawRectangleRec(rec, (state->selectedGood == i) ? SKYBLUE : RAYWHITE);
            DrawTextEx(font, commodityNames[i].c_str(), { rec.x + 5, rec.y + 2 }, 14, 1, BLACK);
        }
        int g = state->selectedGood;
        DrawTextEx(font, TextFormat("Good: %s  Price: %.2f", commodityNames[g].c_str(), sim.prices[g]),
                   { (float)(panelX + 150), (float)(panelY - 10) }, 16, 1, BLACK);
        if (!sim.priceHist.empty()) {
            int startRow = std::max(0, (int)sim.priceHist.size() - 200);
            float chartX = (float)(panelX + 150), chartY = (float)(panelY + 20), chartW = 600, chartH = 200;
            DrawRectangleLines(chartX, chartY, chartW, chartH, BLACK);
            double maxP = 0, minP = 1e9;
            for (size_t r = startRow; r < sim.priceHist.size(); ++r) {
                double val = sim.priceHist[r][g];
                if (val > maxP) maxP = val;
                if (val < minP) minP = val;
            }
            if (maxP <= minP) maxP = minP + 1.0;
            int n = (int)sim.priceHist.size() - startRow;
            for (int i = 1; i < n; ++i) {
                double v0 = sim.priceHist[startRow + i - 1][g];
                double v1 = sim.priceHist[startRow + i][g];
                float x0 = chartX + (i - 1) * chartW / (n - 1);
                float y0 = chartY + chartH - (float)((v0 - minP) / (maxP - minP) * chartH);
                float x1 = chartX + i * chartW / (n - 1);
                float y1 = chartY + chartH - (float)((v1 - minP) / (maxP - minP) * chartH);
                DrawLine(x0, y0, x1, y1, RED);
            }
        }
    }
    // 建筑列表
    else if (state->currentPanel == 1) {
        DrawTextEx(font, "Building                Count  Employ%  Profit%  CashPool",
                   { (float)panelX, (float)(panelY - 20) }, 16, 1, BLACK);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            float y = (float)(panelY + t * 25);
            char buf[256];
            snprintf(buf, sizeof(buf), "%-20s %4d  %5.1f%% %+6.2f%%  %10.2f",
                     buildingTypeNames[t].c_str(),
                     sim.buildingCounts[t],
                     sim.employmentRatio[t] * 100,
                     sim.avgProfitRates[t] * 100,
                     sim.cashPools[t]);
            DrawTextEx(font, buf, { (float)panelX, y }, 14, 1, BLACK);
            Rectangle expandBtn = { 800, y, 50, 20 };
            Rectangle demolishBtn = { 860, y, 50, 20 };
            DrawRectangleRec(expandBtn, LIGHTGRAY);
            DrawTextEx(font, "Build", { expandBtn.x + 5, expandBtn.y + 2 }, 14, 1, BLACK);
            DrawRectangleRec(demolishBtn, LIGHTGRAY);
            DrawTextEx(font, "Demolish", { demolishBtn.x + 5, demolishBtn.y + 2 }, 14, 1, BLACK);
        }
    }
    // 建造队列
    else if (state->currentPanel == 2) {
        DrawTextEx(font, "Construction Queue (Remaining/Total)", { (float)panelX, (float)(panelY - 20) }, 16, 1, BLACK);
        Rectangle urgentBtn = { 800, 140, 160, 30 };
        DrawRectangleRec(urgentBtn, RED);
        DrawTextEx(font, "Urgent Constr Dept", { urgentBtn.x + 5, urgentBtn.y + 5 }, 14, 1, WHITE);
        int y = panelY;
        for (size_t i = 0; i < sim.constructionQueue.size(); ++i) {
            const auto& ord = sim.constructionQueue[i];
            float progress = (ord.totalCost > 0) ? (float)(1.0 - ord.remainingCost / ord.totalCost) : 0.0f;
            DrawTextEx(font, buildingTypeNames[ord.typeIndex].c_str(), { (float)panelX, (float)y }, 14, 1, BLACK);
            DrawRectangle((float)(panelX + 120), (float)(y + 2), 200, 12, LIGHTGRAY);
            DrawRectangle((float)(panelX + 120), (float)(y + 2), 200 * progress, 12, GREEN);
            DrawTextEx(font, TextFormat("%.0f / %.0f", ord.totalCost - ord.remainingCost, ord.totalCost),
                       { (float)(panelX + 330), (float)y }, 14, 1, BLACK);
            double constrCap = 0;
            if (sim.buildingCounts[CONST_DEPT] > 0) {
                const auto& bt = sim.buildingTemplates[CONST_DEPT];
                constrCap = sim.buildingCounts[CONST_DEPT] * bt.outputRate * sim.employmentRatio[CONST_DEPT];
            }
            int weeksLeft = (constrCap > 0) ? (int)ceil(ord.remainingCost / constrCap) : -1;
            if (weeksLeft >= 0)
                DrawTextEx(font, TextFormat("Est %d weeks", weeksLeft), { (float)(panelX + 430), (float)y }, 14, 1, BLACK);
            y += 22;
        }
    }
}