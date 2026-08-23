#include "debug_ui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace debug_ui {
DebugLayout MakeLayout() {
    return MakeLayout(
        DebugUIMode::FullScreen,
        {0.0f, 0.0f, static_cast<float>(GetScreenWidth()),
         static_cast<float>(GetScreenHeight())});
}

DebugLayout MakeLayout(DebugUIMode mode, Rectangle bounds) {
    DebugLayout layout;
    layout.mode = mode;
    layout.bounds = bounds;
    layout.width = std::max(1.0f, bounds.width);
    layout.height = std::max(1.0f, bounds.height);
    layout.originX = bounds.x;
    layout.originY = bounds.y;
    layout.compact = mode == DebugUIMode::EmbeddedLocalMarket &&
                     layout.width < 560.0f;

    const float inset = std::min(14.0f, std::max(8.0f, layout.width * 0.035f));
    if (mode == DebugUIMode::EmbeddedLocalMarket) {
        layout.headerHeight = 58.0f;
        layout.marketHeight = layout.compact ? 46.0f : 0.0f;
        // Match UILayout.localBackButton so drawing and province input share
        // exactly the same target.
        layout.backButton = {
            layout.originX + std::max(inset, layout.width - inset - 78.0f),
            layout.originY + 14.0f,
            std::min(78.0f, std::max(1.0f, layout.width - inset * 2.0f)),
            30.0f};

        if (layout.compact) {
            layout.sidebarWidth = 0.0f;
            layout.goodColumns = 6;
            const float gap = 4.0f;
            layout.goodButtonWidth =
                std::max(1.0f, (layout.width - inset * 2.0f -
                                gap * (layout.goodColumns - 1)) /
                                   static_cast<float>(layout.goodColumns));
            layout.goodButtonHeight = 24.0f;
            layout.goodButtonStride = 26.0f;
            layout.goodListX = layout.originX + inset;
            layout.goodListY = layout.originY + layout.headerHeight +
                               layout.marketHeight + 5.0f;
            layout.contentX = layout.originX + inset;
            layout.contentY = layout.goodListY +
                              layout.goodButtonStride * 2.0f + 8.0f;
            layout.contentWidth =
                std::max(100.0f, layout.width - inset * 2.0f);
        } else {
            layout.sidebarWidth =
                std::min(182.0f, std::max(148.0f, layout.width * 0.31f));
            layout.goodColumns = 1;
            layout.goodButtonWidth =
                std::max(1.0f, layout.sidebarWidth - inset * 2.0f);
            layout.goodButtonHeight = 24.0f;
            layout.goodButtonStride = 25.0f;
            layout.contentX = layout.originX + layout.sidebarWidth + inset;
            layout.contentY = layout.originY + layout.headerHeight + inset;
            layout.contentWidth = std::max(
                100.0f, layout.width - layout.sidebarWidth - inset * 2.0f);
            layout.goodListX = layout.originX + inset;
            layout.goodListY = layout.originY + layout.headerHeight +
                               205.0f;
        }
        layout.contentHeight = std::max(
            100.0f, layout.originY + layout.height - layout.contentY - inset);
        for (int panel = 0; panel < 4; ++panel) {
            if (layout.compact) {
                const float gap = 4.0f;
                const float buttonWidth =
                    (layout.width - inset * 2.0f - gap * 3.0f) / 4.0f;
                layout.panelButtons[panel] = {
                    layout.originX + inset + panel * (buttonWidth + gap),
                    layout.originY + layout.headerHeight + 6.0f,
                    buttonWidth, 34.0f};
            } else {
                layout.panelButtons[panel] = {
                    layout.originX + inset,
                    layout.originY + layout.headerHeight + 12.0f +
                        panel * 43.0f,
                    layout.sidebarWidth - inset * 2.0f, 38.0f};
            }
        }
        return layout;
    }

    layout.sidebarWidth = layout.width < 1200.0f ? 190.0f : 210.0f;
    layout.contentX = layout.originX + layout.sidebarWidth + 16.0f;
    layout.contentY = layout.originY + layout.headerHeight +
                      layout.marketHeight + 14.0f;
    layout.contentWidth = std::max(
        100.0f, layout.width - layout.contentX + layout.originX - 16.0f);
    layout.contentHeight = std::max(
        100.0f, layout.height - layout.contentY + layout.originY - 14.0f);
    layout.goodListX = layout.originX + 12.0f;
    layout.goodListY = layout.originY + 306.0f;
    layout.goodButtonWidth = layout.sidebarWidth - 24.0f;
    layout.goodColumns = 1;
    const float controlsX = layout.originX + layout.width - 316.0f;
    layout.pauseButton = {controlsX, layout.originY + 10.0f, 68.0f, 36.0f};
    layout.stepButton = {controlsX + 74.0f, layout.originY + 10.0f,
                         58.0f, 36.0f};
    layout.speedButtons[0] = {controlsX + 138.0f, layout.originY + 10.0f,
                              52.0f, 36.0f};
    layout.speedButtons[1] = {controlsX + 196.0f, layout.originY + 10.0f,
                              52.0f, 36.0f};
    layout.speedButtons[2] = {controlsX + 254.0f, layout.originY + 10.0f,
                              52.0f, 36.0f};
    for (int panel = 0; panel < 4; ++panel) {
        layout.panelButtons[panel] = {
            layout.originX + 12.0f,
            layout.originY + 116.0f + panel * 43.0f,
            layout.sidebarWidth - 24.0f, 38.0f};
    }
    return layout;
}
Rectangle MarketButton(const DebugLayout& layout, int index, int count) {
    const float width = (layout.width - layout.sidebarWidth) /
                        static_cast<float>(std::max(1, count));
    return {
        layout.originX + layout.sidebarWidth + width * index,
        layout.originY + layout.headerHeight,
        width,
        layout.marketHeight
    };
}

