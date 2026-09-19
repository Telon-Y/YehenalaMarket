#include "ui_internal.h"
#include "number_format.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr Color kPageBackground{241, 244, 241, 255};
constexpr Color kSurface{252, 253, 251, 255};
constexpr Color kHeader{35, 48, 47, 255};
constexpr Color kText{31, 47, 44, 255};
constexpr Color kMuted{101, 113, 108, 255};
constexpr Color kBorder{199, 210, 204, 255};
constexpr Color kGreen{42, 128, 88, 255};
constexpr Color kBlue{48, 107, 166, 255};
constexpr Color kGold{181, 139, 45, 255};
constexpr Color kRed{177, 66, 57, 255};
constexpr Color kPurple{125, 87, 153, 255};
constexpr Color kOrange{190, 105, 41, 255};

bool gGoodsPanelClipActive = false;
Rectangle gGoodsPanelClip{};

struct CommodityMetrics {
    double price = 0.0;
    Money purchases = Money(0);
    Money supply = Money(0);
};

struct GoodsPageLayout {
    Rectangle panel{};
    Rectangle header{};
    Rectangle backButton{};
    Rectangle content{};
    Rectangle tableHeader{};
    float firstRowY = 0.0f;
    float rowHeight = 48.0f;
    int visibleRows = 1;
    int maxScroll = 0;
};

struct ChartSeries {
    std::string label;
    const std::vector<double>* values = nullptr;
    Color color{};
};

GoodsPageLayout MakeGoodsPageLayout() {
    GoodsPageLayout layout;
    layout.panel = CurrentUILayout().provincePanel;
    layout.header = {layout.panel.x, layout.panel.y,
                     layout.panel.width, 72.0f};
    const float inset = std::clamp(layout.panel.width * 0.02f,
                                   12.0f, 18.0f);
    constexpr float scrollbarGutter = 12.0f;
    layout.backButton = {layout.panel.x + 18.0f,
                         layout.panel.y + 16.0f, 40.0f, 40.0f};
    layout.content = {
        layout.panel.x + inset, layout.panel.y + 104.0f,
        std::max(1.0f, layout.panel.width - inset * 2.0f -
                           scrollbarGutter),
        std::max(1.0f, layout.panel.height - 124.0f)};
    layout.tableHeader = {layout.content.x, layout.content.y,
                          layout.content.width, 38.0f};
    layout.firstRowY = layout.tableHeader.y + layout.tableHeader.height;
    layout.rowHeight = 48.0f;
    layout.visibleRows = std::max(
        1, static_cast<int>((layout.panel.y + layout.panel.height -
                            layout.firstRowY - 20.0f) /
                            layout.rowHeight));
    layout.maxScroll = std::max(0, NUM_GOODS - layout.visibleRows);
    return layout;
}

void DrawFittedText(Font font, const std::string& text, Rectangle bounds,
                    float fontSize, Color color, float padding = 0.0f) {
    const float available = std::max(1.0f, bounds.width - padding * 2.0f);
    float fitted = fontSize;
    while (fitted > 12.0f &&
           MeasureTextEx(font, text.c_str(), fitted, 0.0f).x > available) {
        fitted -= 1.0f;
    }
    BeginScissorMode(static_cast<int>(bounds.x),
                     static_cast<int>(bounds.y),
                     std::max(1, static_cast<int>(bounds.width)),
                     std::max(1, static_cast<int>(bounds.height)));
    DrawTextEx(font, text.c_str(),
               {bounds.x + padding,
                bounds.y + std::max(0.0f,
                    (bounds.height - fitted) * 0.5f)},
               fitted, 0.0f, color);
    EndScissorMode();
    if (gGoodsPanelClipActive) {
        BeginScissorMode(static_cast<int>(gGoodsPanelClip.x),
                         static_cast<int>(gGoodsPanelClip.y),
                         std::max(1, static_cast<int>(gGoodsPanelClip.width)),
                         std::max(1, static_cast<int>(gGoodsPanelClip.height)));
    }
}

std::string NumberText(double value, int precision = 1) {
    return FormatChineseNumber(value, precision);
}

std::string NumberText(Money value, int precision = 1) {
    return NumberText(value.toDouble(), precision);
}

std::string PercentText(double value) {
    if (!std::isfinite(value)) return "--";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%+.1f%%", value * 100.0);
    return buffer;
}

