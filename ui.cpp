#include "ui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

static void FormatCash(double cash, char* buf, size_t bufSize) {
    if (cash == 0.0) { snprintf(buf, bufSize, "0.00"); return; }
    double absCash = fabs(cash);
    const char* sign = (cash < 0) ? "-" : "";
    if (absCash >= 1e9) snprintf(buf, bufSize, "%s%.2fb", sign, absCash / 1e9);
    else if (absCash >= 1e6) snprintf(buf, bufSize, "%s%.2fm", sign, absCash / 1e6);
    else if (absCash >= 1e3) snprintf(buf, bufSize, "%s%.2fk", sign, absCash / 1e3);
    else snprintf(buf, bufSize, "%.2f", cash);
}

void InitUIState(UIState* state) {
    state->currentPanel = 0;
    state->selectedGood = 0;
    state->selectedBuilding = 0;
    state->paused = false;
    state->simulationSpeed = 1;
    state->constructionPage = 0;
    float x = 10;
    for (int i = 0; i < 4; ++i) {
        state->speedBtns[i] = { x, 5, 60, 30 };
        x += 70;
    }
    state->panelBtns[0] = { 10, 45, 130, 30 };
    state->panelBtns[1] = { 10, 85, 130, 30 };
    state->panelBtns[2] = { 10, 125, 130, 30 };
    for (int i = 0; i < NUM_GOODS; ++i)
        state->showInTotal[i] = true;
}