Rectangle GoodButton(const DebugLayout& layout, int good) {
    const int columns = std::max(1, layout.goodColumns);
    const int row = good / columns;
    const int column = good % columns;
    const float gap = columns > 1 ? 4.0f : 0.0f;
    return {
        layout.goodListX + column * (layout.goodButtonWidth + gap),
        layout.goodListY + row * layout.goodButtonStride,
        layout.goodButtonWidth,
        layout.goodButtonHeight
    };
}

Rectangle BuildingRow(const DebugLayout& layout, int type) {
    return {layout.contentX, layout.contentY + 66.0f + type * 25.0f,
            layout.contentWidth, 24.0f};
}

Rectangle BuildingAddButton(const DebugLayout& layout) {
    return {layout.contentX + layout.contentWidth - 118.0f,
             layout.contentY + 4.0f, 52.0f, 34.0f};
}

Rectangle BuildingRemoveButton(const DebugLayout& layout) {
    return {layout.contentX + layout.contentWidth - 60.0f,
             layout.contentY + 4.0f, 52.0f, 34.0f};
}

Rectangle ConstructionDepartmentButton(const DebugLayout& layout) {
    return {layout.contentX + layout.contentWidth - 154.0f,
             layout.contentY + 4.0f, 146.0f, 34.0f};
}

void DrawTextAt(Font font, const std::string& text, float x, float y,
                float size, Color color) {
    DrawTextEx(font, text.c_str(), {x, y}, size, 0.0f, color);
}

void DrawFittedText(Font font, const std::string& text, Rectangle bounds,
                    float size, Color color, float padding) {
    const float available = std::max(1.0f, bounds.width - padding * 2.0f);
    float fitted = size;
    while (fitted > kDebugMinimumFontSize &&
           MeasureTextEx(font, text.c_str(), fitted, 0.0f).x > available) {
        fitted -= 1.0f;
    }
    BeginScissorMode(static_cast<int>(bounds.x),
                     static_cast<int>(bounds.y),
                     std::max(1, static_cast<int>(bounds.width)),
                     std::max(1, static_cast<int>(bounds.height)));
    DrawTextAt(font, text, bounds.x + padding,
               bounds.y + std::max(0.0f, (bounds.height - fitted) * 0.5f),
               fitted, color);
    EndScissorMode();
}