Money FlowPurchases(const MarketFlowSnapshot& flow, int good) {
    return flow.consumerUse[good] +
           flow.buildingConsumed[good] +
           flow.directProductionUse[good] +
           flow.constructionUse[good];
}

CommodityMetrics CountryCommodityMetrics(const World& world,
                                          const Country& country,
                                          int good) {
    CommodityMetrics metrics;
    int pricedMarkets = 0;
    const auto& marketIds =
        country.getNationalMarket().getLocalMarketIds();
    for (const int marketId : marketIds) {
        const LocalMarket& market = world.getMarketById(marketId);
        const Money price = market.getPrices()[good];
        if (isfinite(price) && price > Money(0)) {
            metrics.price += price.toDouble();
            ++pricedMarkets;
        }
        const MarketFlowSnapshot& flow = market.getLatestFlow();
        metrics.supply += flow.production[good];
        metrics.purchases += FlowPurchases(flow, good);
    }
    if (pricedMarkets > 0)
        metrics.price /= static_cast<double>(pricedMarkets);
    if (good == TRANSPORT_CAPACITY_GOOD_INDEX) {
        const TransportationSnapshot& transport =
            world.getTransportationSnapshot();
        for (const RouteSnapshot& route : transport.routes) {
            const auto location = std::find_if(
                transport.warehouses.begin(), transport.warehouses.end(),
                [&](const WarehouseLocationSnapshot& warehouse) {
                    return warehouse.warehouseId ==
                           route.destinationWarehouseId;
                });
            if (location != transport.warehouses.end() &&
                location->countryId == country.getId()) {
                metrics.purchases += route.usedCapacity;
            }
        }
    }
    return metrics;
}

std::vector<double> CountryPriceHistory(const World& world,
                                        const Country& country, int good) {
    std::size_t length = 0;
    const auto& marketIds =
        country.getNationalMarket().getLocalMarketIds();
    for (const int marketId : marketIds) {
        length = std::max(
            length, world.getMarketById(marketId).getPriceHistory().size());
    }

    std::vector<double> result(length, 0.0);
    std::vector<int> counts(length, 0);
    for (const int marketId : marketIds) {
        const auto& history =
            world.getMarketById(marketId).getPriceHistory();
        const std::size_t offset = length - history.size();
        for (std::size_t index = 0; index < history.size(); ++index) {
            result[offset + index] += history[index][good].toDouble();
            ++counts[offset + index];
        }
    }
    for (std::size_t index = 0; index < length; ++index) {
        if (counts[index] > 0)
            result[index] /= static_cast<double>(counts[index]);
    }
    return result;
}

std::pair<std::vector<double>, std::vector<double>> CountryFlowHistory(
    const World& world, const Country& country, int good) {
    std::size_t length = 0;
    const auto& marketIds =
        country.getNationalMarket().getLocalMarketIds();
    for (const int marketId : marketIds) {
        length = std::max(
            length, world.getMarketById(marketId).getOutputHistory().size());
    }

    std::vector<double> supply(length, 0.0);
    std::vector<double> demand(length, 0.0);
    for (const int marketId : marketIds) {
        const LocalMarket& market = world.getMarketById(marketId);
        const auto& output = market.getOutputHistory();
        const auto& purchases = market.getDemandHistory();
        const std::size_t count = std::min(output.size(), purchases.size());
        const std::size_t offset = length - count;
        for (std::size_t index = 0; index < count; ++index) {
            supply[offset + index] += output[index][good].toDouble();
            demand[offset + index] += purchases[index][good].toDouble();
        }
    }
    return {std::move(supply), std::move(demand)};
}

void DrawBackButton(Font font, Rectangle bounds) {
    const bool hovered = CheckCollisionPointRec(GetMousePosition(), bounds);
    DrawCircleV({bounds.x + bounds.width * 0.5f,
                 bounds.y + bounds.height * 0.5f},
                bounds.width * 0.5f,
                hovered ? Color{72, 91, 87, 255}
                        : Color{50, 67, 64, 255});
    const float centerY = bounds.y + bounds.height * 0.5f;
    DrawLineEx({bounds.x + 12.0f, centerY},
               {bounds.x + 29.0f, centerY}, 2.4f, RAYWHITE);
    DrawLineEx({bounds.x + 12.0f, centerY},
               {bounds.x + 20.0f, centerY - 8.0f}, 2.4f, RAYWHITE);
    DrawLineEx({bounds.x + 12.0f, centerY},
               {bounds.x + 20.0f, centerY + 8.0f}, 2.4f, RAYWHITE);
    if (hovered) {
        DrawFittedText(font, "返回", {bounds.x + bounds.width + 8.0f,
                       bounds.y + 5.0f, 52.0f, 30.0f},
                       15.0f, RAYWHITE);
    }
}

