#include "debug_ui_internal.h"

#include "building_template.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace debug_ui {
void DrawFlowArrow(Rectangle left, Rectangle right) {
    const float y = left.y + left.height * 0.5f;
    const float start = left.x + left.width + 4.0f;
    const float end = right.x - 5.0f;
    if (end <= start) return;
    DrawLineEx({start, y}, {end, y}, 2.0f, kMuted);
    DrawTriangle({end, y}, {end - 6.0f, y - 4.0f},
                 {end - 6.0f, y + 4.0f}, kMuted);
}

void DrawFlowStage(Font font, Rectangle bounds, const std::string& title,
                   const std::string& primary, const std::string& secondary,
                   Color color) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangle(static_cast<int>(bounds.x), static_cast<int>(bounds.y),
                  static_cast<int>(bounds.width), 3, color);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
    DrawTextAt(font, title, bounds.x + 10.0f, bounds.y + 9.0f,
               kDebugBodyFontSize, kMuted);
    DrawFittedText(font, primary,
                   {bounds.x + 10.0f, bounds.y + 31.0f,
                    bounds.width - 20.0f, 25.0f},
                   22.0f, kText);
    DrawFittedText(font, secondary,
                   {bounds.x + 10.0f, bounds.y + 59.0f,
                    bounds.width - 20.0f, 19.0f},
                   kDebugCaptionFontSize, kMuted);
}

void DrawTradePie(Font font, Rectangle bounds, const std::string& title,
                  const std::vector<std::pair<std::string, Money>>& values) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
    DrawTextAt(font, title, bounds.x + 10.0f, bounds.y + 8.0f,
               kDebugTableFontSize, kText);
    Money total = Money(0);
    for (const auto& value : values) total += std::max(Money(0), value.second);
    const Vector2 center = {bounds.x + 62.0f, bounds.y + bounds.height * 0.5f + 8.0f};
    const float radius = std::min(45.0f, bounds.height * 0.32f);
    const std::array<Color, 5> colors = {kBlue, kGreen, kOrange, kGold, kRed};
    float angle = -90.0f;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const Money amount = std::max(Money(0), values[index].second);
        if (total > Money(0) && amount > Money(0)) {
            const float next = angle + static_cast<float>(
                360.0 * (amount / total).toDouble());
            DrawCircleSector(center, radius, angle, next, 24,
                             colors[index % colors.size()]);
            angle = next;
        }
        const float legendY = bounds.y + 31.0f + static_cast<float>(index) * 18.0f;
        DrawRectangle(static_cast<int>(bounds.x + 120.0f),
                      static_cast<int>(legendY + 3.0f), 9, 9,
                      colors[index % colors.size()]);
        const double share = total > Money(0) ? (amount / total).toDouble() : 0.0;
        DrawFittedText(font, values[index].first + " " + PercentText(share) +
                             " " + NumberText(amount),
                       {bounds.x + 134.0f, legendY, bounds.width - 140.0f, 17.0f},
                       kDebugMinimumFontSize, kMuted);
    }
    if (values.empty() || total <= Money(0))
        DrawTextAt(font, "暂无已完成流量", bounds.x + 18.0f,
                   bounds.y + bounds.height - 26.0f, kDebugCaptionFontSize, kMuted);
}
std::vector<std::pair<std::string, Money>> TopGoodsBreakdown(
    const std::vector<std::pair<std::string, Money>>& values,
    std::size_t maxSlices = 5) {
    std::vector<std::pair<std::string, Money>> sorted;
    for (const auto& value : values) {
        if (value.second > Money(0)) sorted.push_back(value);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& left, const auto& right) {
                  return left.second > right.second;
              });
    if (sorted.size() <= maxSlices) return sorted;
    std::vector<std::pair<std::string, Money>> result(
        sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(maxSlices - 1));
    Money other = Money(0);
    for (std::size_t index = maxSlices - 1; index < sorted.size(); ++index)
        other += sorted[index].second;
    if (other > Money(0)) result.push_back({"其他", other});
    return result;
}