bool DrawButton(Font font, Rectangle bounds, const std::string& label,
                bool active, Color accent) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    Color fill = active ? accent : kSurface;
    if (hovered && !active) fill = Color{238, 241, 239, 255};
    DrawRectangleRec(bounds, fill);
    DrawRectangleLinesEx(bounds, 1.0f, active ? accent : kBorder);
    DrawFittedText(font, label, bounds, 17.0f,
                   active ? WHITE : kText, 7.0f);
    return hovered;
}

std::string NumberText(double value, int precision) {
    if (!std::isfinite(value)) return "NaN";
    const double magnitude = std::fabs(value);
    char buffer[64];
    if (magnitude >= 100000000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.*f亿", precision,
                      value / 100000000.0);
    } else if (magnitude >= 10000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.*f万", precision,
                      value / 10000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
    }
    return buffer;
}

std::string NumberText(Money value, int precision) {
    return NumberText(value.toDouble(), precision);
}

std::string PercentText(double value) {
    if (!std::isfinite(value)) return "NaN";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.1f%%", value * 100.0);
    return buffer;
}

std::string MarketCode(int marketId) {
    return marketId >= 0 && marketId < 26
        ? std::string("M") + static_cast<char>('A' + marketId)
        : std::string("M") + std::to_string(marketId);
}

const char* OrderKindText(WarehouseOrderKind kind) {
    switch (kind) {
    case WarehouseOrderKind::BuildingMaterialDemand: return "建筑需求";
    case WarehouseOrderKind::WarehouseReplenishment: return "仓库补货";
    case WarehouseOrderKind::RemotePurchase: return "远程采购";
    case WarehouseOrderKind::SupplierProduction: return "定向生产";
    }
    return "未知";
}

const char* OrderStatusText(WarehouseOrderStatus status) {
    switch (status) {
    case WarehouseOrderStatus::PendingLocalAllocation: return "待本地分配";
    case WarehouseOrderStatus::WaitingForRoute: return "待寻路";
    case WarehouseOrderStatus::AwaitingSupply: return "待供应";
    case WarehouseOrderStatus::Confirmed: return "已确认";
    case WarehouseOrderStatus::PartiallyConfirmed: return "部分确认";
    case WarehouseOrderStatus::InTransit: return "运输中";
    case WarehouseOrderStatus::PartiallyFulfilled: return "部分收货";
    case WarehouseOrderStatus::Fulfilled: return "已完成";
    case WarehouseOrderStatus::Cancelled: return "已取消";
    }
    return "未知";
}

Color OrderStatusColor(WarehouseOrderStatus status) {
    switch (status) {
    case WarehouseOrderStatus::AwaitingSupply:
    case WarehouseOrderStatus::WaitingForRoute:
        return kOrange;
    case WarehouseOrderStatus::InTransit:
    case WarehouseOrderStatus::PartiallyFulfilled:
        return kBlue;
    case WarehouseOrderStatus::Fulfilled:
        return kGreen;
    case WarehouseOrderStatus::Cancelled:
        return kRed;
    default:
        return kText;
    }
}

bool CanEditBuilding(int type) {
    return type >= 0 && type < TYPE_COUNT &&
           type != BANK && type != FINANCE &&
           type != INDUSTRIAL_BANK && type != SAVINGS_BANK &&
           type != CONST_DEPT;
}

void DrawSectionTitle(Font font, const std::string& title, float x, float y,
                      float width, const std::string& trailing) {
    DrawTextAt(font, title, x, y, kDebugSectionFontSize, kText);
    if (!trailing.empty()) {
        const Vector2 size = MeasureTextEx(
            font, trailing.c_str(), kDebugBodyFontSize, 0.0f);
        DrawTextAt(font, trailing, x + width - size.x, y + 3.0f,
                   kDebugBodyFontSize, kMuted);
    }
    DrawLineEx({x, y + 30.0f}, {x + width, y + 30.0f}, 1.0f, kBorder);
}