void DrawPageHeader(Font font, const GoodsPageLayout& layout,
                    const std::string& title,
                    const std::string& subtitle) {
    DrawRectangleRec(layout.header, kHeader);
    DrawBackButton(font, layout.backButton);
    const float subtitleWidth = std::clamp(
        layout.panel.width * 0.34f, 150.0f, 300.0f);
    const float subtitleX = layout.panel.x + layout.panel.width -
                            subtitleWidth - 18.0f;
    const float titleX = layout.panel.x + 76.0f;
    DrawFittedText(font, title,
                   {titleX, layout.panel.y + 10.0f,
                    std::max(70.0f, subtitleX - titleX - 14.0f), 52.0f},
                   25.0f, RAYWHITE);
    DrawFittedText(font, subtitle,
                   {subtitleX, layout.panel.y + 13.0f,
                    subtitleWidth, 46.0f},
                   15.0f, {192, 207, 201, 255});
}

void DrawMetric(Font font, Rectangle bounds, const std::string& label,
                const std::string& value, Color color) {
    DrawFittedText(font, label,
                   {bounds.x, bounds.y, bounds.width, 22.0f},
                   14.0f, kMuted, 8.0f);
    DrawFittedText(font, value,
                   {bounds.x, bounds.y + 21.0f, bounds.width, 32.0f},
                   21.0f, color, 8.0f);
}

