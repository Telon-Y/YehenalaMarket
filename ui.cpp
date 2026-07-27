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
        state->speedBtns[i] = { x, 7, 90, 40 };
        x += 100;
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
    int speeds[] = { 0, 1, 2, 5 };

    for (int i = 0; i < 4; ++i) {
        if (CheckCollisionPointRec(mouse, state->speedBtns[i]) && mouseLeft) {
            if (i == 0) state->paused = !state->paused;
            else { state->paused = false; state->simulationSpeed = speeds[i]; }
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
        for (int t = 0; t < TYPE_COUNT; ++t) {
            float y = 245.f + (t + 1) * 36.f;
            Rectangle build1  = { 1550, y, 42, 30 };
            Rectangle build5  = { 1598, y, 42, 30 };
            Rectangle build10 = { 1646, y, 42, 30 };
            Rectangle demol1   = { 1694, y, 42, 30 };
            Rectangle demol5   = { 1742, y, 42, 30 };
            Rectangle demol10  = { 1790, y, 42, 30 };
            if (CheckCollisionPointRec(mouse, build1) && mouseLeft)  market.playerBuild(t, 1);
            if (CheckCollisionPointRec(mouse, build5) && mouseLeft)  market.playerBuild(t, 5);
            if (CheckCollisionPointRec(mouse, build10) && mouseLeft) market.playerBuild(t, 10);
            if (CheckCollisionPointRec(mouse, demol1) && mouseLeft)  market.playerDemolish(t, 1);
            if (CheckCollisionPointRec(mouse, demol5) && mouseLeft)  market.playerDemolish(t, 5);
            if (CheckCollisionPointRec(mouse, demol10) && mouseLeft) market.playerDemolish(t, 10);
        }
    } else if (state->currentPanel == 2) {
        Rectangle urgentBtn = { 1600, 140, 220, 40 };
        if (CheckCollisionPointRec(mouse, urgentBtn) && mouseLeft) {
            market.playerBuild(CONST_DEPT, 1);  // 紧急建造部门（置顶、忽略现金）
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

    if (IsKeyPressed(KEY_UP) || GetMouseWheelMove() > 0) {
        market.setAIProfitThreshold(market.getAIProfitThreshold() + 0.01);
        if (market.getAIProfitThreshold() > 1.0) market.setAIProfitThreshold(1.0);
    }
    if (IsKeyPressed(KEY_DOWN) || GetMouseWheelMove() < 0) {
        market.setAIProfitThreshold(market.getAIProfitThreshold() - 0.01);
        if (market.getAIProfitThreshold() < 0.0) market.setAIProfitThreshold(0.0);
    }
}

// ---------- 绘图辅助函数 ----------

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
    char maxBuf[32], minBuf[32];
    FormatCash(maxP, maxBuf, sizeof(maxBuf));
    FormatCash(minP, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 16 }, 14, 1, BLACK);
    float prevX = chartX;
    float prevY = chartY + chartH - (float)((hist[first][goodIdx] - minP) / (maxP - minP) * chartH);
    for (size_t i = first + 1; i < last; ++i) {
        float x = chartX + (i - first) * chartW / (count - 1);
        float y = chartY + chartH - (float)((hist[i][goodIdx] - minP) / (maxP - minP) * chartH);
        DrawLine(prevX, prevY, x, y, color);
        prevX = x; prevY = y;
    }
}