void HandleInput(UIState* state, MarketSimulation& sim) {
    Vector2 mouse = GetMousePosition();
    bool mouseLeft = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    int speeds[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i) {
        if (CheckCollisionPointRec(mouse, state->speedBtns[i]) && mouseLeft) {
            if (i == 0) state->paused = !state->paused;
            else { state->paused = false; state->simulationSpeed = speeds[i]; }
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (CheckCollisionPointRec(mouse, state->panelBtns[i]) && mouseLeft)
            state->currentPanel = i;
    }
    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            Rectangle checkRec = { 10, 170.f + i * 20.f + 2, 12, 12 };
            if (CheckCollisionPointRec(mouse, checkRec) && mouseLeft) {
                state->showInTotal[i] = !state->showInTotal[i];
                continue;
            }
            Rectangle nameRec = { 25, 170.f + i * 20.f, 125, 18 };
            if (CheckCollisionPointRec(mouse, nameRec) && mouseLeft)
                state->selectedGood = i;
        }
    } else if (state->currentPanel == 1) {
        for (int t = 0; t < TYPE_COUNT; ++t) {
            float y = 170.f + (t + 1) * 25.f;
            Rectangle build1  = { 750, y, 30, 20 };
            Rectangle build5  = { 785, y, 30, 20 };
            Rectangle build10 = { 820, y, 30, 20 };
            Rectangle demol1   = { 860, y, 30, 20 };
            Rectangle demol5   = { 895, y, 30, 20 };
            Rectangle demol10  = { 930, y, 30, 20 };
            if (CheckCollisionPointRec(mouse, build1) && mouseLeft)  sim.placePlayerOrder(t, 1);
            if (CheckCollisionPointRec(mouse, build5) && mouseLeft)  sim.placePlayerOrder(t, 5);
            if (CheckCollisionPointRec(mouse, build10) && mouseLeft) sim.placePlayerOrder(t, 10);
            if (CheckCollisionPointRec(mouse, demol1) && mouseLeft)  sim.demolishBuildings(t, 1);
            if (CheckCollisionPointRec(mouse, demol5) && mouseLeft)  sim.demolishBuildings(t, 5);
            if (CheckCollisionPointRec(mouse, demol10) && mouseLeft) sim.demolishBuildings(t, 10);
        }
    } else if (state->currentPanel == 2) {
        Rectangle urgentBtn = { 800, 140, 180, 30 };
        if (CheckCollisionPointRec(mouse, urgentBtn) && mouseLeft) {
            ConstructionOrder order;
            order.typeIndex = CONST_DEPT;
            order.totalCost = buildingCost[CONST_DEPT];
            order.remainingCost = buildingCost[CONST_DEPT];
            order.ignoreCash = true;
            sim.constructionQueue.insert(sim.constructionQueue.begin(), order);
        }
        int totalPages = std::max(1, (int)std::ceil(sim.constructionQueue.size() / 20.0));
        if (IsKeyPressed(KEY_LEFT)) {
            state->constructionPage--;
            if (state->constructionPage < 0) state->constructionPage = 0;
        }
        if (IsKeyPressed(KEY_RIGHT)) {
            state->constructionPage++;
            if (state->constructionPage >= totalPages) state->constructionPage = totalPages - 1;
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

static void DrawPriceCurve(const std::vector<std::array<double, NUM_GOODS>>& hist,
                           int goodIdx, float chartX, float chartY, float chartW, float chartH,
                           Color color, Font font, int startRow = -1) {
    if (hist.empty()) return;
    size_t first = (startRow >= 0) ? (size_t)startRow : 0;
    if (first >= hist.size()) return;
    size_t last = hist.size(), count = last - first;
    if (count < 2) return;
    double minP = 1e30, maxP = -1e30;
    for (size_t i = first; i < last; ++i) {
        double v = hist[i][goodIdx];
        if (v < minP) minP = v;
        if (v > maxP) maxP = v;
    }
    if (maxP <= minP) maxP = minP + 1.0;
    DrawLine(chartX, chartY, chartX, chartY + chartH, BLACK);
    DrawLine(chartX, chartY + chartH, chartX + chartW, chartY + chartH, BLACK);
    DrawTextEx(font, TextFormat("%.0f", maxP), { chartX + 2, chartY }, 10, 1, BLACK);
    DrawTextEx(font, TextFormat("%.0f", minP), { chartX + 2, chartY + chartH - 12 }, 10, 1, BLACK);
    float prevX = chartX;
    float prevY = chartY + chartH - (float)((hist[first][goodIdx] - minP) / (maxP - minP) * chartH);
    for (size_t i = first + 1; i < last; ++i) {
        float x = chartX + (i - first) * chartW / (count - 1);
        float y = chartY + chartH - (float)((hist[i][goodIdx] - minP) / (maxP - minP) * chartH);
        DrawLine(prevX, prevY, x, y, color);
        prevX = x; prevY = y;
    }
}

static void DrawMultiPriceCurve(const std::vector<std::array<double, NUM_GOODS>>& hist,
                                const std::vector<int>& goodIndices,
                                const std::vector<Color>& colors,
                                Font font, Rectangle chartRect) {
    if (hist.empty() || goodIndices.empty()) return;
    size_t count = hist.size();
    if (count < 2) return;

    double minPct = 1e30, maxPct = -1e30;
    for (int g : goodIndices) {
        for (const auto& row : hist) {
            double pct = (row[g] - referencePrice[g]) / referencePrice[g] * 100.0;
            if (pct < minPct) minPct = pct;
            if (pct > maxPct) maxPct = pct;
        }
    }
    if (maxPct <= minPct) maxPct = minPct + 1.0;

    const float legendWidth = 130;
    const float legendPadding = 5;
    Rectangle plotArea = { chartRect.x, chartRect.y,
                           chartRect.width - legendWidth - legendPadding, chartRect.height };
    Rectangle legendArea = { chartRect.x + chartRect.width - legendWidth, chartRect.y,
                             legendWidth, chartRect.height };

    DrawRectangleRec(legendArea, Fade(RAYWHITE, 0.8f));
    DrawRectangleLinesEx(legendArea, 1, GRAY);
    float legendY = legendArea.y + 5;
    for (size_t i = 0; i < goodIndices.size(); ++i) {
        int g = goodIndices[i];
        DrawRectangle(legendArea.x + 5, legendY, 12, 12, colors[i]);
        DrawRectangleLines(legendArea.x + 5, legendY, 12, 12, BLACK);
        DrawTextEx(font, commodityNames[g].c_str(), { legendArea.x + 20, legendY }, 10, 1, BLACK);
        legendY += 14;
    }

    DrawLine(plotArea.x, plotArea.y, plotArea.x, plotArea.y + plotArea.height, BLACK);
    DrawLine(plotArea.x, plotArea.y + plotArea.height,
             plotArea.x + plotArea.width, plotArea.y + plotArea.height, BLACK);
    DrawTextEx(font, TextFormat("%.0f%%", maxPct), { plotArea.x + 2, plotArea.y }, 10, 1, BLACK);
    DrawTextEx(font, TextFormat("%.0f%%", minPct),
               { plotArea.x + 2, plotArea.y + plotArea.height - 12 }, 10, 1, BLACK);
    DrawTextEx(font, TextFormat("%d", (int)count),
               { plotArea.x + plotArea.width - 40, plotArea.y + plotArea.height - 12 }, 10, 1, BLACK);

    for (size_t idx = 0; idx < goodIndices.size(); ++idx) {
        int g = goodIndices[idx];
        Color col = colors[idx];
        double pct0 = (hist[0][g] - referencePrice[g]) / referencePrice[g] * 100.0;
        float prevX = plotArea.x;
        float prevY = plotArea.y + plotArea.height -
                      (float)((pct0 - minPct) / (maxPct - minPct) * plotArea.height);
        for (size_t i = 1; i < count; ++i) {
            float x = plotArea.x + (float)i / (count - 1) * plotArea.width;
            double pct = (hist[i][g] - referencePrice[g]) / referencePrice[g] * 100.0;
            float y = plotArea.y + plotArea.height -
                      (float)((pct - minPct) / (maxPct - minPct) * plotArea.height);
            DrawLine(prevX, prevY, x, y, col);
            prevX = x; prevY = y;
        }
    }
}

static int EstimateWeeksLeft(const MarketSimulation& sim, size_t orderIndex) {
    if (sim.buildingCounts[CONST_DEPT] == 0) return -1;
    const auto& constrBt = sim.buildingTemplates[CONST_DEPT];
    double capacity = sim.buildingCounts[CONST_DEPT] * constrBt.outputRate * sim.employmentRatio[CONST_DEPT];
    if (capacity <= 0.0) return -1;

    std::vector<double> rem(sim.constructionQueue.size());
    for (size_t i = 0; i < rem.size(); ++i)
        rem[i] = sim.constructionQueue[i].remainingCost;

    int weeks = 0;
    while (orderIndex < rem.size() && rem[orderIndex] > 0) {
        double avail = capacity;
        for (size_t i = 0; i < rem.size() && avail > 0; ++i) {
            if (rem[i] <= 0) continue;
            double invest = std::min({ avail, 30.0, rem[i] });
            rem[i] -= invest;
            avail -= invest;
        }
        weeks++;
        if (weeks > 10000) return -1;
    }
    return weeks;
}

void DrawUI(const UIState* state, const MarketSimulation& sim, Font font) {
    DrawRectangle(0, 0, GetScreenWidth(), 40, LIGHTGRAY);
    const char* speedLabels[] = { "暂停", "1倍", "2倍", "5倍" };
    int speeds[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i) {
        Rectangle rec = state->speedBtns[i];
        bool active = (i == 0 && state->paused) || (i > 0 && state->simulationSpeed == speeds[i] && !state->paused);
        DrawRectangleRec(rec, active ? SKYBLUE : LIGHTGRAY);
        DrawTextEx(font, speedLabels[i], { rec.x + 5, rec.y + 5 }, 16, 1, BLACK);
    }
    DrawTextEx(font, TextFormat("周期: %d", sim.stepCount), { 300, 10 }, 16, 1, BLACK);
    DrawTextEx(font, TextFormat("AI 利润阈值: %.2f (上/下键)", sim.aiProfitThreshold), { 450, 10 }, 16, 1, BLACK);
    const char* panelNames[] = { "商品市场", "建筑", "建造队列" };
    for (int i = 0; i < 3; ++i) {
        Color col = (state->currentPanel == i) ? DARKGRAY : GRAY;
        DrawRectangleRec(state->panelBtns[i], col);
        DrawTextEx(font, panelNames[i], { state->panelBtns[i].x + 5, state->panelBtns[i].y + 5 }, 16, 1, WHITE);
    }
    DrawLine(150, 40, 150, GetScreenHeight(), DARKGRAY);
    DrawLine(0, 165, GetScreenWidth(), 165, DARKGRAY);
    int panelX = 160, panelY = 175;

    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            float recY = 170.f + i * 20.f;
            Rectangle checkRec = { 10, recY + 2, 12, 12 };
            DrawRectangleRec(checkRec, state->showInTotal[i] ? GREEN : LIGHTGRAY);
            DrawRectangleLinesEx(checkRec, 1, DARKGRAY);
            if (state->showInTotal[i]) {
                DrawLine(checkRec.x + 2, checkRec.y + 6, checkRec.x + 5, checkRec.y + 10, WHITE);
                DrawLine(checkRec.x + 5, checkRec.y + 10, checkRec.x + 10, checkRec.y + 2, WHITE);
            }
            Rectangle nameRec = { 25, recY, 125, 18 };
            DrawRectangleRec(nameRec, (state->selectedGood == i) ? SKYBLUE : RAYWHITE);
            DrawTextEx(font, commodityNames[i].c_str(), { nameRec.x + 2, nameRec.y + 2 }, 14, 1, BLACK);
        }

        int g = state->selectedGood;
        float detailX = panelX + 10, detailY = panelY - 10;
        DrawTextEx(font, TextFormat("商品: %s", commodityNames[g].c_str()), { detailX, detailY }, 18, 1, BLACK);
        detailY += 22;
        double currentPrice = sim.prices[g];
        double refPrice = referencePrice[g];
        double pctChange = (refPrice > 0) ? (currentPrice - refPrice) / refPrice * 100.0 : 0.0;
        DrawTextEx(font, TextFormat("当前价格: %.2f   (相对初始: %+.1f%%)", currentPrice, pctChange),
                   { detailX, detailY }, 16, 1, BLACK);
        detailY += 20;
        double prod = sim.outputHist.empty() ? 0.0 : sim.outputHist.back()[g];
        double totalCons = sim.latestPotentialIn[g] + sim.latestConsumerTarget[g];
        DrawTextEx(font, TextFormat("市场产量: %.2f", prod), { detailX, detailY }, 16, 1, BLACK);
        detailY += 18;
        DrawTextEx(font, TextFormat("全市场消费: %.2f", totalCons), { detailX, detailY }, 16, 1, BLACK);
        detailY += 24;

        float chart1X = detailX, chart1Y = detailY;
        float chartW = 620, chartH1 = 120;
        DrawTextEx(font, "近期价格变化 (最近200周)", { chart1X, chart1Y - 18 }, 14, 1, DARKGRAY);
        int recentStart = std::max(0, (int)sim.priceHist.size() - 200);
        DrawPriceCurve(sim.priceHist, g, chart1X, chart1Y, chartW, chartH1, RED, font, recentStart);

        float chart2Y = chart1Y + chartH1 + 10;
        DrawTextEx(font, "总价格变化 (全部周期)", { chart1X, chart2Y - 18 }, 14, 1, DARKGRAY);
        float chartH2 = 120;
        DrawPriceCurve(sim.priceHist, g, chart1X, chart2Y, chartW, chartH2, BLUE, font);

        float chart3Y = chart2Y + chartH2 + 10;
        DrawTextEx(font, "价格总表 (全部商品 · 百分比变化)", { chart1X, chart3Y - 18 }, 14, 1, DARKGRAY);
        float chartH3 = 150;

        std::vector<int> shownGoods;
        std::vector<Color> colors;
        Color colorPalette[] = { RED, BLUE, GREEN, ORANGE, PURPLE, BROWN, MAROON,
                                 DARKGREEN, DARKBLUE, GOLD, PINK };
        for (int i = 0; i < NUM_GOODS; ++i) {
            if (state->showInTotal[i]) {
                shownGoods.push_back(i);
                colors.push_back(colorPalette[i % 11]);
            }
        }
        if (!shownGoods.empty()) {
            Rectangle totalRect = { chart1X, chart3Y, chartW, chartH3 };
            DrawMultiPriceCurve(sim.priceHist, shownGoods, colors, font, totalRect);
        } else {
            DrawTextEx(font, "未选择任何商品", { chart1X, chart3Y + 50 }, 14, 1, GRAY);
        }
    } else if (state->currentPanel == 1) {
        const float colName   = panelX;
        const float colCount  = panelX + 180;
        const float colEmploy = panelX + 300;
        const float colProfit = panelX + 400;
        const float colCash   = panelX + 500;
        DrawTextEx(font, "建筑名称",        { colName,   panelY - 20.f }, 16, 1, BLACK);
        DrawTextEx(font, "现有(在建)",      { colCount,  panelY - 20.f }, 16, 1, BLACK);
        DrawTextEx(font, "雇佣率%",         { colEmploy, panelY - 20.f }, 16, 1, BLACK);
        DrawTextEx(font, "利润率%",         { colProfit, panelY - 20.f }, 16, 1, BLACK);
        DrawTextEx(font, "现金池",          { colCash,   panelY - 20.f }, 16, 1, BLACK);
        std::array<int, TYPE_COUNT> pending{};
        for (const auto& ord : sim.constructionQueue) pending[ord.typeIndex]++;
        float y = panelY;
        char buf[64];
        DrawTextEx(font, "自给农场", { colName, y }, 14, 1, DARKGRAY);
        snprintf(buf, sizeof(buf), "%4d(%4d)", sim.subsistenceFarms, 0);
        DrawTextEx(font, buf, { colCount, y }, 14, 1, DARKGRAY);
        DrawTextEx(font, "--", { colEmploy, y }, 14, 1, DARKGRAY);
        DrawTextEx(font, "--", { colProfit, y }, 14, 1, DARKGRAY);
        DrawTextEx(font, "--", { colCash, y }, 14, 1, DARKGRAY);
        for (int t = 0; t < TYPE_COUNT; ++t) {
            y = panelY + (t + 1) * 25;
            bool shortage = (sim.currentSupplyRatio[t] < 1.0 - 1e-9);
            Color nameColor = shortage ? RED : BLACK;
            const char* status = shortage ? " (短缺)" : "";
            DrawTextEx(font, TextFormat("%s%s", buildingTypeNames[t].c_str(), status),
                       { colName, y }, 14, 1, nameColor);
            snprintf(buf, sizeof(buf), "%4d(%4d)", sim.buildingCounts[t], pending[t]);
            DrawTextEx(font, buf, { colCount, y }, 14, 1, BLACK);
            snprintf(buf, sizeof(buf), "%5.1f%%", sim.employmentRatio[t] * 100);
            DrawTextEx(font, buf, { colEmploy, y }, 14, 1, BLACK);
            snprintf(buf, sizeof(buf), "%+6.2f%%", sim.avgProfitRates[t] * 100);
            DrawTextEx(font, buf, { colProfit, y }, 14, 1, BLACK);
            char cashStr[16];
            FormatCash(sim.cashPools[t], cashStr, sizeof(cashStr));
            DrawTextEx(font, cashStr, { colCash, y }, 14, 1, BLACK);
            Rectangle b1  = { 750, y, 30, 20 };
            Rectangle b5  = { 785, y, 30, 20 };
            Rectangle b10 = { 820, y, 30, 20 };
            DrawRectangleRec(b1, LIGHTGRAY);  DrawTextEx(font, "建1",  { b1.x+2,  b1.y+2 }, 14, 1, BLACK);
            DrawRectangleRec(b5, LIGHTGRAY);  DrawTextEx(font, "建5",  { b5.x+2,  b5.y+2 }, 14, 1, BLACK);
            DrawRectangleRec(b10, LIGHTGRAY); DrawTextEx(font, "建10", { b10.x+2, b10.y+2 }, 14, 1, BLACK);
            Rectangle d1  = { 860, y, 30, 20 };
            Rectangle d5  = { 895, y, 30, 20 };
            Rectangle d10 = { 930, y, 30, 20 };
            DrawRectangleRec(d1, LIGHTGRAY);  DrawTextEx(font, "拆1",  { d1.x+2,  d1.y+2 }, 14, 1, RED);
            DrawRectangleRec(d5, LIGHTGRAY);  DrawTextEx(font, "拆5",  { d5.x+2,  d5.y+2 }, 14, 1, RED);
            DrawRectangleRec(d10, LIGHTGRAY); DrawTextEx(font, "拆10", { d10.x+2, d10.y+2 }, 14, 1, RED);
        }
    } else if (state->currentPanel == 2) {
        const float titleY = panelY - 20;
        DrawTextEx(font, "建造队列 (剩余/总成本)", { (float)panelX, titleY }, 16, 1, BLACK);
        Rectangle urgentBtn = { 800, 140, 180, 30 };
        DrawRectangleRec(urgentBtn, RED);
        DrawTextEx(font, "紧急建造部门", { urgentBtn.x + 10, urgentBtn.y + 5 }, 14, 1, WHITE);
        const int itemsPerPage = 20;
        int totalItems = (int)sim.constructionQueue.size();
        int totalPages = std::max(1, (int)std::ceil(totalItems / (double)itemsPerPage));
        int page = state->constructionPage;
        if (page < 0) page = 0;
        if (page >= totalPages) page = totalPages - 1;
        int displayPage = page;
        DrawTextEx(font, TextFormat("第 %d / %d 页 (←→ 翻页)", displayPage + 1, totalPages),
                   { (float)panelX, titleY + 20 }, 14, 1, DARKGRAY);
        int startIdx = displayPage * itemsPerPage;
        int endIdx = std::min(startIdx + itemsPerPage, totalItems);
        int y = panelY + 20;
        for (int i = startIdx; i < endIdx; ++i) {
            const auto& ord = sim.constructionQueue[i];
            float progress = (ord.totalCost > 0) ? (float)(1.0 - ord.remainingCost / ord.totalCost) : 0.0f;
            DrawTextEx(font, buildingTypeNames[ord.typeIndex].c_str(), { (float)panelX, (float)y }, 14, 1, BLACK);
            DrawRectangle(panelX + 120, y + 2, 200, 12, LIGHTGRAY);
            DrawRectangle(panelX + 120, y + 2, (int)(200 * progress), 12, GREEN);
            DrawTextEx(font, TextFormat("%.0f / %.0f", ord.totalCost - ord.remainingCost, ord.totalCost),
                       { (float)(panelX + 330), (float)y }, 14, 1, BLACK);

            int weeksLeft = EstimateWeeksLeft(sim, i);
            if (weeksLeft >= 0)
                DrawTextEx(font, TextFormat("预计 %d 周", weeksLeft),
                           { (float)(panelX + 430), (float)y }, 14, 1, BLACK);
            else
                DrawTextEx(font, "预计 -- 周", { (float)(panelX + 430), (float)y }, 14, 1, GRAY);
            y += 22;
        }
    }
}