void DrawLineChart(Font font, Rectangle bounds, const std::string& title,
                   const std::vector<ChartSeries>& series,
                   std::size_t firstIndex, bool zeroBaseline) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
    DrawFittedText(font, title,
                   {bounds.x + 10.0f, bounds.y + 6.0f,
                    bounds.width - 20.0f, 25.0f},
                   18.0f, kText);

    std::size_t count = 0;
    for (const ChartSeries& item : series) {
        if (item.values != nullptr)
            count = std::max(count, item.values->size());
    }
    firstIndex = std::min(firstIndex, count);
    if (count <= firstIndex) {
        DrawFittedText(font, "暂无历史数据",
                       {bounds.x + 12.0f, bounds.y + 48.0f,
                        bounds.width - 24.0f, bounds.height - 60.0f},
                       16.0f, kMuted);
        return;
    }

    float legendX = bounds.x + 10.0f;
    for (const ChartSeries& item : series) {
        DrawRectangle(static_cast<int>(legendX),
                      static_cast<int>(bounds.y + 36.0f), 12, 3,
                      item.color);
        const float labelWidth = std::min(
            112.0f, MeasureTextEx(font, item.label.c_str(), 13.0f, 0.0f).x);
        DrawFittedText(font, item.label,
                       {legendX + 17.0f, bounds.y + 27.0f,
                        labelWidth + 3.0f, 22.0f},
                       13.0f, kMuted);
        legendX += labelWidth + 28.0f;
    }

    const Rectangle plot = {bounds.x + 46.0f, bounds.y + 55.0f,
                            std::max(1.0f, bounds.width - 58.0f),
                            std::max(1.0f, bounds.height - 78.0f)};
    double minimum = zeroBaseline ? 0.0 : 1.0e300;
    double maximum = -1.0e300;
    for (const ChartSeries& item : series) {
        if (item.values == nullptr) continue;
        for (std::size_t index = firstIndex;
             index < item.values->size(); ++index) {
            const double value = (*item.values)[index];
            if (!std::isfinite(value)) continue;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
    }
    if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
        minimum = 0.0;
        maximum = 1.0;
    }
    if (maximum <= minimum) {
        const double padding = std::max(1.0, std::fabs(maximum) * 0.1);
        minimum = zeroBaseline ? 0.0 : minimum - padding;
        maximum += padding;
    }

    for (int line = 0; line <= 4; ++line) {
        const float y = plot.y + plot.height * line / 4.0f;
        DrawLineEx({plot.x, y}, {plot.x + plot.width, y},
                   1.0f, Color{218, 225, 221, 255});
    }
    DrawFittedText(font, NumberText(maximum),
                   {bounds.x + 2.0f, plot.y - 8.0f, 42.0f, 18.0f},
                   12.0f, kMuted);
    DrawFittedText(font, NumberText(minimum),
                   {bounds.x + 2.0f, plot.y + plot.height - 9.0f,
                    42.0f, 18.0f},
                   12.0f, kMuted);

    const std::size_t displayed = count - firstIndex;
    for (const ChartSeries& item : series) {
        if (item.values == nullptr || item.values->size() <= firstIndex)
            continue;
        const auto& values = *item.values;
        const std::size_t last = values.size() - 1;
        const std::size_t maxSegments = static_cast<std::size_t>(
            std::max(2.0f, plot.width * 1.5f));
        const std::size_t stride = std::max<std::size_t>(
            1, (last - firstIndex + maxSegments - 1) / maxSegments);
        bool hasPrevious = false;
        Vector2 previous{};
        for (std::size_t index = firstIndex; index <= last;) {
            const double value = values[index];
            if (std::isfinite(value)) {
                const float ratioX = displayed <= 1 ? 0.0f :
                    static_cast<float>(index - firstIndex) /
                    static_cast<float>(displayed - 1);
                const float ratioY = static_cast<float>(
                    (value - minimum) / (maximum - minimum));
                const Vector2 point = {
                    plot.x + ratioX * plot.width,
                    plot.y + plot.height -
                        std::clamp(ratioY, 0.0f, 1.0f) * plot.height
                };
                if (hasPrevious)
                    DrawLineEx(previous, point, 2.0f, item.color);
                previous = point;
                hasPrevious = true;
            }
            if (index == last) break;
            index = std::min(last, index + stride);
        }
        if (displayed == 1 && hasPrevious)
            DrawCircleV(previous, 3.0f, item.color);
    }

    DrawFittedText(font, std::to_string(firstIndex + 1),
                   {plot.x, plot.y + plot.height + 3.0f, 48.0f, 17.0f},
                   12.0f, kMuted);
    DrawFittedText(font, std::to_string(count),
                   {plot.x + plot.width - 48.0f,
                    plot.y + plot.height + 3.0f, 48.0f, 17.0f},
                   12.0f, kMuted);
}

std::vector<std::pair<std::string, Money>> TopBreakdown(
    const std::vector<std::pair<std::string, Money>>& values,
    std::size_t maximumSlices = 5) {
    std::vector<std::pair<std::string, Money>> sorted;
    for (const auto& value : values) {
        if (value.second > Money(0)) sorted.push_back(value);
    }
    std::sort(sorted.begin(), sorted.end(),
              [](const auto& left, const auto& right) {
                  return left.second > right.second;
              });
    if (sorted.size() <= maximumSlices) return sorted;

    std::vector<std::pair<std::string, Money>> result(
        sorted.begin(),
        sorted.begin() + static_cast<std::ptrdiff_t>(maximumSlices - 1));
    Money other = Money(0);
    for (std::size_t index = maximumSlices - 1;
         index < sorted.size(); ++index) {
        other += sorted[index].second;
    }
    result.push_back({"其他", other});
    return result;
}