static void DrawScalarCurve(const std::vector<double>& data,
                            float chartX, float chartY, float chartW, float chartH,
                            Color color, Font font, const char* label) {
    if (data.size() < 2) return;
    double minV = *std::min_element(data.begin(), data.end());
    double maxV = *std::max_element(data.begin(), data.end());
    if (maxV <= minV) maxV = minV + 1.0;
    DrawLine(chartX, chartY, chartX, chartY + chartH, BLACK);
    DrawLine(chartX, chartY + chartH, chartX + chartW, chartY + chartH, BLACK);
    char maxBuf[32], minBuf[32];
    FormatCash(maxV, maxBuf, sizeof(maxBuf));
    FormatCash(minV, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 16 }, 14, 1, BLACK);
    if (label) DrawTextEx(font, label, { chartX + 2, chartY - 22 }, 16, 1, DARKGRAY);
    size_t count = data.size();
    float prevX = chartX;
    float prevY = chartY + chartH - (float)((data[0] - minV) / (maxV - minV) * chartH);
    for (size_t i = 1; i < count; ++i) {
        float x = chartX + (float)i / (count - 1) * chartW;
        float y = chartY + chartH - (float)((data[i] - minV) / (maxV - minV) * chartH);
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

    double minV = 1e30, maxV = -1e30;
    for (int g : goodIndices) {
        for (const auto& row : hist) {
            double v = row[g];
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
    }
    if (maxV <= minV) maxV = minV + 1.0;

    const float legendWidth = 150;
    const float legendPadding = 5;
    Rectangle plotArea = { chartRect.x, chartRect.y,
                           chartRect.width - legendWidth - legendPadding, chartRect.height };
    Rectangle legendArea = { chartRect.x + chartRect.width - legendWidth, chartRect.y,
                             legendWidth, chartRect.height };
    DrawRectangleRec(legendArea, Fade(RAYWHITE, 0.8f));
    DrawRectangleLinesEx(legendArea, 1, GRAY);

    float legendY = legendArea.y + 3;
    for (size_t i = 0; i < goodIndices.size(); ++i) {
        int g = goodIndices[i];
        DrawRectangle(legendArea.x + 5, legendY, 12, 12, colors[i]);
        DrawRectangleLines(legendArea.x + 5, legendY, 12, 12, BLACK);
        DrawTextEx(font, commodityNames[g].c_str(), { legendArea.x + 20, legendY }, 12, 1, BLACK);
        legendY += 14;
    }

    DrawLine(plotArea.x, plotArea.y, plotArea.x, plotArea.y + plotArea.height, BLACK);
    DrawLine(plotArea.x, plotArea.y + plotArea.height,
             plotArea.x + plotArea.width, plotArea.y + plotArea.height, BLACK);

    char maxBuf[32], minBuf[32];
    FormatCash(maxV, maxBuf, sizeof(maxBuf));
    FormatCash(minV, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { plotArea.x + 2, plotArea.y }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { plotArea.x + 2, plotArea.y + plotArea.height - 16 }, 14, 1, BLACK);
    DrawTextEx(font, TextFormat("%d", (int)count),
               { plotArea.x + plotArea.width - 60, plotArea.y + plotArea.height - 16 }, 14, 1, BLACK);

    for (size_t idx = 0; idx < goodIndices.size(); ++idx) {
        int g = goodIndices[idx];
        Color col = colors[idx];
        double v0 = hist[0][g];
        float prevX = plotArea.x;
        float prevY = plotArea.y + plotArea.height - (float)((v0 - minV) / (maxV - minV) * plotArea.height);
        for (size_t i = 1; i < count; ++i) {
            float x = plotArea.x + (float)i / (count - 1) * plotArea.width;
            double v = hist[i][g];
            float y = plotArea.y + plotArea.height - (float)((v - minV) / (maxV - minV) * plotArea.height);
            DrawLine(prevX, prevY, x, y, col);
            prevX = x; prevY = y;
        }
    }
}

static int EstimateWeeksLeft(const LocalMarket& market, size_t orderIndex) {
    if (market.getBuildingCounts()[CONST_DEPT] == 0) return -1;
    const auto& constrBt = market.getBuildingTemplates()[CONST_DEPT];
    double capacity = market.getBuildingCounts()[CONST_DEPT] * constrBt.outputRate * market.getEmploymentRatio()[CONST_DEPT];
    if (capacity <= 0.0) return -1;
    std::vector<double> rem(market.getConstructionQueue().size());
    for (size_t i = 0; i < rem.size(); ++i)
        rem[i] = market.getConstructionQueue()[i].remainingCost;
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

void DrawUI(const UIState* state, World& world, Font font) {
    LocalMarket& market = world.getCurrentMarket();
    DrawRectangle(0, 0, GetScreenWidth(), 56, LIGHTGRAY);

    const char* speedLabels[4];
    speedLabels[0] = state->paused ? "继续" : "暂停";
    speedLabels[1] = "1倍";
    speedLabels[2] = "2倍";
    speedLabels[3] = "5倍";
    int speeds[] = { 0, 1, 2, 5 };
    for (int i = 0; i < 4; ++i) {
        Rectangle rec = state->speedBtns[i];
        bool active = (i == 0 && state->paused) || (i > 0 && state->simulationSpeed == speeds[i] && !state->paused);
        DrawRectangleRec(rec, active ? SKYBLUE : LIGHTGRAY);
        DrawTextEx(font, speedLabels[i], { rec.x + 8, rec.y + 8 }, 22, 1, BLACK);
    }
    DrawTextEx(font, TextFormat("周期: %d", market.getStepCount()), { 450, 14 }, 22, 1, BLACK);
    DrawTextEx(font, TextFormat("AI 利润阈值: %.2f (上/下键)", market.getAIProfitThreshold()), { 700, 14 }, 20, 1, BLACK);

    const char* panelNames[] = { "商品市场", "建筑", "建造队列", "其他" };
    for (int i = 0; i < 4; ++i) {
        Color col = (state->currentPanel == i) ? DARKGRAY : GRAY;
        DrawRectangleRec(state->panelBtns[i], col);
        DrawTextEx(font, panelNames[i], { state->panelBtns[i].x + 12, state->panelBtns[i].y + 8 }, 22, 1, WHITE);
    }

    DrawLine(190, 56, 190, GetScreenHeight(), DARKGRAY);
    DrawLine(0, 240, 190, 240, DARKGRAY);

    int panelX = 240;
    int panelY = 245;

    // ----- 商品市场 -----
    if (state->currentPanel == 0) {
        for (int i = 0; i < NUM_GOODS; ++i) {
            float recY = 235.f + i * 28.f;
            Rectangle checkRec = { 10, recY + 2, 16, 16 };
            DrawRectangleRec(checkRec, state->showInTotal[i] ? GREEN : LIGHTGRAY);
            DrawRectangleLinesEx(checkRec, 1, DARKGRAY);
            if (state->showInTotal[i]) {
                DrawLine(checkRec.x + 2, checkRec.y + 8, checkRec.x + 6, checkRec.y + 12, WHITE);
                DrawLine(checkRec.x + 6, checkRec.y + 12, checkRec.x + 14, checkRec.y + 4, WHITE);
            }
            Rectangle nameRec = { 30, recY, 220, 24 };
            DrawRectangleRec(nameRec, (state->selectedGood == i) ? SKYBLUE : RAYWHITE);
            DrawTextEx(font, commodityNames[i].c_str(), { nameRec.x + 4, nameRec.y + 2 }, 20, 1, BLACK);
        }

        int g = state->selectedGood;
        float detailX = panelX + 10, detailY = panelY - 5;
        DrawTextEx(font, TextFormat("商品: %s", commodityNames[g].c_str()), { detailX, detailY }, 24, 1, BLACK);
        detailY += 28;
        double currentPrice = market.getPrices()[g];
        double refPrice = referencePrice[g];
        double pctChange = (refPrice > 0) ? (currentPrice - refPrice) / refPrice * 100.0 : 0.0;
        DrawTextEx(font, TextFormat("当前价格: %.2f   (相对初始: %+.1f%%)", currentPrice, pctChange),
                   { detailX, detailY }, 20, 1, BLACK);
        detailY += 28;
        double prod = market.getPriceHistory().empty() ? 0.0 : 0.0; // 产出需从outputHist获取，简化：此处用0，可改为 market.outputHist 但未暴露。暂时忽略。
        // 为完整，可改为从 priceHist.size 等信息推断，或者暴露 outputHist。我们先简单略过产出显示。
        // 实际可添加 getOutputHistory() 等方法，此处省略。
        double totalCons = market.getLatestPotentialIn()[g] + market.getLatestConsumerTarget()[g];
        DrawTextEx(font, TextFormat("市场产量: --"), { detailX, detailY }, 20, 1, BLACK); // 略
        detailY += 26;
        DrawTextEx(font, TextFormat("全市场消费: %.2f", totalCons), { detailX, detailY }, 20, 1, BLACK);
        detailY += 36;

        float chart1X = detailX, chart1Y = detailY;
        float chartW = 1150;
        float chartH1 = 180;
        DrawTextEx(font, "近期价格变化 (最近200周)", { chart1X, chart1Y - 22 }, 18, 1, DARKGRAY);
        int recentStart = std::max(0, (int)market.getPriceHistory().size() - 200);
        DrawPriceCurve(market.getPriceHistory(), g, chart1X, chart1Y, chartW, chartH1, RED, font, recentStart);

        float chart2Y = chart1Y + chartH1 + 30;
        float chartH2 = 180;
        DrawTextEx(font, "总价格变化 (全部周期)", { chart1X, chart2Y - 22 }, 18, 1, DARKGRAY);
        DrawPriceCurve(market.getPriceHistory(), g, chart1X, chart2Y, chartW, chartH2, BLUE, font);

        float chart3Y = chart2Y + chartH2 + 30;
        float chartH3 = 240;
        DrawTextEx(font, "价格总表 (全部商品 · 绝对价格)", { chart1X, chart3Y - 22 }, 18, 1, DARKGRAY);

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
            DrawMultiPriceCurve(market.getPriceHistory(), shownGoods, colors, font, totalRect);
        } else {
            DrawTextEx(font, "未选择任何商品", { chart1X, chart3Y + 50 }, 18, 1, GRAY);
        }

    // ----- 建筑列表 -----
    } else if (state->currentPanel == 1) {
        float colName   = panelX;
        float colCount  = panelX + 260;
        float colEmploy = panelX + 460;
        float colProfit = panelX + 660;
        float colCash   = panelX + 860;

        DrawTextEx(font, "建筑名称",   { colName,   panelY - 28.f }, 22, 1, BLACK);
        DrawTextEx(font, "现有(在建)", { colCount,  panelY - 28.f }, 22, 1, BLACK);
        DrawTextEx(font, "雇佣率%",    { colEmploy, panelY - 28.f }, 22, 1, BLACK);
        DrawTextEx(font, "利润率%",    { colProfit, panelY - 28.f }, 22, 1, BLACK);
        DrawTextEx(font, "现金池",     { colCash,   panelY - 28.f }, 22, 1, BLACK);

        std::array<int, TYPE_COUNT> pending{};
        for (const auto& ord : market.getConstructionQueue()) pending[ord.typeIndex]++;

        float y = panelY;
        char buf[64];
        DrawTextEx(font, "自给农场", { colName, y }, 18, 1, DARKGRAY);
        snprintf(buf, sizeof(buf), "%4d(%4d)", market.getSubsistenceFarms(), 0);
        DrawTextEx(font, buf, { colCount, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colEmploy, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colProfit, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colCash, y }, 18, 1, DARKGRAY);

        for (int t = 0; t < TYPE_COUNT; ++t) {
            y = panelY + (t + 1) * 36;
            bool shortage = (market.getCurrentSupplyRatio()[t] < 1.0 - 1e-9);
            Color nameColor = shortage ? RED : BLACK;
            const char* status = shortage ? " (短缺)" : "";
            DrawTextEx(font, TextFormat("%s%s", buildingTypeNames[t].c_str(), status),
                       { colName, y }, 18, 1, nameColor);
            snprintf(buf, sizeof(buf), "%4d(%4d)", market.getBuildingCounts()[t], pending[t]);
            DrawTextEx(font, buf, { colCount, y }, 18, 1, BLACK);
            snprintf(buf, sizeof(buf), "%5.1f%%", market.getEmploymentRatio()[t] * 100);
            DrawTextEx(font, buf, { colEmploy, y }, 18, 1, BLACK);
            snprintf(buf, sizeof(buf), "%+6.2f%%", market.getAvgProfitRates()[t] * 100);
            DrawTextEx(font, buf, { colProfit, y }, 18, 1, BLACK);
            char cashStr[24];
            FormatCash(market.getCashPools()[t], cashStr, sizeof(cashStr));
            DrawTextEx(font, cashStr, { colCash, y }, 18, 1, BLACK);

            Rectangle b1  = { 1550, y, 42, 30 };
            Rectangle b5  = { 1598, y, 42, 30 };
            Rectangle b10 = { 1646, y, 42, 30 };
            DrawRectangleRec(b1, LIGHTGRAY);  DrawTextEx(font, "建1",  { b1.x+5,  b1.y+4 }, 18, 1, BLACK);
            DrawRectangleRec(b5, LIGHTGRAY);  DrawTextEx(font, "建5",  { b5.x+5,  b5.y+4 }, 18, 1, BLACK);
            DrawRectangleRec(b10, LIGHTGRAY); DrawTextEx(font, "建10", { b10.x+5, b10.y+4 }, 18, 1, BLACK);
            Rectangle d1  = { 1694, y, 42, 30 };
            Rectangle d5  = { 1742, y, 42, 30 };
            Rectangle d10 = { 1790, y, 42, 30 };
            DrawRectangleRec(d1, LIGHTGRAY);  DrawTextEx(font, "拆1",  { d1.x+5,  d1.y+4 }, 18, 1, RED);
            DrawRectangleRec(d5, LIGHTGRAY);  DrawTextEx(font, "拆5",  { d5.x+5,  d5.y+4 }, 18, 1, RED);
            DrawRectangleRec(d10, LIGHTGRAY); DrawTextEx(font, "拆10", { d10.x+5, d10.y+4 }, 18, 1, RED);
        }

    // ----- 建造队列 -----
    } else if (state->currentPanel == 2) {
        const float titleY = panelY - 28;
        DrawTextEx(font, "建造队列 (剩余/总成本)", { (float)panelX, titleY }, 24, 1, BLACK);
        Rectangle urgentBtn = { 1600, 140, 220, 40 };
        DrawRectangleRec(urgentBtn, RED);
        DrawTextEx(font, "紧急建造部门", { urgentBtn.x + 15, urgentBtn.y + 8 }, 20, 1, WHITE);

        int totalItems = (int)market.getConstructionQueue().size();
        int totalPages = std::max(1, (int)std::ceil(totalItems / 20.0));
        int page = state->constructionPage;
        if (page < 0) page = 0;
        if (page >= totalPages) page = totalPages - 1;
        int displayPage = page;
        DrawTextEx(font, TextFormat("第 %d / %d 页 (←→ 翻页)", displayPage + 1, totalPages),
                   { (float)panelX, titleY + 30 }, 18, 1, DARKGRAY);

        int startIdx = displayPage * 20;
        int endIdx = std::min(startIdx + 20, totalItems);
        int y = panelY + 30;
        for (int i = startIdx; i < endIdx; ++i) {
            const auto& ord = market.getConstructionQueue()[i];
            float progress = (ord.totalCost > 0) ? (float)(1.0 - ord.remainingCost / ord.totalCost) : 0.0f;
            DrawTextEx(font, buildingTypeNames[ord.typeIndex].c_str(), { (float)panelX, (float)y }, 18, 1, BLACK);
            DrawRectangle(panelX + 180, y + 2, 300, 20, LIGHTGRAY);
            DrawRectangle(panelX + 180, y + 2, (int)(300 * progress), 20, GREEN);
            DrawTextEx(font, TextFormat("%.0f / %.0f", ord.totalCost - ord.remainingCost, ord.totalCost),
                       { (float)(panelX + 490), (float)y }, 16, 1, BLACK);

            int weeksLeft = EstimateWeeksLeft(market, i);
            if (weeksLeft >= 0)
                DrawTextEx(font, TextFormat("预计 %d 周", weeksLeft),
                           { (float)(panelX + 650), (float)y }, 16, 1, BLACK);
            else
                DrawTextEx(font, "预计 -- 周", { (float)(panelX + 650), (float)y }, 16, 1, GRAY);
            y += 30;
        }

    // ----- 其他 -----
    } else if (state->currentPanel == 3) {
        float detailX = panelX + 10, detailY = panelY + 5;
        DrawTextEx(font, "宏观数据", { detailX, detailY }, 24, 1, BLACK);
        detailY += 40;

        float chartW = 1300, chartH = 260;
        DrawScalarCurve(market.getGDPHistory(),
                        detailX, detailY, chartW, chartH,
                        BLUE, font, "GDP (周度)");
        detailY += chartH + 50;

        DrawScalarCurve(market.getPopulationHistory(),
                        detailX, detailY, chartW, chartH,
                        DARKGREEN, font, "人口 (周度)");
    }
}