std::vector<std::pair<std::string, Money>> OverallTradeBreakdown(
    const TransportationSnapshot& transport, bool incoming) {
    std::array<Money, NUM_GOODS> totals{};
    for (const TradeFlowSnapshot& record : transport.tradeFlows) {
        if (record.goodIndex < 0 || record.goodIndex >= NUM_GOODS ||
            record.quantity <= Money(0)) {
            continue;
        }
        const int endpoint = incoming
            ? record.destinationWarehouseId : record.sourceWarehouseId;
        if (endpoint < 0) continue;
        totals[record.goodIndex] += record.quantity;
    }
    std::vector<std::pair<std::string, Money>> values;
    for (int goodIndex = 0; goodIndex < NUM_GOODS; ++goodIndex) {
        if (totals[goodIndex] > Money(0))
            values.push_back({commodityNames[goodIndex], totals[goodIndex]});
    }
    return TopGoodsBreakdown(values);
}
void DrawGoodsPanel(DebugUIState* state, World& world, Font font,
                    const DebugLayout& layout,
                    const TransportationSnapshot& transport) {
    const int good = state->selectedGood;
    const int marketIndex = state->selectedMarket;
    const LocalMarket& market = world.getMarket(marketIndex);
    const MarketSnapshot snapshot = market.getSnapshot();
    const MarketFlowSnapshot& flow = market.getLatestFlow();
    const auto& stock = snapshot.warehouseStock[good];
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;

    DrawTextAt(font, commodityNames[good], x, y,
               kDebugPageTitleFontSize, kText);
    DrawTextAt(font, market.getMarketName() + "  |  价格 " +
                     NumberText(snapshot.prices[good], 2),
               x + 146.0f, y + 6.0f, kDebugBodyFontSize, kMuted);
    const std::string cycleLabel = flow.cycle < 0
        ? "尚未推进"
        : "计划 " + std::to_string(snapshot.productionPlanCycle) +
              " / 结果 " + std::to_string(snapshot.productionResultCycle) +
              " / 状态 " + std::to_string(snapshot.cycle);
    const Vector2 cycleSize =
        MeasureTextEx(font, cycleLabel.c_str(),
                      kDebugCaptionFontSize, 0.0f);
    DrawTextAt(font, cycleLabel, x + width - cycleSize.x, y + 7.0f,
               kDebugCaptionFontSize, kMuted);

    const float gap = 16.0f;
    const float stageWidth = (width - gap * 3.0f) / 4.0f;
    const float stageY = y + 43.0f;
    const float stageHeight = 92.0f;
    std::array<Rectangle, 4> stages{};
    for (int index = 0; index < 4; ++index) {
        stages[index] = {x + index * (stageWidth + gap), stageY,
                         stageWidth, stageHeight};
    }
    const Money industrialUse =
        flow.buildingConsumed[good] + flow.directProductionUse[good];
    const Money finalUse =
        flow.consumerUse[good] + flow.constructionUse[good];
    DrawFlowStage(font, stages[0], "1 生产",
                  NumberText(flow.production[good]),
                  "计划 " + NumberText(snapshot.productionCommand[good]) +
                  "  期末待产 " +
                  NumberText(snapshot.pendingProduction[good]),
                  kGreen);
    DrawFlowStage(font, stages[1], "2 仓储",
                  NumberText(stock.onHand),
                  "周均 " + NumberText(stock.averageDemand) +
                  "  覆盖 " + NumberText(Money(stock.coverageWeeks), 1) +
                  " 周", kGold);
    DrawFlowStage(font, stages[2], "3 运输",
                  NumberText(flow.inTransit[good]),
                  "收 " + NumberText(flow.received[good]) +
                  "  发 " + NumberText(flow.dispatched[good]), kBlue);
    DrawFlowStage(font, stages[3], "4 消费",
                  NumberText(industrialUse + finalUse),
                  "生产用 " + NumberText(industrialUse) +
                  "  最终 " + NumberText(finalUse), kOrange);
    for (int index = 0; index < 3; ++index)
        DrawFlowArrow(stages[index], stages[index + 1]);

    const bool balanced =
        std::fabs(flow.inventoryResidual[good].toDouble()) <= 1e-7;
    const std::string equation =
        "守恒：期初 " + NumberText(flow.openingStock[good]) +
        " + 生产 " + NumberText(flow.production[good]) +
        " + 收货 " + NumberText(flow.received[good]) +
        " - 发运/生产/消费 = 期末 " +
        NumberText(flow.closingStock[good]) +
        "  |  残差 " + NumberText(flow.inventoryResidual[good], 4);
    DrawFittedText(font, equation,
                   {x, stageY + stageHeight + 7.0f, width, 24.0f},
                   kDebugTableFontSize, balanced ? kGreen : kRed);

    const float tableTitleY = stageY + stageHeight + 38.0f;
    const std::vector<int> marketIndices =
        MarketIndicesForLayout(state, world, layout);
    const int tableVisibleRows = std::max(
        1, static_cast<int>((layout.contentHeight - tableTitleY - 190.0f) /
                            30.0f));
    const int tableMaxScroll = std::max(
        0, static_cast<int>(marketIndices.size()) - tableVisibleRows);
    state->goodsScroll = std::clamp(state->goodsScroll, 0, tableMaxScroll);
    const std::string tableTitle = layout.mode == DebugUIMode::EmbeddedLocalMarket
        ? "关联市场同商品对照" : "五市场同商品对照";
    DrawSectionTitle(font, tableTitle, x, tableTitleY, width,
                     "同一盘点决策与生产结果周期");
    const float headerY = tableTitleY + 35.0f;
    const float headerHeight = 28.0f;
    const float rowHeight = 30.0f;
    const std::array<float, 13> fractions = {
        0.00f, 0.08f, 0.16f, 0.23f, 0.31f, 0.39f, 0.47f,
        0.55f, 0.63f, 0.71f, 0.79f, 0.87f, 1.00f
    };
    const char* headers[12] = {
        "市场", "周均需", "覆盖周", "目标", "补货点", "库存位",
        "本周请求", "已确认", "在途", "缺口", "计划产", "实际产"
    };
    DrawRectangle(static_cast<int>(x), static_cast<int>(headerY),
                   static_cast<int>(width), static_cast<int>(headerHeight),
                   Color{235, 238, 235, 255});
    for (int column = 0; column < 12; ++column) {
        DrawFittedText(font, headers[column],
                       {x + width * fractions[column] + 5.0f, headerY,
                        width * (fractions[column + 1] -
                                 fractions[column]) - 8.0f, headerHeight},
                       kDebugTableFontSize, kMuted);
    }
    int drawnRows = 0;
    for (int row = 0;
         row < tableVisibleRows && row + state->goodsScroll <
             static_cast<int>(marketIndices.size()); ++row) {
        const int index = marketIndices[static_cast<std::size_t>(
            row + state->goodsScroll)];
        const LocalMarket& rowMarket = world.getMarket(index);
        const MarketSnapshot rowSnapshot = rowMarket.getSnapshot();
        const MarketFlowSnapshot& rowFlow = rowMarket.getLatestFlow();
        const auto& rowStock = rowSnapshot.warehouseStock[good];
        const bool reviewed = rowStock.reviewCycle >= 0;
        const auto reviewValue = [reviewed](Money review, Money current) {
            return reviewed ? review : current;
        };
        const float rowY = headerY + headerHeight + row * rowHeight;
        DrawRectangle(static_cast<int>(x), static_cast<int>(rowY),
                      static_cast<int>(width), static_cast<int>(rowHeight),
                      index == marketIndex ? kGreenSoft :
                      (index % 2 == 0 ? kSurface : kBackground));
        const std::array<std::string, 12> cells = {
            MarketCode(rowMarket.getMarketId()),
            NumberText(reviewValue(rowStock.reviewAverageDemand, rowStock.averageDemand)),
            NumberText(Money(reviewed ? rowStock.reviewCoverageWeeks : rowStock.coverageWeeks), 1),
            NumberText(reviewValue(rowStock.reviewTargetStock, rowStock.targetStock)),
            NumberText(reviewValue(rowStock.reviewReorderPoint, rowStock.reorderPoint)),
            NumberText(reviewValue(rowStock.reviewPosition, rowStock.position)),
            NumberText(rowStock.plannedRequest),
            NumberText(reviewValue(rowStock.reviewConfirmedInbound, rowStock.confirmedInbound)),
            NumberText(reviewValue(rowStock.reviewPhysicalInTransit, rowStock.physicalInTransit)),
            NumberText(rowStock.rawReplenishment),
            NumberText(rowSnapshot.productionCommand[good]),
            NumberText(rowFlow.production[good])
        };
        for (int column = 0; column < 12; ++column) {
            DrawFittedText(font, cells[column],
                           {x + width * fractions[column] + 5.0f, rowY,
                            width * (fractions[column + 1] -
                                     fractions[column]) - 8.0f, rowHeight},
                           kDebugTableFontSize,
                           column == 9 && rowStock.rawReplenishment > Money(1e-7)
                               ? kOrange : kText);
        }
        DrawLineEx({x, rowY + rowHeight},
                   {x + width, rowY + rowHeight}, 1.0f, kBorder);
        ++drawnRows;
    }

    const float lowerY =
        headerY + headerHeight + drawnRows * rowHeight + 16.0f;
    std::vector<std::pair<std::string, Money>> exports;
    std::vector<std::pair<std::string, Money>> imports;
    for (const TradeFlowSnapshot& flowRecord : transport.tradeFlows) {
        if (flowRecord.goodIndex != good) continue;
        const int marketId = market.getMarketId();
        if (flowRecord.sourceWarehouseId == marketId) {
            const std::string partner = MarketCode(flowRecord.destinationWarehouseId);
            auto it = std::find_if(exports.begin(), exports.end(),
                [&](const auto& item) { return item.first == partner; });
            if (it == exports.end()) exports.push_back({partner, flowRecord.quantity});
            else it->second += flowRecord.quantity;
        }
        if (flowRecord.destinationWarehouseId == marketId) {
            const std::string partner = MarketCode(flowRecord.sourceWarehouseId);
            auto it = std::find_if(imports.begin(), imports.end(),
                [&](const auto& item) { return item.first == partner; });
            if (it == imports.end()) imports.push_back({partner, flowRecord.quantity});
            else it->second += flowRecord.quantity;
        }
    }
    const std::vector<std::pair<std::string, Money>> consumers = {
        {"居民消费", flow.consumerUse[good]},
        {"生产投入", industrialUse},
        {"建造部门", flow.constructionUse[good]}
    };
    const float pieGap = 12.0f;
    const float pieWidth = (width - pieGap * 2.0f) / 3.0f;
    DrawTradePie(font, {x, lowerY, pieWidth, 116.0f}, "出口去向", exports);
    DrawTradePie(font, {x + pieWidth + pieGap, lowerY, pieWidth, 116.0f},
                 "进口来源", imports);
    DrawTradePie(font, {x + (pieWidth + pieGap) * 2.0f, lowerY,
                        pieWidth, 116.0f},
                 "消费方（本周期）", consumers);
    const float listsY = lowerY + 128.0f;
    const float lowerGap = 18.0f;
    const float columnWidth = (width - lowerGap) * 0.5f;
    DrawSectionTitle(font, "关联路线", x, listsY, columnWidth,
                     "使用 / 容量 / 排队");
    int relatedShipments = 0;
    for (const ShipmentSnapshot& shipment : transport.shipments) {
        if (shipment.goodIndex == good &&
            (shipment.sourceWarehouseId == market.getMarketId() ||
             shipment.destinationWarehouseId == market.getMarketId())) {
            ++relatedShipments;
        }
    }
    DrawSectionTitle(font, "活动订单", x + columnWidth + lowerGap, listsY,
                     columnWidth,
                     std::to_string(relatedShipments) + " 笔关联在途");

    const int lowerVisibleRows = std::max(
        1, static_cast<int>((layout.height - (listsY + 36.0f) - 14.0f) /
                            24.0f));
    int routeRow = 0;
    for (const RouteSnapshot& route : transport.routes) {
        if (route.goodIndex != good ||
            (route.sourceWarehouseId != market.getMarketId() &&
             route.destinationWarehouseId != market.getMarketId())) {
            continue;
        }
        if (routeRow >= lowerVisibleRows) break;
        const float rowY = listsY + 36.0f + routeRow * 24.0f;
        const std::string label =
            MarketCode(route.sourceWarehouseId) + " > " +
            MarketCode(route.destinationWarehouseId) + "  用 " +
            NumberText(route.usedCapacity) + " / " +
            NumberText(route.capacityPerCycle) + "  排 " +
            NumberText(route.queuedVolume) + "  " +
            (route.railwayAvailable
                 ? (route.profitable ? "可贸易" : "无利润")
                 : "缺铁路");
        DrawFittedText(font, label,
                       {x, rowY, columnWidth, 23.0f},
                       kDebugCaptionFontSize,
                       !route.railwayAvailable ? kRed :
                       (route.profitable ? kBlue : kOrange));
        ++routeRow;
    }
    if (routeRow == 0)
        DrawTextAt(font, "无关联路线", x, listsY + 39.0f,
                   kDebugTableFontSize, kMuted);

    std::vector<const WarehouseOrderSnapshot*> orders;
    for (const WarehouseOrderSnapshot& order : transport.orders) {
        if (order.goodIndex == good &&
            (order.buyerWarehouseId == market.getMarketId() ||
             order.sellerWarehouseId == market.getMarketId())) {
            orders.push_back(&order);
        }
    }
    int orderRow = 0;
    for (auto it = orders.rbegin();
         it != orders.rend() && orderRow < lowerVisibleRows;
         ++it, ++orderRow) {
        const WarehouseOrderSnapshot& order = **it;
        const float rowY = listsY + 36.0f + orderRow * 24.0f;
        const std::string routeText =
            MarketCode(order.sellerWarehouseId) + " > " +
            MarketCode(order.buyerWarehouseId);
        const std::string label =
            "#" + std::to_string(order.id) + " " +
            OrderKindText(order.kind) + "  " + routeText + "  " +
            NumberText(order.received) + " / " +
            NumberText(order.requested) + "  " +
            OrderStatusText(order.status);
        DrawFittedText(font, label,
                       {x + columnWidth + lowerGap, rowY,
                        columnWidth, 23.0f},
                       kDebugCaptionFontSize,
                       OrderStatusColor(order.status));
    }
    if (orders.empty())
        DrawTextAt(font, "无活动订单", x + columnWidth + lowerGap,
                   listsY + 39.0f, kDebugTableFontSize, kMuted);
}


}  // namespace debug_ui