void DrawPieChart(Font font, Rectangle bounds, const std::string& title,
                  const std::vector<std::pair<std::string, Money>>& source) {
    DrawRectangleRec(bounds, kSurface);
    DrawRectangleLinesEx(bounds, 1.0f, kBorder);
    DrawFittedText(font, title,
                   {bounds.x + 10.0f, bounds.y + 7.0f,
                    bounds.width - 20.0f, 25.0f},
                   18.0f, kText);

    const auto values = TopBreakdown(source);
    Money total = Money(0);
    for (const auto& value : values) total += value.second;
    if (total <= Money(0)) {
        DrawFittedText(font, "暂无本周期流量",
                       {bounds.x + 12.0f, bounds.y + 45.0f,
                        bounds.width - 24.0f, bounds.height - 58.0f},
                       15.0f, kMuted);
        return;
    }

    const std::array<Color, 6> colors = {
        kGreen, kBlue, kGold, kRed, kPurple, kOrange
    };
    const float radius = std::min(
        58.0f, std::max(8.0f,
            std::min(bounds.width * 0.17f, bounds.height * 0.24f)));
    const Vector2 center = {
        bounds.x + bounds.width * 0.23f,
        bounds.y + bounds.height * 0.54f
    };
    float angle = -90.0f;
    for (std::size_t index = 0; index < values.size(); ++index) {
        const float next = angle + static_cast<float>(
            360.0 * (values[index].second / total).toDouble());
        DrawCircleSector(center, radius, angle, next, 28,
                         colors[index % colors.size()]);
        angle = next;
    }
    DrawCircleLines(static_cast<int>(center.x),
                    static_cast<int>(center.y), radius, kBorder);

    const float legendX = bounds.x + bounds.width * 0.46f;
    const float legendY = bounds.y + 39.0f;
    const float rowHeight = std::min(
        24.0f, std::max(18.0f,
            (bounds.height - 50.0f) /
            static_cast<float>(std::max<std::size_t>(1, values.size()))));
    for (std::size_t index = 0; index < values.size(); ++index) {
        const float y = legendY + static_cast<float>(index) * rowHeight;
        DrawRectangle(static_cast<int>(legendX),
                      static_cast<int>(y + 5.0f), 9, 9,
                      colors[index % colors.size()]);
        const double share =
            (values[index].second / total).toDouble() * 100.0;
        char shareText[24];
        std::snprintf(shareText, sizeof(shareText), "%.1f%%", share);
        DrawFittedText(font,
                       values[index].first + " " + shareText,
                       {legendX + 14.0f, y,
                        bounds.x + bounds.width - legendX - 20.0f,
                        rowHeight},
                       13.0f, kMuted);
    }
}

void DrawSupplyDemandBar(Rectangle bounds, Money supply, Money demand) {
    const Rectangle track = {
        bounds.x + 10.0f,
        bounds.y + bounds.height * 0.5f - 4.0f,
        std::max(8.0f, bounds.width - 30.0f),
        8.0f
    };
    DrawRectangleRec(track, Color{216, 223, 219, 255});
    DrawRectangleLinesEx(track, 1.0f, Color{184, 195, 189, 255});

    const double supplyValue = std::max(0.0, supply.toDouble());
    const double demandValue = std::max(0.0, demand.toDouble());
    const double scale = std::max(supplyValue, demandValue);
    if (scale > 1.0e-9) {
        const float ratio = static_cast<float>(std::clamp(
            std::fabs(supplyValue - demandValue) / scale, 0.0, 1.0));
        const float centerX = track.x + track.width * 0.5f;
        const float fillWidth = track.width * 0.5f * ratio;
        if (supplyValue > demandValue + 1.0e-9) {
            DrawRectangleRec(
                {centerX, track.y + 1.0f, fillWidth, track.height - 2.0f},
                kOrange);
        } else if (demandValue > supplyValue + 1.0e-9) {
            DrawRectangleRec(
                {centerX - fillWidth, track.y + 1.0f,
                 fillWidth, track.height - 2.0f},
                kBlue);
        }
    }
    const float centerX = track.x + track.width * 0.5f;
    DrawLineEx({centerX, track.y - 2.0f},
               {centerX, track.y + track.height + 2.0f},
               1.0f, Color{93, 108, 101, 255});
}

