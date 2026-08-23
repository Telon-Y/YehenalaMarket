#include "debug_ui_internal.h"

#include "building_template.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace debug_ui {
std::string BuildingInputSummary(const LocalMarket& market, int type) {
    const BuildingTemplate& building = market.getBuildingTemplates()[type];
    Money largestBacklog = Money(0);
    int blockedGood = -1;
    int inputCount = 0;
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (building.inputs[good] <= 0.0) continue;
        ++inputCount;
        const InventoryState& input =
            market.getWarehouse().buildingInput(type, good);
        if (input.backlog > largestBacklog) {
            largestBacklog = input.backlog;
            blockedGood = good;
        }
    }
    if (inputCount == 0) return "无中间投入";
    if (blockedGood >= 0 && largestBacklog > Money(1e-7))
        return commodityNames[blockedGood] + " 缺 " +
               NumberText(largestBacklog);
    return "输入就绪";
}

void DrawBuildingsPanel(DebugUIState* state, World& world, Font font,
                        const DebugLayout& layout) {
    const LocalMarket& market = world.getMarket(state->selectedMarket);
    const WarehouseNetwork& network = world.getWarehouseNetwork();
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;

    DrawTextAt(font, "建筑与生产输入", x, y,
               kDebugPageTitleFontSize, kText);
    DrawTextAt(font, market.getMarketName(), x + 220.0f, y + 7.0f,
               kDebugBodyFontSize, kMuted);
    const bool editable = CanEditBuilding(state->selectedBuilding);
    DrawButton(font, BuildingAddButton(layout), "+1",
               false, editable ? kGreen : kMuted);
    DrawButton(font, BuildingRemoveButton(layout), "-1",
               false, editable ? kRed : kMuted);

    const float headerY = y + 38.0f;
    const float headerHeight = 28.0f;
    const std::array<float, 12> fractions = {
        0.00f, 0.15f, 0.21f, 0.29f, 0.38f, 0.47f,
        0.56f, 0.65f, 0.74f, 0.82f, 0.90f, 1.00f
    };
    const char* headers[11] = {
        "建筑", "数量", "在岗率", "招聘满足", "原料满足",
        "产能利用", "资金满足", "期末待产", "周产出", "现金",
        "输入状态"
    };
    DrawRectangle(static_cast<int>(x), static_cast<int>(headerY),
                   static_cast<int>(width), static_cast<int>(headerHeight),
                   Color{235, 238, 235, 255});
    for (int column = 0; column < 11; ++column) {
        DrawFittedText(font, headers[column],
                       {x + width * fractions[column] + 5.0f, headerY,
                        width * (fractions[column + 1] -
                                  fractions[column]) - 8.0f, headerHeight},
                        kDebugTableFontSize, kMuted);
    }

    for (int type = 0; type < TYPE_COUNT; ++type) {
        const Rectangle row = BuildingRow(layout, type);
        const bool selected = type == state->selectedBuilding;
        const int count = market.getBuildingCounts()[type];
        const BuildingTemplate& building =
            market.getBuildingTemplates()[type];
        const Money pending = building.outputGood < 0
            ? Money(0)
            : network.pendingProduction(market.getMarketId(), type,
                                        building.outputGood);
        const double supply = market.getCurrentSupplyRatio()[type];
        const double capacityUtilization =
            market.getCapacityUtilization()[type];
        const double fundingAvailability =
            market.getFundingAvailability()[type];
        const double targetEmployment =
            market.getTargetEmployment()[type];
        const double recruitmentSatisfaction =
            targetEmployment > 1.0e-9
                ? std::clamp(
                      market.getActualEmployment()[type] /
                          targetEmployment,
                      0.0, 1.0)
                : 1.0;
        DrawRectangleRec(row, selected ? kBlueSoft :
                         (type % 2 == 0 ? kSurface : kBackground));
        if (selected) {
            DrawRectangle(static_cast<int>(row.x),
                          static_cast<int>(row.y), 3,
                          static_cast<int>(row.height), kBlue);
        }
        const std::array<std::string, 11> cells = {
            buildingTypeNames[type],
            std::to_string(count),
            PercentText(market.getActualEmploymentRate()[type]),
            PercentText(recruitmentSatisfaction),
            PercentText(supply),
            PercentText(capacityUtilization),
            PercentText(fundingAvailability),
            NumberText(pending),
            NumberText(market.getLatestBuildingOutput()[type]),
            NumberText(market.getCashPools()[type]),
            BuildingInputSummary(market, type)
        };
        for (int column = 0; column < 11; ++column) {
            Color color = kText;
            if (count == 0 && column > 0) color = kMuted;
            if (column == 4 && count > 0 && supply < 0.999)
                color = kRed;
            if (column == 10 &&
                cells[column].find("缺") != std::string::npos)
                color = kOrange;
            DrawFittedText(font, cells[column],
                           {x + width * fractions[column] + 5.0f, row.y,
                            width * (fractions[column + 1] -
                                     fractions[column]) - 8.0f,
                            row.height},
                            kDebugTableFontSize, color);
        }
        DrawLineEx({row.x, row.y + row.height},
                   {row.x + row.width, row.y + row.height},
                   1.0f, kBorder);
    }

    const int type = state->selectedBuilding;
    const BuildingTemplate& building = market.getBuildingTemplates()[type];
    const float detailY =
        y + 66.0f + TYPE_COUNT * 25.0f + 12.0f;
    const Money pending = building.outputGood < 0
        ? Money(0)
        : network.pendingProduction(market.getMarketId(), type,
                                    building.outputGood);
    const std::string outputText = building.outputGood >= 0
        ? commodityNames[building.outputGood] : "无商品产出";
    DrawSectionTitle(font, buildingTypeNames[type] + " 输入缓冲",
                     x, detailY, width,
                     "产出 " + outputText + "  |  计划 " +
                     NumberText(market.getProductionTarget()[type]) +
                     "  期末待产 " + NumberText(pending));

    std::vector<int> inputGoods;
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (building.inputs[good] > 0.0) inputGoods.push_back(good);
    }
    if (inputGoods.empty()) {
        DrawTextAt(font, "该建筑没有中间商品投入。订单需求可直接转为生产。",
                   x, detailY + 42.0f, kDebugBodyFontSize, kMuted);
        return;
    }
    const float gap = 12.0f;
    const float inputWidth =
        (width - gap * (inputGoods.size() - 1)) /
        static_cast<float>(inputGoods.size());
    for (int index = 0; index < static_cast<int>(inputGoods.size()); ++index) {
        const int good = inputGoods[index];
        const InventoryState& input =
            market.getWarehouse().buildingInput(type, good);
        const Rectangle bounds = {
            x + index * (inputWidth + gap), detailY + 39.0f,
            inputWidth,
            std::max(58.0f, layout.height - (detailY + 39.0f) - 15.0f)
        };
        DrawRectangleRec(bounds, kSurface);
        DrawRectangleLinesEx(bounds, 1.0f,
                             input.backlog > Money(1e-7) ? kOrange : kBorder);
        DrawTextAt(font, commodityNames[good], bounds.x + 10.0f,
                   bounds.y + 8.0f, kDebugBodyFontSize, kText);
        const std::string detail =
            "现存 " + NumberText(input.onHand) +
            "  目标 " + NumberText(input.policy.targetStock) +
            "  补货点 " + NumberText(input.policy.reorderPoint) +
            "  库存位 " + NumberText(input.position()) +
            "  已确认补货 " + NumberText(input.confirmedInbound) +
            "  运输中 " + NumberText(input.physicalInTransit) +
            "  缺口 " + NumberText(input.backlog);
        DrawFittedText(font, detail,
                       {bounds.x + 10.0f, bounds.y + 29.0f,
                        bounds.width - 20.0f, 24.0f},
                       kDebugTableFontSize,
                       input.backlog > Money(1e-7) ? kOrange : kMuted);
    }
}


}  // namespace debug_ui