void DrawMetric(Font font, Rectangle bounds, const std::string& label,
                const std::string& value, Color accent) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangle(static_cast<int>(bounds.x), static_cast<int>(bounds.y),
                  3, static_cast<int>(bounds.height), accent);
    DrawTextAt(font, label, bounds.x + 12.0f, bounds.y + 9.0f,
               kDebugTableFontSize, kMuted);
    DrawFittedText(font, value,
                   {bounds.x + 12.0f, bounds.y + 27.0f,
                    bounds.width - 18.0f, bounds.height - 29.0f},
                   22.0f, kText);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
}

void DrawMoneySeries(Font font, const std::vector<Money>& values,
                     Rectangle bounds, Color color,
                     const std::string& label) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
    DrawTextAt(font, label, bounds.x + 10.0f, bounds.y + 8.0f,
               kDebugBodyFontSize, kText);
    if (values.empty()) {
        DrawTextAt(font, "暂无数据", bounds.x + 10.0f, bounds.y + 38.0f,
                   kDebugBodyFontSize, kMuted);
        return;
    }
    const int first = std::max(0, static_cast<int>(values.size()) - 260);
    double minimum = values[first].toDouble();
    double maximum = minimum;
    for (int index = first; index < static_cast<int>(values.size()); ++index) {
        const double value = values[index].toDouble();
        if (!std::isfinite(value)) continue;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    if (std::fabs(maximum - minimum) < 1e-9) {
        maximum += 1.0;
        minimum -= 1.0;
    }
    const Rectangle plot = {bounds.x + 10.0f, bounds.y + 34.0f,
                            bounds.width - 20.0f, bounds.height - 52.0f};
    DrawLineEx({plot.x, plot.y + plot.height},
               {plot.x + plot.width, plot.y + plot.height}, 1.0f, kBorder);
    const int count = static_cast<int>(values.size()) - first;
    for (int offset = 1; offset < count; ++offset) {
        const double previous = values[first + offset - 1].toDouble();
        const double current = values[first + offset].toDouble();
        if (!std::isfinite(previous) || !std::isfinite(current)) continue;
        const float x0 = plot.x + plot.width * (offset - 1) /
                                      static_cast<float>(std::max(1, count - 1));
        const float x1 = plot.x + plot.width * offset /
                                      static_cast<float>(std::max(1, count - 1));
        const float y0 = plot.y + plot.height *
            static_cast<float>((maximum - previous) / (maximum - minimum));
        const float y1 = plot.y + plot.height *
            static_cast<float>((maximum - current) / (maximum - minimum));
        DrawLineEx({x0, y0}, {x1, y1}, 1.5f, color);
    }
    DrawTextAt(font, NumberText(maximum), plot.x, plot.y - 2.0f,
               kDebugCaptionFontSize, kMuted);
    DrawTextAt(font, NumberText(minimum), plot.x,
               plot.y + plot.height - 15.0f,
               kDebugCaptionFontSize, kMuted);
}

void DrawDoubleSeries(Font font, const std::vector<double>& values,
                      Rectangle bounds, Color color,
                      const std::string& label) {
    std::vector<Money> converted;
    const int first = std::max(0, static_cast<int>(values.size()) - 260);
    converted.reserve(values.size() - static_cast<std::size_t>(first));
    for (int index = first; index < static_cast<int>(values.size()); ++index)
        converted.emplace_back(values[index]);
    DrawMoneySeries(font, converted, bounds, color, label);
}

void RefreshAudit(DebugUIState* state, const World& world) {
    ++state->frameCounter;
    if (state->frameCounter != 1 && state->frameCounter % 30 != 0) return;
    state->cachedAudit = world.getWarehouseNetwork().audit();
    state->cachedInventoryBalanced = true;
    for (int index = 0; index < world.getMarketCount(); ++index) {
        state->cachedInventoryBalanced =
            state->cachedInventoryBalanced &&
            world.getMarket(index).getLatestFlow().inventoryBalanced;
    }
}