void DrawCommodityList(const UIState* state, World& world, Font font) {
    const GoodsPageLayout layout = MakeGoodsPageLayout();
    const Country& country =
        world.getCountryById(state->playerCountryId);
    const CountrySnapshot countrySnapshot =
        world.getCountrySnapshot(state->playerCountryId);
    const auto& marketIds =
        country.getNationalMarket().getLocalMarketIds();
    const int scroll = std::clamp(state->goodsScroll, 0, layout.maxScroll);
    DrawPageHeader(
        font, layout, countrySnapshot.name + "商品市场",
        "第 " + std::to_string(countrySnapshot.cycle) + " 周 · " +
        std::to_string(marketIds.size()) + " 个市场");

    const std::array<float, 8> columns = {
        0.00f, 0.20f, 0.34f, 0.46f, 0.59f, 0.72f, 0.84f, 1.00f
    };
    const char* headings[7] = {
        "商品", "当前价格", "溢价", "购买量", "供应量", "供应差值",
        "供需"
    };
    DrawRectangleRec(layout.tableHeader, Color{222, 228, 224, 255});
    for (int column = 0; column < 7; ++column) {
        DrawFittedText(font, headings[column],
                       {layout.tableHeader.x +
                            layout.tableHeader.width * columns[column],
                        layout.tableHeader.y,
                        layout.tableHeader.width *
                            (columns[column + 1] - columns[column]),
                        layout.tableHeader.height},
                       15.0f, kMuted, 10.0f);
    }

    const Vector2 mouse = GetMousePosition();
    for (int visible = 0; visible < layout.visibleRows; ++visible) {
        const int good = scroll + visible;
        if (good >= NUM_GOODS) break;
        const Rectangle row = {
            layout.content.x,
            layout.firstRowY + visible * layout.rowHeight,
            layout.content.width,
            layout.rowHeight
        };
        const bool hovered = CheckCollisionPointRec(mouse, row);
        DrawRectangleRec(row, hovered
            ? Color{229, 238, 232, 255}
            : (good % 2 == 0 ? kSurface
                             : Color{246, 248, 245, 255}));
        DrawLineEx({row.x, row.y + row.height},
                   {row.x + row.width, row.y + row.height},
                   1.0f, kBorder);

        const CommodityMetrics metrics =
            CountryCommodityMetrics(world, country, good);
        const double premium = referencePrice[good] > 0.0
            ? (metrics.price - referencePrice[good]) /
                  referencePrice[good]
            : 0.0;
        const Money difference = metrics.supply - metrics.purchases;
        const std::array<std::string, 6> cells = {
            commodityNames[good],
            NumberText(metrics.price, 2),
            PercentText(premium),
            NumberText(metrics.purchases),
            NumberText(metrics.supply),
            NumberText(difference)
        };
        for (int column = 0; column < 6; ++column) {
            Color color = kText;
            if (column == 2)
                color = premium > 1.0e-9 ? kRed :
                        premium < -1.0e-9 ? kGreen : kMuted;
            if (column == 5)
                color = difference > Money(1.0e-9) ? kOrange :
                        difference < Money(-1.0e-9) ? kBlue : kMuted;
            DrawFittedText(font, cells[column],
                           {row.x + row.width * columns[column],
                            row.y,
                            row.width *
                                (columns[column + 1] - columns[column]),
                            row.height},
                           column == 0 ? 18.0f : 16.0f,
                           color, 10.0f);
        }
        DrawSupplyDemandBar(
            {row.x + row.width * columns[6], row.y,
             row.width * (columns[7] - columns[6]), row.height},
            metrics.supply, metrics.purchases);
        DrawTriangle({row.x + row.width - 10.0f, row.y + row.height * 0.5f},
                     {row.x + row.width - 17.0f, row.y + row.height * 0.5f - 6.0f},
                     {row.x + row.width - 17.0f, row.y + row.height * 0.5f + 6.0f},
                     hovered ? kGreen : kBorder);
    }

    if (layout.maxScroll > 0) {
        const float trackHeight =
            layout.visibleRows * layout.rowHeight - 8.0f;
        const Rectangle track = {
            layout.content.x + layout.content.width + 8.0f,
            layout.firstRowY + 4.0f, 4.0f, trackHeight
        };
        DrawRectangleRec(track, Color{215, 222, 218, 255});
        const float thumbHeight = std::max(
            32.0f, trackHeight *
                static_cast<float>(layout.visibleRows) / NUM_GOODS);
        const float progress = layout.maxScroll > 0
            ? static_cast<float>(scroll) / layout.maxScroll : 0.0f;
        DrawRectangleRec({track.x,
                          track.y + progress * (track.height - thumbHeight),
                          track.width, thumbHeight}, kGreen);
    }
}

