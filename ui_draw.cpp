// ==================== ui_draw.cpp ====================
// Main drawing functions and panels
#include "ui_internal.h"
#include "building_icons.h"
#include "number_format.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

void DrawProvinceDetailUI(const UIState* state, World& world, Font font,
                          double elapsedSeconds) {
    (void)elapsedSeconds;
    const UILayout layout = CurrentUILayout();
    const int provinceId = state->selectedProvinceId >= 0
        ? state->selectedProvinceId : world.getCurrentProvince().getId();
    const ProvinceSnapshot snapshot = world.getProvinceSnapshot(provinceId);
    const auto& buildingTemplates =
        world.getProvinceById(provinceId).getLocalMarket().getBuildingTemplates();
    const Rectangle panel = layout.provincePanel;

    BeginScissorMode(static_cast<int>(panel.x), static_cast<int>(panel.y),
                     static_cast<int>(panel.width), static_cast<int>(panel.height));
    DrawRectangleRec(panel, {244, 247, 244, 252});
    DrawRectangleRec(layout.countryHeader, {35, 49, 50, 255});
    DrawTextEx(font, snapshot.name.c_str(), {16, 14}, 24, 0, RAYWHITE);
    DrawTextEx(font, snapshot.countryName.c_str(), {16, 45}, kUiBodyFontSize, 0,
               {190, 207, 201, 255});
    DrawRectangleRec(layout.localBackButton, {69, 91, 89, 255});
    DrawTextEx(font, "返回", {layout.localBackButton.x + 18,
                               layout.localBackButton.y + 8}, kUiBodyFontSize, 0, RAYWHITE);

    const char* tabs[] = {"详情", "建筑", "人口"};
    for (int index = 0; index < 3; ++index) {
        const Rectangle tab = layout.provincePageTabs[static_cast<std::size_t>(index)];
        const bool active = state->provinceTab == index;
        DrawRectangleRec(tab, active ? Color{218, 178, 80, 255}
                                    : Color{61, 78, 78, 255});
        const Vector2 labelSize = MeasureTextEx(font, tabs[index], kUiTabFontSize, 0);
        DrawTextEx(font, tabs[index],
                   {tab.x + (tab.width - labelSize.x) * 0.5f,
                    tab.y + (tab.height - labelSize.y) * 0.5f},
                   kUiTabFontSize, 0, active ? Color{32, 42, 42, 255} : RAYWHITE);
    }
    EndScissorMode();

    const Rectangle content = layout.provincePageContent;
    BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                     static_cast<int>(content.width),
                     static_cast<int>(content.height));
    const Color ink = {33, 55, 53, 255};
    DrawTextEx(font, state->provinceTab == 0 ? "省份详情"
                                             : state->provinceTab == 1
                                                 ? "建筑"
                                                 : "人口",
               {content.x, content.y + 1.0f}, kUiHeadingFontSize, 0, ink);

    if (state->provinceTab == 0) {
        const float gap = 12.0f;
        const float columnWidth = std::max(1.0f, (content.width - gap) * 0.5f);
        const float valueY[3] = {content.y + 34.0f,
                                 content.y + 88.0f,
                                 content.y + 142.0f};
        const float columnX[2] = {content.x, content.x + columnWidth + gap};
        const char* labels[3][2] = {
            {"人口", "国内生产总值"},
            {"就业", "价格水平"},
            {"需求满足度", "模拟周期"}};
        const std::string values[3][2] = {
            {FormatChineseNumber(snapshot.population),
             FormatChineseNumber(snapshot.gdp.toDouble())},
            {TextFormat("%.1f%%", snapshot.employmentRate * 100.0),
             FormatChineseNumber(snapshot.priceLevel.toDouble(), 2)},
            {TextFormat("%.1f%%", snapshot.satisfaction * 100.0),
             TextFormat("%d", snapshot.cycle)}};
        for (int row = 0; row < 3; ++row) {
            DrawLine(static_cast<int>(content.x), static_cast<int>(valueY[row] - 9),
                     static_cast<int>(content.x + content.width),
                     static_cast<int>(valueY[row] - 9), {215, 224, 218, 255});
            for (int column = 0; column < 2; ++column) {
                DrawTextEx(font, labels[row][column],
                           {columnX[column], valueY[row]}, kUiBodyFontSize, 0, GRAY);
                DrawTextEx(font, values[row][column].c_str(),
                           {columnX[column], valueY[row] + 18}, 18, 0,
                           {35, 60, 53, 255});
            }
        }
        const std::string moneyLines =
            "供养人口 " + FormatChineseNumber(snapshot.dependentPopulation) +
            "  货币供应 " + FormatChineseNumber(snapshot.totalMoneySupply.toDouble());
        DrawTextEx(font, moneyLines.c_str(),
                   {content.x, content.y + 207.0f}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string investmentLines =
            "投资池 " + FormatChineseNumber(snapshot.investmentPool.toDouble()) +
            "  债务 " + FormatChineseNumber(snapshot.totalDebt.toDouble());
        DrawTextEx(font, investmentLines.c_str(),
                   {content.x, content.y + 232.0f}, kUiBodyFontSize, 0, DARKGRAY);
        const float chartY = content.y + 270.0f;
        const float chartHeight = std::max(48.0f,
            std::min(100.0f, content.height - 286.0f));
        DrawScalarCurve(snapshot.gdpHistory, content.x, chartY,
                        content.width * 0.48f, chartHeight,
                        {71, 119, 184, 255}, font, "国内生产总值历史");
        DrawScalarCurveDouble(snapshot.populationHistory,
                              content.x + content.width * 0.52f, chartY,
                              content.width * 0.48f, chartHeight,
                              {62, 155, 87, 255}, font, "人口历史");
    } else if (state->provinceTab == 1) {
        DrawTextEx(font, "建筑", {content.x, content.y + 1},
                   kUiHeadingFontSize, 0, ink);
        DrawTextEx(font, "当前 / 排队", {content.x, content.y + 28},
                   kUiBodyFontSize, 0, DARKGRAY);
        const ProvinceBuildingGridLayout grid =
            ComputeProvinceBuildingGridLayout(content.width, content.height,
                                              static_cast<int>(snapshot.buildings.size()));
        const int columns = grid.columns;
        const float gap = grid.gap;
        const float cardWidth = grid.cardSize;
        const float cardHeight = cardWidth;
        const float listTop = content.y + grid.listTopOffset;
        const int visibleRows = grid.visibleRows;
        const int maxScroll = grid.maxScroll;
        const int scroll = std::clamp(state->provincePageScroll[1], 0, maxScroll);
        for (int row = 0; row < visibleRows; ++row) {
            for (int column = 0; column < columns; ++column) {
                const int index = (scroll + row) * columns + column;
                if (index >= static_cast<int>(snapshot.buildings.size())) continue;
                const BuildingSnapshot& building =
                    snapshot.buildings[static_cast<std::size_t>(index)];
                const Rectangle card = {content.x + column * (cardWidth + gap),
                    listTop + row * (cardHeight + gap), cardWidth, cardHeight};
                DrawRectangleRec(card, {250, 251, 248, 255});
                DrawRectangleLinesEx(card, 1.0f, {184, 198, 190, 255});
                const int type = building.typeIndex;
                const Rectangle icon = {card.x + 8.0f, card.y + 8.0f, 42.0f, 42.0f};
                const int outputGood = type >= 0 && type < TYPE_COUNT
                    ? buildingTemplates[static_cast<std::size_t>(type)].outputGood
                    : -1;
                building_icons::DrawBuildingProductionIcon(
                    icon, type, outputGood, building.operational);
                const char* label = type >= 0 && type < TYPE_COUNT
                    ? buildingTypeNames[type].c_str() : "建筑";
                DrawTextEx(font, label, {card.x + 58.0f, card.y + 10.0f},
                           kUiBodyFontSize, 0, {35, 48, 47, 255});
                const std::string buildingCount =
                    FormatChineseNumber(building.count, 2) + " / " +
                    FormatChineseNumber(building.pending, 2);
                DrawTextEx(font, buildingCount.c_str(),
                           {card.x + 58.0f, card.y + 32.0f}, kUiBodyFontSize, 0, DARKGRAY);
                const std::string employmentText =
                    "就业 " + FormatChineseNumber(building.employment);
                DrawTextEx(font, employmentText.c_str(),
                           {card.x + 8.0f, card.y + 78.0f}, kUiBodyFontSize, 0, DARKGRAY);
                DrawTextEx(font, TextFormat("%.0f%%", building.utilization * 100.0),
                           {card.x + 8.0f, card.y + 57.0f}, kUiBodyFontSize, 0, DARKGRAY);
                const std::string outputText =
                    "产出 " + FormatChineseNumber(building.output);
                DrawTextEx(font, outputText.c_str(),
                           {card.x + 8.0f, card.y + 96.0f}, kUiBodyFontSize, 0, DARKGRAY);
                const Rectangle minus = {card.x + card.width - 66.0f,
                                          card.y + card.height - 34.0f, 26.0f, 26.0f};
                const Rectangle plus = {card.x + card.width - 34.0f,
                                        card.y + card.height - 34.0f, 26.0f, 26.0f};
                DrawRectangleRec(minus, building.pending > 0
                    ? Color{190, 113, 93, 255} : Color{219, 224, 220, 255});
                DrawRectangleRec(plus, {61, 126, 91, 255});
                DrawTextEx(font, "-", {minus.x + 8.0f, minus.y + 3.0f}, 18, 0,
                           building.pending > 0 ? RAYWHITE : GRAY);
                DrawTextEx(font, "+", {plus.x + 6.0f, plus.y + 3.0f}, 18, 0, RAYWHITE);
            }
        }
        const Rectangle national = {content.x, content.y + content.height - 34.0f,
                                    content.width, 28.0f};
        DrawRectangleRec(national, {61, 126, 91, 245});
        DrawTextEx(font, "打开国家建设",
                   {national.x + 10.0f, national.y + 4.0f},
                   kUiBodyFontSize, 0, RAYWHITE);
    } else {
        DrawTextEx(font, "阶层", {content.x, content.y + 28}, kUiBodyFontSize, 0, GRAY);
        const float colPop = content.x + content.width * 0.30f;
        const float colEmployment = content.x + content.width * 0.52f;
        const float colIncome = content.x + content.width * 0.70f;
        const float colDemand = content.x + content.width * 0.87f;
        DrawTextEx(font, "人口", {colPop, content.y + 28}, kUiBodyFontSize, 0, GRAY);
        DrawTextEx(font, "就业/失业", {colEmployment, content.y + 28}, kUiBodyFontSize, 0, GRAY);
        DrawTextEx(font, "收入", {colIncome, content.y + 28}, kUiBodyFontSize, 0, GRAY);
        DrawTextEx(font, "需求", {colDemand, content.y + 28}, kUiBodyFontSize, 0, GRAY);
        const std::string dependentLine =
            "供养人口 " + FormatChineseNumber(snapshot.dependentPopulation);
        DrawTextEx(font, dependentLine.c_str(),
                   {content.x, content.y + 52.0f}, kUiBodyFontSize, 0, DARKGRAY);
        const Vector2 pieCenter = {content.x + content.width - 48.0f,
                                   content.y + 60.0f};
        const double total = std::max(1.0, snapshot.population);
        double angle = -90.0;
        for (std::size_t index = 0; index < snapshot.populationClasses.size(); ++index) {
            const double sweep = snapshot.populationClasses[index].population / total * 360.0;
            DrawCircleSector(pieCenter, 28.0f, static_cast<float>(angle),
                             static_cast<float>(angle + sweep), 24,
                             index % 2 == 0 ? Color{142, 190, 111, 255} : Color{229, 138, 50, 255});
            angle += sweep;
        }
        const double dependentSweep = snapshot.dependentPopulation / total * 360.0;
        DrawCircleSector(pieCenter, 28.0f, static_cast<float>(angle),
                         static_cast<float>(angle + dependentSweep), 24,
                         {205, 224, 224, 255});
        DrawCircleLines(static_cast<int>(pieCenter.x), static_cast<int>(pieCenter.y),
                        28.0f, {45, 62, 58, 255});
        const float rowHeight = 38.0f;
        const float listTop = content.y + 80.0f;
        const int visibleRows = std::max(1, static_cast<int>(
            (content.height - 42.0f) / rowHeight));
        const int maxScroll = std::max(
            0, static_cast<int>(snapshot.populationClasses.size()) - visibleRows);
        const int scroll = std::clamp(state->provincePageScroll[2], 0, maxScroll);
        for (int row = 0; row < visibleRows; ++row) {
            const int index = scroll + row;
            if (index >= static_cast<int>(snapshot.populationClasses.size())) break;
            const PopulationClassSnapshot& population =
                snapshot.populationClasses[static_cast<std::size_t>(index)];
            const float y = listTop + row * rowHeight;
            DrawRectangle(content.x, y - 3.0f, content.width, rowHeight - 3.0f,
                          row % 2 == 0 ? Color{250, 251, 248, 255}
                                       : Color{235, 241, 236, 255});
            const char* label = population.classIndex == LABORER
                ? "劳工" : population.classIndex == ENGINEER
                    ? "工程师" : "资本家";
            DrawTextEx(font, label, {content.x + 3.0f, y + 3.0f},
                       kUiBodyFontSize, 0,
                       {35, 48, 47, 255});
            const std::string classPopulation =
                FormatChineseNumber(population.population);
            const std::string classEmployment =
                FormatChineseNumber(population.employed) + "/" +
                FormatChineseNumber(population.unemployed);
            const std::string classIncome =
                FormatChineseNumber(population.income.toDouble());
            DrawTextEx(font, classPopulation.c_str(),
                       {colPop, y + 3}, kUiBodyFontSize, 0, DARKGRAY);
            DrawTextEx(font, classEmployment.c_str(),
                       {colEmployment, y + 3}, kUiBodyFontSize, 0, DARKGRAY);
            DrawTextEx(font, classIncome.c_str(),
                       {colIncome, y + 3}, kUiBodyFontSize, 0, DARKGRAY);
            DrawTextEx(font, TextFormat("%.0f%%",
                                       population.demandSatisfaction * 100.0),
                       {colDemand, y + 3}, kUiBodyFontSize, 0, DARKGRAY);
        }
    }
    EndScissorMode();
    DrawRectangleLinesEx(panel, 1.0f, {45, 62, 58, 255});
}
void DrawUI(UIState* state, World& world, Font font,
            double elapsedSeconds) {
    // The map is the permanent base view. Detail desks are drawn as overlays
    // so the rest of the map remains visible and interactive.
    DrawWorldMapUI(state, world, font, elapsedSeconds);
    if (state->view == UIView::CommodityMarket) {
        DrawCommodityMarketUI(state, world, font);
    } else if (state->view == UIView::TransportLogistics) {
        DrawTransportUI(state, world, font);
    } else if (state->view == UIView::CountryOverview ||
               state->view == UIView::NationalMarket) {
        DrawCountryOverviewUI(state, world, font, elapsedSeconds);
    } else if (state->view == UIView::ProvinceDetail) {
        DrawEmbeddedLocalMarketUI(&state->localMarketUI, world, font,
                                  state->selectedProvinceId,
                                  CurrentUILayout().provincePanel,
                                  elapsedSeconds);
    }
    DrawConstructionListUI(state, world, font);
}
