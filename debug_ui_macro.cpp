#include "debug_ui_internal.h"


#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace debug_ui {
void DrawMacroPanel(DebugUIState* state, World& world, Font font,
                    const DebugLayout& layout,
                    const TransportationSnapshot& transport) {
    const LocalMarket& market = world.getMarket(state->selectedMarket);
    const MarketFlowSnapshot& flow = market.getLatestFlow();
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;

    DrawTextAt(font, "宏观与物流", x, y,
               kDebugPageTitleFontSize, kText);
    DrawTextAt(font, market.getMarketName(), x + 165.0f, y + 7.0f,
               kDebugBodyFontSize, kMuted);

    const float metricY = y + 46.0f;
    const float metricGap = 12.0f;
    const float metricWidth = (width - metricGap * 3.0f) / 4.0f;
    DrawMetric(font, {x, metricY, metricWidth, 72.0f},
               "52 周折算国内生产总值", NumberText(market.getGDP()), kGreen);
    DrawMetric(font, {x + metricWidth + metricGap, metricY,
                       metricWidth, 72.0f},
               "本周国内生产总值", NumberText(market.getWeeklyGDP()),
               market.getWeeklyGDP() > Money(0) ? kBlue : kOrange);
    DrawMetric(font, {x + (metricWidth + metricGap) * 2.0f, metricY,
                       metricWidth, 72.0f},
               "总产出价值", NumberText(flow.grossOutputValue), kGold);
    DrawMetric(font, {x + (metricWidth + metricGap) * 3.0f, metricY,
                       metricWidth, 72.0f},
               "中间投入", NumberText(flow.intermediateCost), kOrange);

    const float marketSectionY = metricY + 86.0f;
    const std::vector<int> marketIndices =
        MarketIndicesForLayout(state, world, layout);
    const int tableVisibleRows = std::max(
        1, static_cast<int>((layout.contentHeight - marketSectionY - 420.0f) /
                            30.0f));
    const int tableMaxScroll = std::max(
        0, static_cast<int>(marketIndices.size()) - tableVisibleRows);
    state->goodsScroll = std::clamp(state->goodsScroll, 0, tableMaxScroll);
    const std::string tableTitle = layout.mode == DebugUIMode::EmbeddedLocalMarket
        ? "关联市场宏观对照" : "五市场宏观对照";
    DrawSectionTitle(font, tableTitle, x, marketSectionY, width,
                     "国内生产总值使用 52 周折算口径");
    const float tableY = marketSectionY + 35.0f;
    const float tableHeaderHeight = 28.0f;
    const float marketRowHeight = 30.0f;
    const std::array<float, 8> fractions = {
        0.00f, 0.14f, 0.27f, 0.41f,
        0.55f, 0.69f, 0.84f, 1.00f
    };
    const char* headers[7] = {
        "市场", "本周国内生产总值", "年化国内生产总值", "产出价值",
        "中间投入", "人口", "满意度"
    };
    DrawRectangle(static_cast<int>(x), static_cast<int>(tableY),
                   static_cast<int>(width),
                   static_cast<int>(tableHeaderHeight),
                   Color{235, 238, 235, 255});
    for (int column = 0; column < 7; ++column) {
        DrawFittedText(font, headers[column],
                       {x + width * fractions[column] + 5.0f, tableY,
                        width * (fractions[column + 1] -
                                  fractions[column]) - 8.0f,
                        tableHeaderHeight},
                       kDebugTableFontSize, kMuted);
    }
    int drawnRows = 0;
    for (int row = 0;
         row < tableVisibleRows && row + state->goodsScroll <
             static_cast<int>(marketIndices.size()); ++row) {
        const int index = marketIndices[static_cast<std::size_t>(
            row + state->goodsScroll)];
        const LocalMarket& rowMarket = world.getMarket(index);
        const MarketFlowSnapshot& rowFlow = rowMarket.getLatestFlow();
        const float rowY = tableY + tableHeaderHeight +
                           row * marketRowHeight;
        DrawRectangle(static_cast<int>(x), static_cast<int>(rowY),
                      static_cast<int>(width),
                      static_cast<int>(marketRowHeight),
                      index == state->selectedMarket ? kGreenSoft :
                      (index % 2 == 0 ? kSurface : kBackground));
        const std::array<std::string, 7> cells = {
            MarketCode(rowMarket.getMarketId()),
            NumberText(rowMarket.getWeeklyGDP()),
            NumberText(rowMarket.getGDP()),
            NumberText(rowFlow.grossOutputValue),
            NumberText(rowFlow.intermediateCost),
            NumberText(rowMarket.getPopulation()),
            PercentText(rowMarket.getSatisfaction())
        };
        for (int column = 0; column < 7; ++column) {
            Color color = kText;
            if (rowMarket.getStepCount() > 0 && column == 1 &&
                rowMarket.getWeeklyGDP() <= Money(0))
                color = kOrange;
            if (rowMarket.getStepCount() > 0 && column == 2 &&
                rowMarket.getGDP() <= Money(0))
                color = kRed;
            DrawFittedText(font, cells[column],
                           {x + width * fractions[column] + 5.0f, rowY,
                             width * (fractions[column + 1] -
                                      fractions[column]) - 8.0f,
                             marketRowHeight},
                            kDebugTableFontSize, color);
        }
        DrawLineEx({x, rowY + marketRowHeight},
                   {x + width, rowY + marketRowHeight},
                   1.0f, kBorder);
        ++drawnRows;
    }

    const float chartY =
        tableY + tableHeaderHeight +
        drawnRows * marketRowHeight + 16.0f;
    const float chartGap = 14.0f;
    const float chartWidth = (width - chartGap) * 0.5f;
    const bool compact = layout.height < 860.0f;
    const float chartHeight = compact
        ? 116.0f
        : std::clamp(layout.height - chartY - 430.0f, 120.0f, 180.0f);
    const auto overallExports = OverallTradeBreakdown(transport, false);
    const auto overallImports = OverallTradeBreakdown(transport, true);
    const float pieY = compact ? chartY : chartY + chartHeight + 12.0f;
    if (!compact) {
        DrawMoneySeries(font, market.getGDPHistory(),
                        {x, chartY, chartWidth, chartHeight},
                        kBlue, "国内生产总值历史（最近 260 周）");
        DrawDoubleSeries(font, market.getPopulationHistory(),
                         {x + chartWidth + chartGap, chartY,
                          chartWidth, chartHeight},
                         kGreen, "人口历史（最近 260 周）");
    }
    DrawTradePie(font, {x, pieY, chartWidth, 116.0f},
                 "总体出口构成（近52周）", overallExports);
    DrawTradePie(font, {x + chartWidth + chartGap, pieY,
                        chartWidth, 116.0f},
                 "总体进口构成（近52周）", overallImports);
    const float logisticsY = compact
        ? chartY + 130.0f
        : pieY + 130.0f;
    const bool healthy =
        state->cachedAudit.valid && state->cachedInventoryBalanced;
    const std::string auditText = healthy
        ? "审计通过"
        : (state->cachedAudit.message.empty()
               ? "库存守恒异常" : state->cachedAudit.message);
    int localOrderCount = 0;
    for (const WarehouseOrderSnapshot& order : transport.orders) {
        if (order.buyerWarehouseId == market.getMarketId() ||
            order.sellerWarehouseId == market.getMarketId()) {
            ++localOrderCount;
        }
    }
    int localShipmentCount = 0;
    for (const ShipmentSnapshot& shipment : transport.shipments) {
        if (shipment.sourceWarehouseId == market.getMarketId() ||
            shipment.destinationWarehouseId == market.getMarketId()) {
            ++localShipmentCount;
        }
    }
    Money localRailwayRevenue = Money(0);
    for (const RouteSnapshot& route : transport.routes) {
        if (route.sourceWarehouseId == market.getMarketId() ||
            route.destinationWarehouseId == market.getMarketId()) {
            localRailwayRevenue += route.railwayRevenue;
        }
    }
    // Warehouse profit is deliberately not shown: the warehouse margin share is
    // pinned to zero by design (see warehouse_audit.cpp), so the value is always
    // zero and displaying it implies a margin that does not exist.
    DrawSectionTitle(font, "当前市场物流活动", x, logisticsY, width,
                     auditText + "  |  " +
                     std::to_string(localOrderCount) + " 活动订单  " +
                     std::to_string(localShipmentCount) + " 在途  铁路收入 " +
                     NumberText(localRailwayRevenue));
    if (!healthy) {
        DrawRectangle(static_cast<int>(x),
                      static_cast<int>(logisticsY + 31.0f),
                      static_cast<int>(width), 22, kRedSoft);
        DrawFittedText(font, auditText,
                       {x + 6.0f, logisticsY + 31.0f,
                        width - 12.0f, 24.0f},
                       kDebugCaptionFontSize, kRed);
    }

    const float listY = logisticsY + (healthy ? 34.0f : 58.0f);
    const float listGap = 18.0f;
    const float listWidth = (width - listGap) * 0.5f;
    DrawTextAt(font, "在途与路线", x, listY,
               kDebugTableFontSize, kMuted);
    DrawTextAt(font, "订单状态", x + listWidth + listGap,
               listY, kDebugTableFontSize, kMuted);
    const float rowsY = listY + 24.0f;
    const int visible = std::max(
        1, static_cast<int>((layout.height - rowsY - 14.0f) / 24.0f));

    int activityRow = 0;
    for (const ShipmentSnapshot& shipment : transport.shipments) {
        if (shipment.sourceWarehouseId != market.getMarketId() &&
            shipment.destinationWarehouseId != market.getMarketId()) {
            continue;
        }
        if (activityRow >= visible) break;
        const std::string label =
            "在途 #" + std::to_string(shipment.id) + "  " +
            MarketCode(shipment.sourceWarehouseId) + " > " +
            MarketCode(shipment.destinationWarehouseId) + "  " +
            commodityNames[shipment.goodIndex] + " " +
            NumberText(shipment.cargo) + "  剩 " +
            std::to_string(shipment.remainingCycles) + " 周";
        DrawFittedText(font, label,
                       {x, rowsY + activityRow * 24.0f,
                        listWidth, 23.0f},
                       kDebugCaptionFontSize, kBlue);
        ++activityRow;
    }
    for (const RouteSnapshot& route : transport.routes) {
        if (route.sourceWarehouseId != market.getMarketId() &&
            route.destinationWarehouseId != market.getMarketId()) {
            continue;
        }
        if (activityRow >= visible) break;
        const std::string label =
            "路线 #" + std::to_string(route.id) + "  " +
            MarketCode(route.sourceWarehouseId) + " > " +
            MarketCode(route.destinationWarehouseId) + "  " +
            commodityNames[route.goodIndex] + "  " +
            NumberText(route.distanceKm, 0) + " 千米  运力 " +
            NumberText(route.railwayCapacityPricePerUnit) + " / 货物 " +
            NumberText(route.transportCostPerUnit);
        DrawFittedText(font, label,
                       {x, rowsY + activityRow * 24.0f,
                        listWidth, 23.0f},
                       kDebugCaptionFontSize,
                       !route.railwayAvailable ? kRed :
                       (!route.profitable ? kOrange :
                        (route.usedCapacity > Money(0) ? kBlue : kMuted)));
        ++activityRow;
    }
    if (activityRow == 0)
        DrawTextAt(font, "当前周期没有关联运输活动。",
                   x, rowsY, kDebugCaptionFontSize, kMuted);

    std::vector<const WarehouseOrderSnapshot*> orders;
    for (const WarehouseOrderSnapshot& order : transport.orders) {
        if (order.buyerWarehouseId == market.getMarketId() ||
            order.sellerWarehouseId == market.getMarketId()) {
            orders.push_back(&order);
        }
    }
    const int maxScroll =
        std::max(0, static_cast<int>(orders.size()) - visible);
    state->orderScroll = std::clamp(state->orderScroll, 0, maxScroll);
    for (int row = 0;
         row < visible && row + state->orderScroll <
                            static_cast<int>(orders.size());
         ++row) {
        const WarehouseOrderSnapshot& order =
            *orders[orders.size() - 1 -
                    static_cast<std::size_t>(row + state->orderScroll)];
        const std::string label =
            "#" + std::to_string(order.id) + " " +
            OrderKindText(order.kind) + "  " +
            commodityNames[order.goodIndex] + "  " +
            NumberText(order.received) + "/" +
            NumberText(order.requested) + "  " +
            OrderStatusText(order.status);
        DrawFittedText(font, label,
                       {x + listWidth + listGap,
                         rowsY + row * 24.0f,
                         listWidth, 23.0f},
                        kDebugCaptionFontSize,
                        OrderStatusColor(order.status));
    }
    if (orders.empty())
        DrawTextAt(font, "当前市场没有活动订单。",
                   x + listWidth + listGap, rowsY,
                   kDebugCaptionFontSize, kMuted);
}


}  // namespace debug_ui