void DrawCommodityDetail(const UIState* state, World& world, Font font) {
    const GoodsPageLayout layout = MakeGoodsPageLayout();
    const Country& country =
        world.getCountryById(state->playerCountryId);
    const CountrySnapshot countrySnapshot =
        world.getCountrySnapshot(state->playerCountryId);
    const int good = std::clamp(state->selectedGood, 0, NUM_GOODS - 1);
    const CommodityMetrics metrics =
        CountryCommodityMetrics(world, country, good);
    const double premium = referencePrice[good] > 0.0
        ? (metrics.price - referencePrice[good]) / referencePrice[good]
        : 0.0;
    const Money difference = metrics.supply - metrics.purchases;
    DrawPageHeader(font, layout, commodityNames[good],
                   countrySnapshot.name + "汇总 · 第 " +
                   std::to_string(countrySnapshot.cycle) + " 周");

    const Rectangle panel = layout.panel;
    const float margin = std::clamp(
        panel.width * 0.02f, 12.0f, 18.0f);
    const float contentX = panel.x + margin;
    const float contentWidth = std::max(1.0f, panel.width - margin * 2.0f);
    const float metricY = panel.y + 82.0f;
    const float metricWidth = contentWidth / 5.0f;
    const std::array<std::string, 5> labels = {
        "当前价格", "溢价", "购买量", "供应量", "供应差值"
    };
    const std::array<std::string, 5> values = {
        NumberText(metrics.price, 2), PercentText(premium),
        NumberText(metrics.purchases), NumberText(metrics.supply),
        NumberText(difference)
    };
    for (int index = 0; index < 5; ++index) {
        const Rectangle metric = {
            contentX + index * metricWidth, metricY,
            metricWidth, 54.0f
        };
        if (index > 0) {
            DrawLineEx({metric.x, metric.y + 4.0f},
                       {metric.x, metric.y + metric.height - 4.0f},
                       1.0f, kBorder);
        }
        Color color = kText;
        if (index == 1)
            color = premium > 1.0e-9 ? kRed :
                    premium < -1.0e-9 ? kGreen : kMuted;
        if (index == 4)
            color = difference > Money(1.0e-9) ? kGreen :
                    difference < Money(-1.0e-9) ? kRed : kMuted;
        DrawMetric(font, metric, labels[index], values[index], color);
    }

    const std::vector<double> priceHistory =
        CountryPriceHistory(world, country, good);
    auto [supplyHistory, demandHistory] =
        CountryFlowHistory(world, country, good);
    const float chartGap = 10.0f;
    const float pieGap = 10.0f;
    const float chartY = panel.y + 151.0f;
    const float availableHeight = std::max(
        1.0f, panel.y + panel.height - chartY - margin - pieGap);
    float chartHeight = std::clamp(
        availableHeight * 0.43f, 120.0f, 280.0f);
    chartHeight = std::min(
        chartHeight, std::max(1.0f, availableHeight - 110.0f));
    const float chartWidth =
        (contentWidth - chartGap * 2.0f) / 3.0f;
    const std::size_t recentStart = priceHistory.size() > 200
        ? priceHistory.size() - 200 : 0;
    DrawLineChart(font,
                  {contentX, chartY, chartWidth, chartHeight},
                  "最近 200 周价格",
                  {{"全国均价", &priceHistory, kRed}},
                  recentStart, false);
    DrawLineChart(font,
                  {contentX + chartWidth + chartGap, chartY,
                   chartWidth, chartHeight},
                  "全国价格（全部周期）",
                  {{"全国均价", &priceHistory, kBlue}},
                  0, false);
    DrawLineChart(font,
                  {contentX + (chartWidth + chartGap) * 2.0f,
                   chartY, chartWidth, chartHeight},
                  "商品供需",
                  {{"供应", &supplyHistory, kGreen},
                   {"购买", &demandHistory, kOrange}},
                  0, true);

    std::vector<std::pair<std::string, Money>> origins;
    std::vector<std::pair<std::string, Money>> sales;
    Money resident = Money(0);
    Money production = Money(0);
    Money construction = Money(0);
    std::array<Money, CLASS_COUNT> classes{};
    for (const int provinceId : country.getProvinceIds()) {
        const Province& province = world.getProvinceById(provinceId);
        const LocalMarket& market = province.getLocalMarket();
        const MarketFlowSnapshot& flow = market.getLatestFlow();
        const Money supplied = flow.production[good];
        const Money purchased = FlowPurchases(flow, good);
        if (supplied > Money(0))
            origins.push_back({province.getName(), supplied});
        if (purchased > Money(0))
            sales.push_back({province.getName(), purchased});
        resident += flow.consumerUse[good];
        production += flow.buildingConsumed[good] +
                      flow.directProductionUse[good];
        construction += flow.constructionUse[good];
        const auto& classConsumption =
            market.getLatestClassConsumerActual();
        for (int classIndex = 0; classIndex < CLASS_COUNT; ++classIndex)
            classes[classIndex] += classConsumption[classIndex][good];
    }

    const std::vector<std::pair<std::string, Money>> consumerTypes = {
        {"居民消费", resident},
        {"生产投入", production},
        {"建造部门", construction}
    };
    const std::vector<std::pair<std::string, Money>> consumerClasses = {
        {"劳工", classes[LABORER]},
        {"工程师", classes[ENGINEER]},
        {"资本家", classes[CAPITALIST]}
    };
    const float pieY = chartY + chartHeight + 14.0f;
    const float pieHeight = std::max(
        1.0f, panel.y + panel.height - pieY - margin);
    const float pieWidth =
        (contentWidth - pieGap * 3.0f) / 4.0f;
    DrawPieChart(font, {contentX, pieY, pieWidth, pieHeight},
                 "商品产地", origins);
    DrawPieChart(font,
                 {contentX + pieWidth + pieGap, pieY,
                  pieWidth, pieHeight},
                 "商品销售地", sales);
    DrawPieChart(font,
                 {contentX + (pieWidth + pieGap) * 2.0f,
                  pieY, pieWidth, pieHeight},
                 "消费方类型占比", consumerTypes);
    DrawPieChart(font,
                 {contentX + (pieWidth + pieGap) * 3.0f,
                  pieY, pieWidth, pieHeight},
                 "消费者阶级分布", consumerClasses);
}

}  // namespace