std::vector<int> MarketIndicesForLayout(const DebugUIState* state,
                                        const World& world,
                                        const DebugLayout& layout) {
    std::vector<int> indices;
    if (state == nullptr || world.getMarketCount() <= 0) return indices;

    const int selected = std::clamp(state->selectedMarket, 0,
                                    world.getMarketCount() - 1);
    const LocalMarket& selectedMarket = world.getMarket(selected);
    const Country* fiscalCountry = selectedMarket.getFiscalCountry();
    for (int index = 0; index < world.getMarketCount(); ++index) {
        const LocalMarket& candidate = world.getMarket(index);
        if (layout.mode == DebugUIMode::EmbeddedLocalMarket &&
            fiscalCountry != nullptr) {
            const Country* candidateCountry = candidate.getFiscalCountry();
            if (candidateCountry == nullptr ||
                candidateCountry->getId() != fiscalCountry->getId()) {
                continue;
            }
        }
        indices.push_back(index);
    }
    if (indices.empty()) indices.push_back(selected);
    return indices;
}

void DrawShell(DebugUIState* state, World& world, Font font,
               const DebugLayout& layout, double elapsedSeconds,
               const TransportationSnapshot& transport) {
    const LocalMarket& market = world.getMarket(state->selectedMarket);
    if (layout.mode == DebugUIMode::EmbeddedLocalMarket) {
        const float ox = layout.originX;
        const float oy = layout.originY;
        DrawRectangleRec(layout.bounds, kBackground);
        DrawRectangle(static_cast<int>(ox), static_cast<int>(oy),
                      static_cast<int>(layout.width),
                      static_cast<int>(layout.headerHeight), kSurface);
        DrawLine(static_cast<int>(ox),
                 static_cast<int>(oy + layout.headerHeight - 1.0f),
                 static_cast<int>(ox + layout.width),
                 static_cast<int>(oy + layout.headerHeight - 1.0f),
                 kBorder);
        DrawFittedText(font, "本地市场  " + market.getMarketName(),
                       {ox + 14.0f, oy + 8.0f,
                        std::max(1.0f, layout.width - 116.0f), 28.0f},
                       22.0f, kText, 0.0f);
        DrawFittedText(font,
                       "周期 " + std::to_string(market.getStepCount()) +
                           "  GDP " + NumberText(market.getGDP()),
                       {ox + 14.0f, oy + 36.0f,
                        std::max(1.0f, layout.width - 116.0f), 17.0f},
                       kDebugCaptionFontSize, kMuted, 0.0f);
        DrawButton(font, layout.backButton, "地图", false, kBlue);
        if (!layout.compact) {
            DrawRectangle(static_cast<int>(ox),
                          static_cast<int>(oy + layout.headerHeight),
                          static_cast<int>(layout.sidebarWidth),
                          static_cast<int>(layout.height - layout.headerHeight),
                          kSidebar);
        }
        const char* panelNames[4] = {
            "商品与流量", "建筑与输入", "建造队列", "宏观与物流"
        };
        for (int panel = 0; panel < 4; ++panel) {
            const Rectangle button = layout.panelButtons[panel];
            const bool selected = panel == state->currentPanel;
            const bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
            DrawRectangleRec(button, selected ? kGreen :
                             (hovered ? kSidebarHover : kSidebar));
            if (selected) {
                if (layout.compact) {
                    DrawRectangle(static_cast<int>(button.x),
                                  static_cast<int>(button.y + button.height - 3.0f),
                                  static_cast<int>(button.width), 3, kGold);
                } else {
                    DrawRectangle(static_cast<int>(button.x),
                                  static_cast<int>(button.y), 3,
                                  static_cast<int>(button.height), kGold);
                }
            }
            DrawFittedText(font, panelNames[panel], button, 16.0f,
                           WHITE, layout.compact ? 3.0f : 10.0f);
        }
        if (state->currentPanel == 0) {
            if (!layout.compact) {
                DrawTextAt(font, "商品", layout.goodListX,
                           layout.goodListY - 22.0f,
                           kDebugCaptionFontSize,
                           Color{170, 178, 179, 255});
            }
            for (int good = 0; good < NUM_GOODS; ++good) {
                const Rectangle button = GoodButton(layout, good);
                const bool selected = good == state->selectedGood;
                const bool hovered =
                    CheckCollisionPointRec(GetMousePosition(), button);
                DrawRectangleRec(button, selected ? kSidebarHover :
                                 (hovered ? Color{48, 53, 55, 255} : kSidebar));
                if (selected && !layout.compact)
                    DrawRectangle(static_cast<int>(button.x),
                                  static_cast<int>(button.y), 3,
                                  static_cast<int>(button.height), kOrange);
                DrawFittedText(font, commodityNames[good], button,
                               layout.compact ? kDebugMinimumFontSize
                                              : kDebugBodyFontSize,
                               selected ? WHITE :
                               Color{205, 211, 211, 255},
                               layout.compact ? 2.0f : 8.0f);
            }
        }
        const bool healthy =
            state->cachedAudit.valid && state->cachedInventoryBalanced;
        if (!layout.compact) {
            const float statusY =
                std::max(oy + layout.height - 76.0f, layout.goodListY +
                         NUM_GOODS * layout.goodButtonStride + 24.0f);
            DrawLineEx({ox + 12.0f, statusY - 10.0f},
                       {ox + layout.sidebarWidth - 12.0f, statusY - 10.0f},
                       1.0f, kSidebarHover);
            DrawCircle(static_cast<int>(ox + 20.0f),
                       static_cast<int>(statusY + 6.0f), 5.0f,
                       healthy ? kGreen : kRed);
            DrawFittedText(font, healthy ? "库存审计通过" : "库存审计异常",
                           {ox + 32.0f, statusY - 4.0f,
                            layout.sidebarWidth - 44.0f, 20.0f},
                           kDebugCaptionFontSize, WHITE);
        }
        return;
    }    DrawRectangle(0, 0, static_cast<int>(layout.width),
                  static_cast<int>(layout.height), kBackground);
    DrawRectangle(0, 0, static_cast<int>(layout.width),
                  static_cast<int>(layout.headerHeight), kSurface);
    DrawLine(0, static_cast<int>(layout.headerHeight - 1.0f),
             static_cast<int>(layout.width),
             static_cast<int>(layout.headerHeight - 1.0f), kBorder);

    DrawTextAt(font, "Yehenala 2.0", 16.0f, 7.0f, 22.0f, kText);
    DrawTextAt(font, "DEBUG", 16.0f, 34.0f,
               kDebugMinimumFontSize, kRed);
    DrawTextAt(font, "五市场供应链", 70.0f, 33.0f,
               kDebugCaptionFontSize, kMuted);
    DrawTextAt(font, "周期 " + std::to_string(market.getStepCount()),
               230.0f, 17.0f, 18.0f, kText);
    if (layout.width >= 1320.0f) {
        const double speed = elapsedSeconds > 0.0
            ? market.getStepCount() / elapsedSeconds : 0.0;
        DrawTextAt(font, "年化 GDP " + NumberText(market.getGDP()) +
                         "  |  " + NumberText(speed) + " 周/秒",
                   350.0f, 17.0f, kDebugBodyFontSize, kMuted);
    }

    DrawButton(font, layout.pauseButton, state->paused ? "继续" : "暂停",
               state->paused, kOrange);
    DrawButton(font, layout.stepButton, "单步", false, kBlue);
    const int speeds[3] = {1, 2, 5};
    for (int index = 0; index < 3; ++index) {
        DrawButton(font, layout.speedButtons[index],
                   std::to_string(speeds[index]) + "x",
                   !state->paused && state->simulationSpeed == speeds[index],
                   kGreen);
    }

    DrawRectangle(0, static_cast<int>(layout.headerHeight),
                  static_cast<int>(layout.sidebarWidth),
                  static_cast<int>(layout.marketHeight), kSidebar);
    for (int index = 0; index < world.getMarketCount(); ++index) {
        const Rectangle button =
            MarketButton(layout, index, world.getMarketCount());
        const bool selected = index == state->selectedMarket;
        DrawRectangleRec(button, selected ? kGreenSoft : kSurface);
        DrawLineEx({button.x, button.y + button.height - 1.0f},
                   {button.x + button.width, button.y + button.height - 1.0f},
                   selected ? 3.0f : 1.0f, selected ? kGreen : kBorder);
        const LocalMarket& tabMarket = world.getMarket(index);
        const std::string tab = tabMarket.getMarketName() + "  GDP " +
                                NumberText(tabMarket.getGDP());
        DrawFittedText(font, tab, button, kDebugBodyFontSize,
                       selected ? kGreen : kText, 10.0f);
    }

    DrawRectangle(0, static_cast<int>(layout.headerHeight +
                                      layout.marketHeight),
                  static_cast<int>(layout.sidebarWidth),
                  static_cast<int>(layout.height - layout.headerHeight -
                                   layout.marketHeight), kSidebar);
    const char* panelNames[4] = {
        "商品与流量", "建筑与输入", "建造队列", "宏观与物流"
    };
    for (int panel = 0; panel < 4; ++panel) {
        const Rectangle button = layout.panelButtons[panel];
        const bool selected = panel == state->currentPanel;
        const bool hovered = CheckCollisionPointRec(GetMousePosition(), button);
        DrawRectangleRec(button, selected ? kGreen :
                         (hovered ? kSidebarHover : kSidebar));
        if (selected)
            DrawRectangle(static_cast<int>(button.x),
                          static_cast<int>(button.y), 3,
                          static_cast<int>(button.height), kGold);
        DrawFittedText(font, panelNames[panel], button, 17.0f,
                       WHITE, 12.0f);
    }

    if (state->currentPanel == 0) {
        DrawTextAt(font, "商品", 14.0f, 287.0f,
                   kDebugCaptionFontSize,
                   Color{170, 178, 179, 255});
        for (int good = 0; good < NUM_GOODS; ++good) {
            const Rectangle button = GoodButton(layout, good);
            const bool selected = good == state->selectedGood;
            const bool hovered =
                CheckCollisionPointRec(GetMousePosition(), button);
            DrawRectangleRec(button, selected ? kSidebarHover :
                             (hovered ? Color{48, 53, 55, 255} : kSidebar));
            if (selected)
                DrawRectangle(static_cast<int>(button.x),
                              static_cast<int>(button.y), 3,
                              static_cast<int>(button.height), kOrange);
            DrawFittedText(font, commodityNames[good], button,
                           kDebugBodyFontSize,
                           selected ? WHITE :
                           Color{205, 211, 211, 255}, 10.0f);
        }
    }

    const float statusY = std::max(624.0f, layout.height - 96.0f);
    DrawLineEx({12.0f, statusY - 10.0f},
               {layout.sidebarWidth - 12.0f, statusY - 10.0f},
               1.0f, kSidebarHover);
    const bool healthy =
        state->cachedAudit.valid && state->cachedInventoryBalanced;
    DrawCircle(20, static_cast<int>(statusY + 6.0f), 5.0f,
               healthy ? kGreen : kRed);
    DrawTextAt(font, healthy ? "库存审计通过" : "库存审计异常",
               32.0f, statusY - 2.0f, kDebugCaptionFontSize, WHITE);
    DrawTextAt(font,
               std::to_string(transport.routes.size()) + " 路线  " +
               std::to_string(transport.orders.size()) + " 活动订单",
               14.0f, statusY + 23.0f, kDebugMinimumFontSize,
               Color{176, 184, 184, 255});
    DrawTextAt(font,
               std::to_string(transport.shipments.size()) + " 在途  托管 " +
               NumberText(transport.totalEscrow),
               14.0f, statusY + 43.0f, kDebugMinimumFontSize,
               Color{176, 184, 184, 255});
}


}  // namespace debug_ui
