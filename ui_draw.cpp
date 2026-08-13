// ==================== ui_draw.cpp ====================
// 主绘制函数与四个面板
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

void DrawUI(const UIState* state, World& world, Font font, double elapsedSeconds) {
    LocalMarket& market = world.getCurrentMarket();
    DrawRectangle(0, 0, GetScreenWidth(), 56, LIGHTGRAY);

    // 速度按钮标签与速度值
    const char* speedLabels[5];
    speedLabels[0] = state->paused ? "继续" : "暂停";
    speedLabels[1] = "1倍";
    speedLabels[2] = "2倍";
    speedLabels[3] = "5倍";
    speedLabels[4] = "无限";
    int speeds[] = {0, 1, 2, 5, -1};

    for (int i = 0; i < 5; ++i) {
        Rectangle rec = state->speedBtns[i];
        bool active = false;
        if (i == 0) active = state->paused;
        else active = (!state->paused && state->simulationSpeed == speeds[i]);
        DrawRectangleRec(rec, active ? SKYBLUE : LIGHTGRAY);
        DrawTextEx(font, speedLabels[i], { rec.x + 8, rec.y + 8 }, 22, 1, BLACK);
    }

    // 周期、AI阈值、计时器显示
    DrawTextEx(font, TextFormat("周期: %d", market.getStepCount()), { 450, 14 }, 22, 1, BLACK);
    DrawTextEx(font, TextFormat("AI 利润阈值: %.2f (上/下键)", market.getAIProfitThreshold()), { 700, 14 }, 20, 1, BLACK);

    double avgSpeed = (elapsedSeconds > 0.0) ? (market.getStepCount() / elapsedSeconds) : 0.0;
    DrawTextEx(font, TextFormat("运行: %.1f 秒  平均: %.1f 周/秒", elapsedSeconds, avgSpeed),
               { 1050, 14 }, 20, 1, DARKBLUE);

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

    if (state->currentPanel == 0) {
        // ========== 商品市场面板 ==========
        for (int i = 0; i < NUM_GOODS; ++i) {
            float recY = 235.f + i * 28.f;
            Rectangle checkRec = { 10, recY + 2, 16, 16 };
            DrawRectangleRec(checkRec, state->showInTotal[i] ? GREEN : LIGHTGRAY);
            DrawRectangleLinesEx(checkRec, 1, DARKGRAY);
            if (state->showInTotal[i]) {
                DrawLine(checkRec.x + 2, checkRec.y + 8, checkRec.x + 6, checkRec.y + 12, WHITE);
                DrawLine(checkRec.x + 6, checkRec.y + 12, checkRec.x + 14, checkRec.y + 4, WHITE);
            }
            Rectangle nameRec = { 30, recY, 200, 24 };
            DrawRectangleRec(nameRec, (state->selectedGood == i) ? SKYBLUE : RAYWHITE);
            DrawTextEx(font, commodityNames[i].c_str(), { nameRec.x + 4, nameRec.y + 2 }, 20, 1, BLACK);
        }

        int g = state->selectedGood;
        float detailX = panelX + 10, detailY = panelY - 5;
        DrawTextEx(font, TextFormat("商品: %s", commodityNames[g].c_str()), { detailX, detailY }, 24, 1, BLACK);
        detailY += 28;
        double currentPrice = market.getPrices()[g].toDouble();
        double refPrice = referencePrice[g];
        double pctChange = (refPrice > 0) ? (currentPrice - refPrice) / refPrice * 100.0 : 0.0;
        DrawTextEx(font, TextFormat("当前价格: %.2f   (相对初始: %+.1f%%)", currentPrice, pctChange),
                   { detailX, detailY }, 20, 1, BLACK);
        detailY += 28;
        double marketOutput = market.getLatestRealOut()[g].toDouble();
        double totalCons = market.getLatestPotentialIn()[g].toDouble() + market.getLatestConsumerTarget()[g].toDouble();
        DrawTextEx(font, TextFormat("市场产量: %.2f", marketOutput), { detailX, detailY }, 20, 1, BLACK);
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

    } else if (state->currentPanel == 1) {
        // ========== 建筑面板 ==========
        float colName    = panelX;
        float colCount   = panelX + 180;
        float colTend    = panelX + 280;
        float colEmp     = panelX + 380;
        float colRate    = panelX + 470;
        float colProfit  = panelX + 560;
        float colOutput  = panelX + 650;
        float colCash    = panelX + 750;
        float colOwner   = panelX + 860;
        float btnStartX  = colOwner + 80;

        DrawTextEx(font, "建筑名称",   { colName,   panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "现有(在建)", { colCount,  panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "雇佣倾向%",  { colTend,   panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "实际雇佣*", { colEmp,    panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "实际率%",    { colRate,   panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "利润率%",    { colProfit, panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "周产量",     { colOutput, panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "现金池",     { colCash,   panelY - 28.f }, 20, 1, BLACK);
        DrawTextEx(font, "所有权",     { colOwner,  panelY - 28.f }, 16, 1, BLACK);

        std::array<int, TYPE_COUNT> pending{};
        for (const auto& ord : market.getConstructionQueue()) pending[ord.typeIndex]++;

        float y = panelY;
        char buf[64];

        DrawTextEx(font, "自给农场", { colName, y }, 18, 1, DARKGRAY);
        snprintf(buf, sizeof(buf), "%4d(%4d)", market.getSubsistenceFarms(), 0);
        DrawTextEx(font, buf, { colCount, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colTend, y }, 18, 1, DARKGRAY);
        double subPop = market.getSubsistencePop();
        if (subPop >= 10000.0)
            snprintf(buf, sizeof(buf), "%.1f万", subPop / 10000.0);
        else
            snprintf(buf, sizeof(buf), "%.0f", subPop);
        DrawTextEx(font, buf, { colEmp, y }, 18, 1, BLUE);
        DrawTextEx(font, "--", { colRate, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colProfit, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colOutput, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colCash, y }, 18, 1, DARKGRAY);
        DrawTextEx(font, "--", { colOwner, y }, 18, 1, DARKGRAY);

        const auto& buildingOutput = market.getLatestBuildingOutput();

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
            DrawTextEx(font, buf, { colTend, y }, 18, 1, BLACK);

            double emp = market.getActualEmployment()[t];
            if (emp < 1e-6) {
                DrawTextEx(font, "--", { colEmp, y }, 18, 1, GRAY);
            } else {
                if (emp >= 10000.0)
                    snprintf(buf, sizeof(buf), "%.1f万", emp / 10000.0);
                else
                    snprintf(buf, sizeof(buf), "%.0f", emp);
                DrawTextEx(font, buf, { colEmp, y }, 18, 1, BLACK);
            }

            double empRate = market.getActualEmploymentRate()[t];
            if (market.getBuildingCounts()[t] == 0) {
                DrawTextEx(font, "--", { colRate, y }, 18, 1, GRAY);
            } else {
                snprintf(buf, sizeof(buf), "%5.1f%%", empRate * 100);
                DrawTextEx(font, buf, { colRate, y }, 18, 1, BLACK);
            }

            snprintf(buf, sizeof(buf), "%+6.2f%%", market.getAvgProfitRates()[t] * 100);
            DrawTextEx(font, buf, { colProfit, y }, 18, 1, BLACK);

            if (market.getBuildingTemplates()[t].isFinancial || market.getBuildingCounts()[t] == 0) {
                DrawTextEx(font, "--", { colOutput, y }, 18, 1, GRAY);
            } else {
                double output = buildingOutput[t].toDouble();
                if (output < 1e-3) {
                    DrawTextEx(font, "0.0", { colOutput, y }, 18, 1, GRAY);
                } else {
                    snprintf(buf, sizeof(buf), "%.1f", output);
                    DrawTextEx(font, buf, { colOutput, y }, 18, 1, BLACK);
                }
            }

            char cashStr[24];
            Money displayedCash = (t == SAVINGS_BANK)
                ? market.getInvestmentPool()
                : (t == CONST_DEPT ? market.getPlayerCash()
                                   : market.getCashPools()[t]);
            FormatCash(displayedCash, cashStr, sizeof(cashStr));
            DrawTextEx(font, cashStr, { colCash, y }, 18, 1, BLACK);

            // 所有权列：所有金融建筑显示 "--"
            if (t == BANK || t == FINANCE ||
                t == INDUSTRIAL_BANK || t == SAVINGS_BANK) {
                DrawTextEx(font, "--", { colOwner, y }, 18, 1, GRAY);
            } else {
                const auto& owned = market.getBuildingManager().getOwnedBuildings()[t];
                snprintf(buf, sizeof(buf), "%d/%d/%d", owned[OWNER_GOVERNMENT], owned[OWNER_INITIAL], owned[OWNER_FINANCE]);
                DrawTextEx(font, buf, { colOwner, y }, 18, 1, BLACK);
            }

            float bx = btnStartX;
            // 所有金融建筑不可建造
            bool canBuild = (t != BANK && t != FINANCE && t != CONST_DEPT &&
                             t != INDUSTRIAL_BANK && t != SAVINGS_BANK);
            for (int i = 0; i < 3; ++i) {
                Rectangle btn = { bx, y, BTN_W, BTN_H };
                const char* label = (i == 0 ? "建1" : (i == 1 ? "建5" : "建10"));
                Color col = canBuild ? GREEN : GRAY;
                DrawRectangleRec(btn, col);
                DrawRectangleLinesEx(btn, 1, DARKGRAY);
                DrawTextEx(font, label, { btn.x + 2, btn.y + 2 }, 12, 1, BLACK);
                bx += BTN_W + BTN_GAP;
            }
            if (market.getBuildingCounts()[t] > 0 && canBuild) {
                for (int i = 0; i < 3; ++i) {
                    Rectangle btn = { bx, y, BTN_W, BTN_H };
                    const char* label = (i == 0 ? "拆1" : (i == 1 ? "拆5" : "拆10"));
                    DrawRectangleRec(btn, RED);
                    DrawRectangleLinesEx(btn, 1, DARKGRAY);
                    DrawTextEx(font, label, { btn.x + 2, btn.y + 2 }, 12, 1, BLACK);
                    bx += BTN_W + BTN_GAP;
                }
            }
        }

        float noteY = panelY + (TYPE_COUNT + 2) * 36;
        DrawTextEx(font, "* 自给农场人口不计入阶级现金池，不通过市场消费。",
                   { (float)panelX, noteY }, 16, 1, GRAY);

    } else if (state->currentPanel == 2) {
        // ========== 建造队列面板 ==========
        const float titleY = panelY - 28;
        DrawTextEx(font, "建造队列 (剩余/总成本)", { (float)panelX, titleY }, 24, 1, BLACK);
        Rectangle urgentBtn = { 1600, 140, 220, 40 };
        DrawRectangleRec(urgentBtn, RED);
        DrawTextEx(font, "紧急建造部门", { urgentBtn.x + 15, urgentBtn.y + 8 }, 20, 1, WHITE);

        Money totalRemaining = Money(0);
        for (const auto& ord : market.getConstructionQueue()) totalRemaining += ord.remainingCost;
        double constrCapacity = market.getLastConstrProduced().toDouble();
        double constrUsed = market.getLastConstrUsed().toDouble();
        double constrWasted = constrCapacity - constrUsed;
        if (constrWasted < 0.0) constrWasted = 0.0;
        double usageRate = (constrCapacity > 0.0) ? (constrUsed / constrCapacity * 100.0) : 0.0;

        int totalWeeksLeft = -1;
        if (constrCapacity > 0 && totalRemaining > Money(0)) {
            totalWeeksLeft = (int)std::ceil(totalRemaining.toDouble() / constrCapacity);
        }

        float infoY = titleY + 58;
        DrawTextEx(font, TextFormat("建造力产出: %.2f / 周", constrCapacity),
                   { (float)panelX, infoY }, 20, 1, BLACK);
        DrawTextEx(font, TextFormat("实际使用: %.2f / 周", constrUsed),
                   { (float)panelX + 220, infoY }, 20, 1, BLACK);
        DrawTextEx(font, TextFormat("未使用: %.2f", constrWasted),
                   { (float)panelX + 440, infoY }, 20, 1, (constrWasted > 0.1) ? RED : BLACK);
        DrawTextEx(font, TextFormat("利用率: %.1f%%", usageRate),
                   { (float)panelX + 620, infoY }, 20, 1, BLACK);
        if (totalWeeksLeft >= 0)
            DrawTextEx(font, TextFormat("预计全部完成: %d 周", totalWeeksLeft),
                       { (float)panelX + 800, infoY }, 20, 1, BLACK);
        else
            DrawTextEx(font, "预计全部完成: -- 周", { (float)panelX + 800, infoY }, 20, 1, GRAY);

        int totalItems = (int)market.getConstructionQueue().size();
        int totalPages = std::max(1, (int)std::ceil(totalItems / 20.0));
        int page = state->constructionPage;
        if (page < 0) page = 0;
        if (page >= totalPages) page = totalPages - 1;
        int displayPage = page;
        DrawTextEx(font, TextFormat("第 %d / %d 页 (←→ 翻页)", displayPage + 1, totalPages),
                   { (float)panelX, titleY + 28 }, 18, 1, DARKGRAY);

        int startIdx = displayPage * 20;
        int endIdx = std::min(startIdx + 20, totalItems);
        int y = panelY + 90;
        for (int i = startIdx; i < endIdx; ++i) {
            const auto& ord = market.getConstructionQueue()[i];
            float progress = (ord.totalCost > Money(0)) ? (float)(1.0 - ord.remainingCost.toDouble() / ord.totalCost.toDouble()) : 0.0f;
            const char* ownerStr = "?";
            switch(ord.owner) {
                case OWNER_GOVERNMENT: ownerStr = "政府"; break;
                case OWNER_INITIAL:    ownerStr = "私人"; break;
                case OWNER_FINANCE:    ownerStr = "金融"; break;
                case OWNER_COUNT:      ownerStr = "未知"; break;
            }
            DrawTextEx(font, TextFormat("[%s] %s", ownerStr, buildingTypeNames[ord.typeIndex].c_str()),
                       { (float)panelX, (float)y }, 18, 1, BLACK);
            DrawRectangle(panelX + 200, y + 2, 280, 20, LIGHTGRAY);
            DrawRectangle(panelX + 200, y + 2, (int)(280 * progress), 20, GREEN);
            DrawTextEx(font, TextFormat("%.0f / %.0f", ord.totalCost.toDouble() - ord.remainingCost.toDouble(), ord.totalCost.toDouble()),
                       { (float)(panelX + 490), (float)y }, 16, 1, BLACK);

            int weeksLeft = EstimateWeeksLeft(market, i);
            if (weeksLeft >= 0)
                DrawTextEx(font, TextFormat("预计 %d 周", weeksLeft),
                           { (float)(panelX + 650), (float)y }, 16, 1, BLACK);
            else
                DrawTextEx(font, "预计 -- 周", { (float)(panelX + 650), (float)y }, 16, 1, GRAY);
            y += 30;
        }

    } else if (state->currentPanel == 3) {
        // ========== 其他面板（宏观数据） ==========
        float detailX = panelX + 10;
        float detailY = panelY + 5;
        DrawTextEx(font, "宏观数据", { detailX, detailY }, 24, 1, BLACK);
        detailY += 30;

        float chartW = 900;
        float chartH = 220;
        DrawScalarCurve(market.getGDPHistory(), detailX, detailY, chartW, chartH,
                        BLUE, font, "GDP (周度)");
        detailY += chartH + 40;

        DrawScalarCurveDouble(market.getPopulationHistory(), detailX, detailY, chartW, chartH,
                              DARKGREEN, font, "人口 (周度)");
        detailY += chartH + 30;

        // 金融数据显示在右侧
        float finX = detailX + chartW + 50;
        float finY = panelY + 40;
        DrawTextEx(font, "金融数据", { finX, finY }, 22, 1, DARKGRAY);
        finY += 30;

        char cashBuf[64];
        FormatCash(market.getTotalMoneySupply(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("货币供给: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getInvestmentPool(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("储蓄银行: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getPlayerCash(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("玩家（政府）资金池: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getBuildTransferTotal(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("建造划转累计: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getClassCash(LABORER), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("劳工: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getClassCash(ENGINEER), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("工程师: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getClassCash(CAPITALIST), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("资本家: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getBankLoanCapacity(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("工商银行可贷: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getInvestmentLoanBalance(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("投资池贷款: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        FormatCash(market.getTotalDebt(), cashBuf, sizeof(cashBuf));
        DrawTextEx(font, TextFormat("贷款总额: %s", cashBuf), { finX, finY }, 18, 1, BLACK); finY += 24;
        DrawTextEx(font, TextFormat("贷款到期: %d", market.getInvestmentLoanDueStep()),
                   { finX, finY }, 18, 1, BLACK);
        finY += 24;
        DrawTextEx(font, TextFormat("逾期周数: %d", market.getInvestmentLoanDelinquentWeeks()),
                   { finX, finY }, 18, 1,
                   market.getInvestmentLoanDelinquentWeeks() > 0 ? RED : BLACK);
        finY += 34;

        // ===== 劳动力比例饼图（不含受抚养人口） =====
        DrawTextEx(font, "劳动力比例（不含受抚养人口）",
                   { finX, finY }, 18, 1, DARKGRAY);
        finY += 30;
        DrawLaborPieChart(market, finX, finY, 130.0f, font);
    }
}