void HandleCommodityMarketInput(UIState* state, World& world) {
    (void)world;
    if (state->playerCountryId < 0) {
        state->view = UIView::WorldMap;
        state->selectedGood = -1;
        state->panelConsumesInput = false;
        return;
    }
    const GoodsPageLayout layout = MakeGoodsPageLayout();
    const Vector2 mouse = GetMousePosition();
    const bool insidePanel =
        CheckCollisionPointRec(mouse, layout.panel);
    state->panelConsumesInput = insidePanel;
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsKeyPressed(KEY_ESCAPE) ||
        (left && CheckCollisionPointRec(mouse, layout.backButton))) {
        NavigateBack(state);
        return;
    }
    if (!insidePanel) return;

    if (state->selectedGood >= 0) return;
    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        const int direction = wheel > 0.0f ? -1 : 1;
        state->goodsScroll = std::clamp(
            state->goodsScroll + direction * 3,
            0, layout.maxScroll);
    }
    if (IsKeyPressed(KEY_UP))
        state->goodsScroll = std::max(0, state->goodsScroll - 1);
    if (IsKeyPressed(KEY_DOWN))
        state->goodsScroll =
            std::min(layout.maxScroll, state->goodsScroll + 1);

    if (!left) return;
    for (int visible = 0; visible < layout.visibleRows; ++visible) {
        const int good = state->goodsScroll + visible;
        if (good >= NUM_GOODS) break;
        const Rectangle row = {
            layout.content.x,
            layout.firstRowY + visible * layout.rowHeight,
            layout.content.width,
            layout.rowHeight
        };
        if (CheckCollisionPointRec(mouse, row)) {
            state->selectedGood = good;
            return;
        }
    }
}

void DrawCommodityMarketUI(const UIState* state, World& world, Font font) {
    if (state->playerCountryId < 0) return;
    const GoodsPageLayout layout = MakeGoodsPageLayout();
    gGoodsPanelClip = layout.panel;
    gGoodsPanelClipActive = true;
    BeginScissorMode(static_cast<int>(layout.panel.x),
                     static_cast<int>(layout.panel.y),
                     std::max(1, static_cast<int>(layout.panel.width)),
                     std::max(1, static_cast<int>(layout.panel.height)));
    DrawRectangleRec(layout.panel, kPageBackground);
    if (state->selectedGood >= 0)
        DrawCommodityDetail(state, world, font);
    else
        DrawCommodityList(state, world, font);
    gGoodsPanelClipActive = false;
    EndScissorMode();
    DrawRectangleLinesEx(layout.panel, 1.0f, {45, 62, 58, 255});
}
