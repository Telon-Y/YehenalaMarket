#include "ui_internal.h"
#include "number_format.h"

#include "map_model.h"
#include "ui_map_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr float kTransportHeaderFontSize = 15.0f;
constexpr float kTransportRowFontSize = 16.0f;
constexpr float kTransportRowHeight = 28.0f;

struct TransportGeometry {
    Rectangle routeMap;
    Rectangle inspector;
    Rectangle batches;
    Rectangle orders;
};

TransportGeometry Geometry() {
    const float width = static_cast<float>(GetScreenWidth());
    const float height = static_cast<float>(GetScreenHeight());
    const float left = 44.0f;
    const float right = 22.0f;
    const float gap = 18.0f;
    const float available = std::max(420.0f, width - left - right - gap);
    const float mapWidth = std::clamp(available * 0.62f, 440.0f,
                                      std::max(440.0f, available - 320.0f));
    const float inspectorWidth = std::max(280.0f, available - mapWidth);
    const float mapY = 248.0f;
    const float mapHeight = std::clamp(height * 0.29f, 190.0f, 282.0f);
    const float tableY = mapY + mapHeight + 16.0f;
    const float batchHeight = std::clamp(height * 0.155f, 112.0f, 158.0f);
    return {{left, mapY, mapWidth, mapHeight},
            {left + mapWidth + gap, mapY, inspectorWidth, height - mapY - 22.0f},
            {left, tableY, mapWidth, batchHeight},
            {left, tableY + batchHeight + 14.0f, mapWidth,
             std::max(88.0f, height - tableY - batchHeight - 36.0f)}};
}

int VisibleTableRows(const Rectangle& table) {
    return std::max(
        1, static_cast<int>((table.height - 61.0f) /
                            kTransportRowHeight));
}

int ClampTableScroll(int scroll, std::size_t rowCount, int visibleRows) {
    const int maxScroll =
        std::max(0, static_cast<int>(rowCount) - visibleRows);
    return std::clamp(scroll, 0, maxScroll);
}

struct TransportRows {
    std::vector<const RouteSnapshot*> routes;
    std::vector<const WarehouseOrderSnapshot*> orders;
    std::vector<const ShipmentSnapshot*> shipments;
    std::vector<const InventoryReviewDecisionSnapshot*> inventoryReviews;
};

TransportRows RowsForCountry(const Country& country, const World& world,
                             const TransportationSnapshot& snapshot) {
    std::unordered_set<int> marketIds;
    for (const int provinceId : country.getProvinceIds())
        marketIds.insert(world.getProvinceById(provinceId).getLocalMarketId());
    const auto belongs = [&marketIds](int warehouseId) {
        return marketIds.find(warehouseId) != marketIds.end();
    };

    TransportRows rows;
    for (const RouteSnapshot& route : snapshot.routes) {
        if (belongs(route.sourceWarehouseId) ||
            belongs(route.destinationWarehouseId))
            rows.routes.push_back(&route);
    }
    for (const WarehouseOrderSnapshot& order : snapshot.orders) {
        if (belongs(order.buyerWarehouseId) ||
            belongs(order.sellerWarehouseId))
            rows.orders.push_back(&order);
    }
    for (const ShipmentSnapshot& shipment : snapshot.shipments) {
        if (belongs(shipment.sourceWarehouseId) ||
            belongs(shipment.destinationWarehouseId))
            rows.shipments.push_back(&shipment);
    }
    for (const InventoryReviewDecisionSnapshot& review :
         snapshot.inventoryReviews) {
        if (belongs(review.warehouseId))
            rows.inventoryReviews.push_back(&review);
    }
    std::sort(rows.routes.begin(), rows.routes.end(),
              [](const RouteSnapshot* left, const RouteSnapshot* right) {
                  return left->id < right->id;
              });
    std::sort(rows.orders.begin(), rows.orders.end(),
              [](const WarehouseOrderSnapshot* left,
                 const WarehouseOrderSnapshot* right) {
                  return left->id < right->id;
              });
    std::sort(rows.shipments.begin(), rows.shipments.end(),
              [](const ShipmentSnapshot* left,
                 const ShipmentSnapshot* right) {
                  return left->id < right->id;
              });
    return rows;
}
const Country* FindCountryById(const World& world, int countryId) {
    for (int index = 0; index < world.getCountryCount(); ++index) {
        const Country& candidate = world.getCountry(index);
        if (candidate.getId() == countryId) return &candidate;
    }
    return nullptr;
}


map_model::MapView RouteMapView(const Rectangle& panel) {
    return map_model::FitWorldView(
        {panel.x, panel.y, panel.width, panel.height});
}

bool WarehousePoint(int warehouseId, const ui_map::BoundMapData& data,
                    const World& world, const map_model::MapView& view,
                    Vector2* result) {
    if (result == nullptr) return false;
    map_model::Point worldPoint;
    if (!ui_map::TryWarehouseWorldPoint(
            warehouseId, data, world, &worldPoint)) {
        return false;
    }
    const map_model::Point screen =
        map_model::WorldToScreen(view, worldPoint);
    *result = {screen.x, screen.y};
    return true;
}

using RouteSegment = std::pair<Vector2, Vector2>;

std::vector<RouteSegment> RouteSegments(
    const RouteSnapshot& route, const ui_map::BoundMapData& data,
    const World& world, const map_model::MapView& view) {
    map_model::Point source;
    map_model::Point destination;
    if (!ui_map::TryWarehouseWorldPoint(
            route.sourceWarehouseId, data, world, &source) ||
        !ui_map::TryWarehouseWorldPoint(
            route.destinationWarehouseId, data, world, &destination)) {
        return {};
    }
    const float deltaX = destination.x - source.x;
    if (deltaX > view.worldWidth * 0.5f)
        destination.x -= view.worldWidth;
    else if (deltaX < -view.worldWidth * 0.5f)
        destination.x += view.worldWidth;

    std::vector<RouteSegment> segments;
    for (const map_model::LoopCopy copy :
         map_model::VisibleLoopCopies(view)) {
        const map_model::Point start =
            map_model::WorldToScreen(view, source, copy.repeatIndex);
        const map_model::Point end =
            map_model::WorldToScreen(view, destination, copy.repeatIndex);
        const float left = view.viewport.x;
        const float right = left + view.viewport.width;
        if ((start.x < left && end.x < left) ||
            (start.x > right && end.x > right)) {
            continue;
        }
        segments.push_back({
            {start.x, start.y}, {end.x, end.y}});
    }
    return segments;
}

std::string WarehouseName(int warehouseId, const World& world) {
    for (int provinceId = 0; provinceId < world.getProvinceCount(); ++provinceId) {
        const Province& province = world.getProvince(provinceId);
        if (province.getLocalMarketId() == warehouseId)
            return province.getName();
    }
    return "#" + std::to_string(warehouseId);
}

const char* StatusLabel(WarehouseOrderStatus status) {
    switch (status) {
    case WarehouseOrderStatus::PendingLocalAllocation: return "本地待处理";
    case WarehouseOrderStatus::WaitingForRoute: return "等待路线";
    case WarehouseOrderStatus::AwaitingSupply: return "等待供应";
    case WarehouseOrderStatus::Confirmed: return "已确认";
    case WarehouseOrderStatus::PartiallyConfirmed: return "部分确认";
    case WarehouseOrderStatus::InTransit: return "运输中";
    case WarehouseOrderStatus::PartiallyFulfilled: return "部分收货";
    case WarehouseOrderStatus::Fulfilled: return "已完成";
    case WarehouseOrderStatus::Cancelled: return "已取消";
    }
    return "未知";
}

const char* KindLabel(WarehouseOrderKind kind) {
    switch (kind) {
    case WarehouseOrderKind::BuildingMaterialDemand: return "根需求";
    case WarehouseOrderKind::WarehouseReplenishment: return "仓库补货";
    case WarehouseOrderKind::RemotePurchase: return "远程采购";
    case WarehouseOrderKind::SupplierProduction: return "生产";
    }
    return "订单";
}

const char* ReviewReasonLabel(InventoryReviewReason reason) {
    switch (reason) {
    case InventoryReviewReason::StockSufficient: return "库存充足";
    case InventoryReviewReason::RequestSmoothedToZero:
        return "平滑至零";
    case InventoryReviewReason::NewOrderCreated: return "新订单";
    case InventoryReviewReason::ExistingOrderUpdated:
        return "已有订单";
    }
    return "已检查";
}

Color StatusColor(WarehouseOrderStatus status) {
    switch (status) {
    case WarehouseOrderStatus::Fulfilled: return {63, 137, 94, 255};
    case WarehouseOrderStatus::InTransit:
    case WarehouseOrderStatus::PartiallyFulfilled: return {49, 111, 164, 255};
    case WarehouseOrderStatus::AwaitingSupply:
    case WarehouseOrderStatus::WaitingForRoute: return {185, 113, 45, 255};
    case WarehouseOrderStatus::Cancelled: return {152, 71, 63, 255};
    default: return {81, 91, 90, 255};
    }
}

const char* GoodLabel(int goodIndex) {
    if (goodIndex < 0 || goodIndex >= NUM_GOODS) return "未知商品";
    return commodityNames[goodIndex].c_str();
}

const RouteSnapshot* FindRoute(const TransportRows& rows, int routeId) {
    for (const RouteSnapshot* route : rows.routes)
        if (route->id == routeId) return route;
    return nullptr;
}

const WarehouseOrderSnapshot* FindOrder(const TransportRows& rows,
                                        WarehouseOrderId orderId) {
    for (const WarehouseOrderSnapshot* order : rows.orders)
        if (order->id == orderId) return order;
    return nullptr;
}

const InventoryReviewDecisionSnapshot* FindReview(
    const TransportRows& rows, int warehouseId, int goodIndex) {
    for (const InventoryReviewDecisionSnapshot* review :
         rows.inventoryReviews) {
        if (review->warehouseId == warehouseId &&
            review->goodIndex == goodIndex) {
            return review;
        }
    }
    return nullptr;
}

const WarehouseOrderSnapshot* OrderForShipment(
    const TransportRows& rows, const ShipmentSnapshot& shipment) {
    return FindOrder(rows, shipment.orderId);
}

float DistanceToSegment(Vector2 point, Vector2 start, Vector2 end) {
    const Vector2 delta = {end.x - start.x, end.y - start.y};
    const float lengthSquared = delta.x * delta.x + delta.y * delta.y;
    if (lengthSquared <= 0.001f)
        return std::sqrt((point.x - start.x) * (point.x - start.x) +
                         (point.y - start.y) * (point.y - start.y));
    const float t = std::clamp(((point.x - start.x) * delta.x +
                                (point.y - start.y) * delta.y) /
                                   lengthSquared,
                               0.0f, 1.0f);
    const Vector2 projection = {start.x + t * delta.x, start.y + t * delta.y};
    return std::sqrt((point.x - projection.x) * (point.x - projection.x) +
                     (point.y - projection.y) * (point.y - projection.y));
}

void DrawPanel(Rectangle panel, Color fill, Color border) {
    DrawRectangleRec(panel, fill);
    DrawRectangleLinesEx(panel, 1.0f, border);
}

void DrawRouteMap(const UIState* state, const Country& country, World& world,
                  const TransportRows& rows,
                  const TransportGeometry& geometry, Font font) {
    DrawPanel(geometry.routeMap, {231, 238, 235, 255},
              {156, 171, 167, 255});
    DrawTextEx(font, "路线地图",
               {geometry.routeMap.x + 16, geometry.routeMap.y + 12},
               21, 0, {32, 53, 51, 255});
    DrawTextEx(
        font,
        TextFormat("活动路线 %d 条",
                   static_cast<int>(rows.routes.size())),
        {geometry.routeMap.x + geometry.routeMap.width - 150,
         geometry.routeMap.y + 14},
        17, 0, {83, 99, 96, 255});

    const Rectangle plot = {
        geometry.routeMap.x + 12, geometry.routeMap.y + 42,
        geometry.routeMap.width - 24, geometry.routeMap.height - 54};
    ui_map::BoundMapData& data = ui_map::GetMapData(world);
    const map_model::MapView view = RouteMapView(plot);
    const std::vector<map_model::LoopCopy> copies =
        map_model::VisibleLoopCopies(view);
    const Rectangle mapBounds = {
        view.viewport.x, view.viewport.y,
        view.viewport.width, view.viewport.height};
    constexpr Color water{205, 224, 224, 255};
    DrawRectangleRec(plot, {218, 226, 223, 255});
    DrawRectangleRec(mapBounds, water);
    BeginScissorMode(
        static_cast<int>(view.viewport.x),
        static_cast<int>(view.viewport.y),
        std::max(1, static_cast<int>(view.viewport.width)),
        std::max(1, static_cast<int>(view.viewport.height)));
    ui_map::DrawMapGrid(view);
    ui_map::DrawWorldBasemap(data, view, copies);

    for (const map_model::LoopCopy copy : copies) {
        for (std::size_t shapeIndex = 0;
             shapeIndex < data.shapes.size(); ++shapeIndex) {
            const map_layout::ProvinceShape& shape =
                data.shapes[shapeIndex];
            if (shape.provinceId < 0) continue;
            const Province& province =
                world.getProvinceById(shape.provinceId);
            const bool belongs =
                province.getCountryId() == country.getId();
            const Color fill = belongs
                ? Color{145, 181, 160, 235}
                : Color{202, 211, 205, 215};
            const ui_map::CachedProvinceShape& cached =
                data.provinceRender[shapeIndex];
            for (const ui_map::CachedWorldPolygon& part : cached.parts) {
                ui_map::DrawCachedPolygonFill(
                    part, view, copy.repeatIndex, fill);
                ui_map::DrawCachedPolygonOutline(
                    part, view, copy.repeatIndex,
                    belongs ? 1.1f : 0.55f,
                    belongs ? Color{66, 102, 90, 220}
                            : Color{126, 143, 136, 150});
            }
        }
    }
    for (const map_model::LoopCopy copy : copies) {
        for (const ui_map::CachedProvinceShape& shape :
             data.provinceRender) {
            for (const ui_map::CachedWorldPolygon& hole : shape.holes) {
                ui_map::DrawCachedPolygonFill(
                    hole, view, copy.repeatIndex, water);
            }
        }
    }

    for (const RouteSnapshot* route : rows.routes) {
        const bool selected =
            route->id == state->selectedTransportRouteId;
        const Color line = selected
            ? Color{213, 156, 49, 255}
            : (!route->railwayAvailable
                   ? Color{152, 71, 63, 215}
                   : (!route->profitable
                          ? Color{185, 113, 45, 215}
                          : Color{45, 103, 111, 220}));
        for (const RouteSegment& segment :
             RouteSegments(*route, data, world, view)) {
            const Vector2 start = segment.first;
            const Vector2 end = segment.second;
            DrawLineEx(start, end, selected ? 3.0f : 1.5f, line);
            const Vector2 direction = {
                end.x - start.x, end.y - start.y};
            const float length = std::sqrt(
                direction.x * direction.x +
                direction.y * direction.y);
            if (length > 12.0f) {
                const Vector2 unit = {
                    direction.x / length, direction.y / length};
                const Vector2 normal = {-unit.y, unit.x};
                const Vector2 tip = {
                    end.x - unit.x * 6.0f,
                    end.y - unit.y * 6.0f};
                DrawTriangle(
                    tip,
                    {tip.x - unit.x * 10.0f + normal.x * 4.0f,
                     tip.y - unit.y * 10.0f + normal.y * 4.0f},
                    {tip.x - unit.x * 10.0f - normal.x * 4.0f,
                     tip.y - unit.y * 10.0f - normal.y * 4.0f},
                    line);
            }
            DrawCircleV(start, selected ? 5.0f : 3.5f, line);
            DrawCircleV(end, selected ? 5.0f : 3.5f, line);
        }
    }

    int missingBindings = 0;
    for (const int provinceId : country.getProvinceIds()) {
        const int warehouseId =
            world.getProvinceById(provinceId).getLocalMarketId();
        Vector2 point;
        if (!WarehousePoint(
                warehouseId, data, world, view, &point)) {
            ++missingBindings;
            continue;
        }
        DrawCircleV(point, 4.0f, {40, 78, 69, 235});
        DrawCircleLines(
            static_cast<int>(point.x), static_cast<int>(point.y),
            5.5f, {236, 244, 239, 230});
    }
    EndScissorMode();
    DrawRectangleLinesEx(mapBounds, 1.0f, {129, 151, 143, 220});

    if (rows.routes.empty()) {
        DrawTextEx(font, "该国家尚未配置路线。",
                   {mapBounds.x + 20,
                    mapBounds.y + mapBounds.height * 0.5f},
                   16, 0, GRAY);
    }
    if (missingBindings > 0) {
        DrawTextEx(
            font,
            TextFormat("缺少 %d 个仓库绑定",
                       missingBindings),
            {plot.x + 8, plot.y + plot.height - 20},
            15, 0, Color{157, 71, 64, 255});
    }
}

void DrawBatchTable(const UIState* state, const TransportRows& rows,
                    const TransportGeometry& geometry, Font font) {
    DrawPanel(geometry.batches, {247, 250, 247, 255}, {174, 183, 179, 255});
    DrawTextEx(font, "运输批次", {geometry.batches.x + 14, geometry.batches.y + 10},
               20, 0, {33, 55, 53, 255});
    const float y0 = geometry.batches.y + 39;
    DrawTextEx(font, "批次 / 订单", {geometry.batches.x + 14, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "商品", {geometry.batches.x + geometry.batches.width * 0.30f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "货物", {geometry.batches.x + geometry.batches.width * 0.52f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "预计到达", {geometry.batches.x + geometry.batches.width * 0.72f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "状态", {geometry.batches.x + geometry.batches.width * 0.82f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    float y = y0 + 25.0f;
    const int maxRows = VisibleTableRows(geometry.batches);
    const int start = ClampTableScroll(
        state->transportShipmentScroll, rows.shipments.size(), maxRows);
    const int end = std::min(
        start + maxRows, static_cast<int>(rows.shipments.size()));
    const std::string range = rows.shipments.empty()
        ? "0 / 0"
        : std::to_string(start + 1) + "-" + std::to_string(end) + " / " +
              std::to_string(rows.shipments.size());
    const Vector2 rangeSize = MeasureTextEx(
        font, range.c_str(), kTransportHeaderFontSize, 0);
    DrawTextEx(font, range.c_str(),
               {geometry.batches.x + geometry.batches.width -
                    rangeSize.x - 14.0f, geometry.batches.y + 12.0f},
               kTransportHeaderFontSize, 0, GRAY);
    for (int index = start; index < end; ++index) {
        const ShipmentSnapshot* shipment =
            rows.shipments[static_cast<std::size_t>(index)];
        const bool selected = state->selectedTransportOrderId == shipment->orderId;
        if (selected)
            DrawRectangle(geometry.batches.x + 6, y - 3, geometry.batches.width - 12,
                           kTransportRowHeight - 1.0f,
                           {231, 238, 217, 255});
        DrawTextEx(font, TextFormat("#%llu / #%llu",
                                   static_cast<unsigned long long>(shipment->id),
                                   static_cast<unsigned long long>(shipment->orderId)),
                   {geometry.batches.x + 14, y},
                   kTransportRowFontSize, 0, {39, 53, 52, 255});
        DrawTextEx(font, GoodLabel(shipment->goodIndex),
                   {geometry.batches.x + geometry.batches.width * 0.30f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        const std::string cargo = FormatChineseNumber(shipment->cargo.toDouble());
        DrawTextEx(font, cargo.c_str(),
                   {geometry.batches.x + geometry.batches.width * 0.52f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        DrawTextEx(font, TextFormat("%d", shipment->remainingCycles),
                   {geometry.batches.x + geometry.batches.width * 0.70f, y},
                   kTransportRowFontSize, 0,
                   shipment->remainingCycles > 0 ? ORANGE : GREEN);
        const WarehouseOrderSnapshot* order = OrderForShipment(rows, *shipment);
        DrawTextEx(font, order == nullptr ? "运输批次" : StatusLabel(order->status),
                   {geometry.batches.x + geometry.batches.width * 0.82f, y},
                   kTransportRowFontSize, 0,
                   order == nullptr ? GRAY : StatusColor(order->status));
        y += kTransportRowHeight;
    }
    if (rows.shipments.empty())
        DrawTextEx(font, "暂无活动运输批次。", {geometry.batches.x + 14, y},
                   kTransportRowFontSize, 0, GRAY);
}

void DrawOrderTable(const UIState* state, const TransportRows& rows,
                    const TransportGeometry& geometry, Font font) {
    DrawPanel(geometry.orders, {247, 250, 247, 255}, {174, 183, 179, 255});
    DrawTextEx(font, "订单流程", {geometry.orders.x + 14, geometry.orders.y + 10},
               20, 0, {33, 55, 53, 255});
    const float y0 = geometry.orders.y + 39;
    DrawTextEx(font, "编号 / 根需求", {geometry.orders.x + 14, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "类型", {geometry.orders.x + geometry.orders.width * 0.27f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "商品", {geometry.orders.x + geometry.orders.width * 0.50f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "进度", {geometry.orders.x + geometry.orders.width * 0.66f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "路线", {geometry.orders.x + geometry.orders.width * 0.88f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    float y = y0 + 25.0f;
    const int maxRows = VisibleTableRows(geometry.orders);
    const int start = ClampTableScroll(
        state->transportOrderScroll, rows.orders.size(), maxRows);
    const int end = std::min(
        start + maxRows, static_cast<int>(rows.orders.size()));
    const std::string range = rows.orders.empty()
        ? "0 / 0"
        : std::to_string(start + 1) + "-" + std::to_string(end) + " / " +
              std::to_string(rows.orders.size());
    const Vector2 rangeSize = MeasureTextEx(
        font, range.c_str(), kTransportHeaderFontSize, 0);
    DrawTextEx(font, range.c_str(),
               {geometry.orders.x + geometry.orders.width -
                    rangeSize.x - 14.0f, geometry.orders.y + 12.0f},
               kTransportHeaderFontSize, 0, GRAY);
    for (int index = start; index < end; ++index) {
        const WarehouseOrderSnapshot* order =
            rows.orders[static_cast<std::size_t>(index)];
        const bool selected = state->selectedTransportOrderId == order->id;
        if (selected)
            DrawRectangle(geometry.orders.x + 6, y - 3, geometry.orders.width - 12,
                           kTransportRowHeight - 1.0f,
                           {231, 238, 217, 255});
        DrawTextEx(font, TextFormat("#%llu / #%llu",
                                   static_cast<unsigned long long>(order->id),
                                   static_cast<unsigned long long>(order->rootDemandId)),
                   {geometry.orders.x + 14, y},
                   kTransportRowFontSize, 0, {39, 53, 52, 255});
        DrawTextEx(font, KindLabel(order->kind),
                   {geometry.orders.x + geometry.orders.width * 0.27f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        DrawTextEx(font, GoodLabel(order->goodIndex),
                   {geometry.orders.x + geometry.orders.width * 0.50f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        const std::string progress =
            FormatChineseNumber(order->received.toDouble()) + " / " +
            FormatChineseNumber(order->requested.toDouble());
        DrawTextEx(font, progress.c_str(),
                   {geometry.orders.x + geometry.orders.width * 0.66f, y},
                   kTransportRowFontSize, 0,
                   StatusColor(order->status));
        DrawTextEx(font, TextFormat("%d", order->routeId),
                   {geometry.orders.x + geometry.orders.width * 0.88f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        y += kTransportRowHeight;
    }
    if (rows.orders.empty())
        DrawTextEx(font, "该国家暂无未完成订单。", {geometry.orders.x + 14, y},
                   kTransportRowFontSize, 0, GRAY);
}

void DrawInspector(const UIState* state, World& world,
                   const TransportRows& rows, const TransportGeometry& geometry,
                   Font font) {
    DrawPanel(geometry.inspector, {241, 246, 242, 255}, {157, 172, 167, 255});
    DrawTextEx(font, "路线检查", {geometry.inspector.x + 16,
                                          geometry.inspector.y + 14}, 21, 0,
               {32, 53, 51, 255});
    const RouteSnapshot* route = FindRoute(rows, state->selectedTransportRouteId);
    if (route == nullptr && !rows.routes.empty()) route = rows.routes.front();
    float y = geometry.inspector.y + 50.0f;
    if (route != nullptr) {
        const char* economics = !route->railwayAvailable
            ? "无铁路" : (route->profitable ? "盈利" : "无利润");
        const std::string routeHeading =
            "路线 #" + std::to_string(route->id) + "  " + economics;
        DrawTextEx(font, routeHeading.c_str(),
                   {geometry.inspector.x + 16, y}, 17, 0,
                   !route->railwayAvailable ? Color{152, 71, 63, 255} :
                   (route->profitable ? Color{42, 111, 82, 255}
                                      : Color{185, 113, 45, 255}));
        y += 28.0f;
        DrawTextEx(font, (WarehouseName(route->sourceWarehouseId, world) +
                          "  至  " + WarehouseName(route->destinationWarehouseId, world)).c_str(),
                   {geometry.inspector.x + 16, y}, 16, 0, {47, 62, 61, 255});
        y += 26.0f;
        const std::string goodLine =
            std::string("商品 ") + GoodLabel(route->goodIndex);
        DrawTextEx(font, goodLine.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string distanceLine =
            "距离 " + FormatChineseNumber(route->distanceKm) + " 千米   预计 " +
            FormatChineseNumber(route->transitCycles, 0) + " 周";
        DrawTextEx(font, distanceLine.c_str(),
                   {geometry.inspector.x + 16, y + 22}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string priceLine =
            "起点 " + FormatChineseNumber(route->sourceUnitPrice.toDouble(), 2) +
            "   终点 " + FormatChineseNumber(route->destinationUnitPrice.toDouble(), 2);
        DrawTextEx(font, priceLine.c_str(),
                   {geometry.inspector.x + 16, y + 44}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string capacityPriceLine =
            "运力价格 " + FormatChineseNumber(route->railwayCapacityPricePerUnit.toDouble(), 2) +
            "   货运费用 " + FormatChineseNumber(route->transportCostPerUnit.toDouble(), 2);
        DrawTextEx(font, capacityPriceLine.c_str(),
                   {geometry.inspector.x + 16, y + 66}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string unitCapacityLine =
            "单位货物运力 " + FormatChineseNumber(route->transportCapacityPerUnit.toDouble(), 4) +
            "   合同价 " + FormatChineseNumber(route->unitPrice.toDouble(), 2);
        DrawTextEx(font, unitCapacityLine.c_str(),
                   {geometry.inspector.x + 16, y + 88}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string revenueLine =
            "最近铁路收入 " + FormatChineseNumber(route->railwayRevenue.toDouble(), 2);
        DrawTextEx(font, revenueLine.c_str(),
                   {geometry.inspector.x + 16, y + 110}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string usedLine =
            "已用 " + FormatChineseNumber(route->usedCapacity.toDouble(), 2) + " / " +
            FormatChineseNumber(route->capacityPerCycle.toDouble(), 2) + "   排队 " +
            FormatChineseNumber(route->queuedVolume.toDouble(), 2);
        DrawTextEx(font, usedLine.c_str(),
                   {geometry.inspector.x + 16, y + 132}, kUiBodyFontSize, 0,
                   route->queuedVolume > Money(0) ? ORANGE : DARKGRAY);
        y += 168.0f;
    } else {
        DrawTextEx(font, "请在地图上选择路线以查看详情。",
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, GRAY);
        y += 34.0f;
    }

    const InventoryReviewDecisionSnapshot* review = nullptr;
    if (route != nullptr) {
        review = FindReview(rows, route->destinationWarehouseId,
                            route->goodIndex);
        if (review == nullptr) {
            review = FindReview(rows, route->sourceWarehouseId,
                                route->goodIndex);
        }
    }
    if (review == nullptr && !rows.inventoryReviews.empty())
        review = rows.inventoryReviews.front();

    DrawLine(static_cast<int>(geometry.inspector.x + 14), static_cast<int>(y),
             static_cast<int>(geometry.inspector.x + geometry.inspector.width - 14),
             static_cast<int>(y), {179, 190, 186, 255});
    y += 18.0f;
    DrawTextEx(font, "库存检查", {geometry.inspector.x + 16, y},
               19, 0, {32, 53, 51, 255});
    y += 27.0f;
    if (review != nullptr) {
        const double coverage = review->averageDemand > Money(1e-9)
            ? (review->onHand / review->averageDemand).toDouble() : 0.0;
        DrawTextEx(font,
                   TextFormat("%s  %s  周期 %d  %s",
                              WarehouseName(review->warehouseId, world).c_str(),
                              GoodLabel(review->goodIndex), review->cycle,
                              ReviewReasonLabel(review->reason)),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   {47, 62, 61, 255});
        y += 21.0f;
        const std::string stockLine =
            "现有 " + FormatChineseNumber(review->onHand.toDouble(), 2) +
            "  可用 " + FormatChineseNumber(review->available.toDouble(), 2) +
            "  已预留 " + FormatChineseNumber(review->reserved.toDouble(), 2);
        DrawTextEx(font, stockLine.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        const std::string demandLine =
            "需求 " + FormatChineseNumber(review->averageDemand.toDouble(), 2) +
            "  可覆盖 " + FormatChineseNumber(coverage) + " 周  目标 " +
            FormatChineseNumber(review->targetStock.toDouble(), 2);
        DrawTextEx(font, demandLine.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        const std::string reorderLine =
            "再订货 " + FormatChineseNumber(review->reorderPoint.toDouble(), 2) +
            "  库存位置 " + FormatChineseNumber(review->inventoryPosition.toDouble(), 2) +
            "  缺口 " + FormatChineseNumber(review->rawGap.toDouble(), 2);
        DrawTextEx(font, reorderLine.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        const std::string requestLine =
            "请求 " + FormatChineseNumber(review->plannedRequest.toDouble(), 2) +
            "  已确认 " + FormatChineseNumber(review->confirmedInbound.toDouble(), 2) +
            "  在途 " + FormatChineseNumber(review->physicalInTransit.toDouble(), 2);
        DrawTextEx(font, requestLine.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   review->rawGap > Money(0) ? ORANGE : DARKGRAY);
        y += 27.0f;
    } else {
        DrawTextEx(font, "暂无已完成的库存检查。",
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, GRAY);
        y += 27.0f;
    }
    if (y > geometry.inspector.y + geometry.inspector.height - 90.0f)
        return;
    DrawLine(static_cast<int>(geometry.inspector.x + 14), static_cast<int>(y),
             static_cast<int>(geometry.inspector.x + geometry.inspector.width - 14),
             static_cast<int>(y), {179, 190, 186, 255});
    y += 18.0f;
    DrawTextEx(font, "订单溯源", {geometry.inspector.x + 16, y}, 19, 0,
               {32, 53, 51, 255});
    y += 27.0f;
    const WarehouseOrderSnapshot* selected = FindOrder(
        rows, state->selectedTransportOrderId);
    if (selected == nullptr && route != nullptr) {
        for (const WarehouseOrderSnapshot* order : rows.orders) {
            if (order->routeId == route->id) {
                selected = order;
                break;
            }
        }
    }
    if (selected == nullptr) {
        DrawTextEx(font, "请选择批次或订单行。",
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, GRAY);
        return;
    }
    DrawTextEx(font, TextFormat("#%llu  %s  %s",
                               static_cast<unsigned long long>(selected->id),
                               KindLabel(selected->kind),
                               StatusLabel(selected->status)),
               {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
               StatusColor(selected->status));
    y += 23.0f;
    const std::string traceRequest =
        "请求 " + FormatChineseNumber(selected->requested.toDouble(), 2) +
        "   已接受 " + FormatChineseNumber(selected->accepted.toDouble(), 2);
    DrawTextEx(font, traceRequest.c_str(),
               {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
    y += 20.0f;
    const std::string traceFlow =
        "已预留 " + FormatChineseNumber(selected->reserved.toDouble(), 2) +
        "   已发货 " + FormatChineseNumber(selected->shipped.toDouble(), 2) +
        "   已收货 " + FormatChineseNumber(selected->received.toDouble(), 2);
    DrawTextEx(font, traceFlow.c_str(),
               {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
    y += 28.0f;
    if (selected->routeId >= 0) {
        const std::string tracePrice =
            "起点 " + FormatChineseNumber(selected->sourceUnitPrice.toDouble(), 2) +
            " + 货运铁路 " + FormatChineseNumber(selected->railwayChargePerUnit.toDouble(), 2) +
            " = 合同 " + FormatChineseNumber(selected->contractPrice.toDouble(), 2);
        DrawTextEx(font, tracePrice.c_str(),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   selected->profitableTrade ? Color{42, 111, 82, 255}
                                             : Color{185, 113, 45, 255});
        y += 24.0f;
    }

    std::vector<const WarehouseOrderSnapshot*> lineage;
    const WarehouseOrderSnapshot* cursor = selected;
    for (int depth = 0; cursor != nullptr && depth < 6; ++depth) {
        lineage.push_back(cursor);
        if (cursor->parentOrderId == NO_WAREHOUSE_ORDER) break;
        cursor = FindOrder(rows, cursor->parentOrderId);
    }
    std::reverse(lineage.begin(), lineage.end());
    for (std::size_t index = 0; index < lineage.size(); ++index) {
        if (y > geometry.inspector.y + geometry.inspector.height - 28.0f) break;
        const WarehouseOrderSnapshot* item = lineage[index];
        const float indent = 12.0f + static_cast<float>(index) * 12.0f;
        DrawLine(static_cast<int>(geometry.inspector.x + indent - 7),
                 static_cast<int>(y + 8), static_cast<int>(geometry.inspector.x + indent),
                 static_cast<int>(y + 8), {145, 161, 155, 255});
        DrawTextEx(font, TextFormat("#%llu  %s", static_cast<unsigned long long>(item->id),
                                   KindLabel(item->kind)),
                   {geometry.inspector.x + indent, y}, kUiBodyFontSize, 0,
                   StatusColor(item->status));
        y += 22.0f;
    }
}

}  // namespace

void HandleTransportInput(UIState* state, World& world) {
    if (state == nullptr) return;
    const Country* countryPtr =
        FindCountryById(world, state->selectedCountryId);
    if (countryPtr == nullptr) {
        NavigateBack(state);
        return;
    }
    state->panelConsumesInput = true;
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const TransportGeometry geometry = Geometry();
    const Vector2 mouse = GetMousePosition();
    if (IsKeyPressed(KEY_ESCAPE) ||
        (left && CheckCollisionPointRec(
                     mouse, CurrentUILayout().countryBackButton))) {
        NavigateBack(state);
        return;
    }
    const Country& country = *countryPtr;
    const TransportationSnapshot snapshot =
        world.getTransportationSnapshot();
    const TransportRows rows =
        RowsForCountry(country, world, snapshot);
    const int batchRows = VisibleTableRows(geometry.batches);
    const int orderRows = VisibleTableRows(geometry.orders);
    state->transportShipmentScroll = ClampTableScroll(
        state->transportShipmentScroll, rows.shipments.size(), batchRows);
    state->transportOrderScroll = ClampTableScroll(
        state->transportOrderScroll, rows.orders.size(), orderRows);

    const float wheel = GetMouseWheelMove();
    if (wheel != 0.0f &&
        CheckCollisionPointRec(mouse, geometry.batches)) {
        state->transportShipmentScroll = ClampTableScroll(
            state->transportShipmentScroll + (wheel > 0.0f ? -1 : 1),
            rows.shipments.size(), batchRows);
        return;
    }
    if (wheel != 0.0f &&
        CheckCollisionPointRec(mouse, geometry.orders)) {
        state->transportOrderScroll = ClampTableScroll(
            state->transportOrderScroll + (wheel > 0.0f ? -1 : 1),
            rows.orders.size(), orderRows);
        return;
    }
    if (!left) return;

    const Rectangle plot = {
        geometry.routeMap.x + 12, geometry.routeMap.y + 42,
        geometry.routeMap.width - 24, geometry.routeMap.height - 54};
    const map_model::MapView view = RouteMapView(plot);
    ui_map::BoundMapData& data = ui_map::GetMapData(world);
    if (CheckCollisionPointRec(mouse, plot)) {
        float bestDistance = 12.0f;
        const RouteSnapshot* best = nullptr;
        for (const RouteSnapshot* route : rows.routes) {
            for (const RouteSegment& segment :
                 RouteSegments(*route, data, world, view)) {
                const float distance = DistanceToSegment(
                    mouse, segment.first, segment.second);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = route;
                }
            }
        }
        if (best != nullptr) {
            state->selectedTransportRouteId = best->id;
        }
        return;
    }
    Rectangle rowRect = {geometry.batches.x + 6,
                         geometry.batches.y + 61,
                         geometry.batches.width - 12,
                         kTransportRowHeight - 1.0f};
    for (int visible = 0; visible < batchRows; ++visible) {
        const int index = state->transportShipmentScroll + visible;
        if (index >= static_cast<int>(rows.shipments.size())) break;
        if (CheckCollisionPointRec(mouse, rowRect)) {
            const ShipmentSnapshot* shipment =
                rows.shipments[static_cast<std::size_t>(index)];
            state->selectedTransportOrderId = shipment->orderId;
            const WarehouseOrderSnapshot* order =
                OrderForShipment(rows, *shipment);
            if (order != nullptr && order->routeId >= 0)
                state->selectedTransportRouteId = order->routeId;
            return;
        }
        rowRect.y += kTransportRowHeight;
    }
    rowRect = {geometry.orders.x + 6, geometry.orders.y + 61,
               geometry.orders.width - 12,
               kTransportRowHeight - 1.0f};
    for (int visible = 0; visible < orderRows; ++visible) {
        const int index = state->transportOrderScroll + visible;
        if (index >= static_cast<int>(rows.orders.size())) break;
        if (CheckCollisionPointRec(mouse, rowRect)) {
            const WarehouseOrderSnapshot* order =
                rows.orders[static_cast<std::size_t>(index)];
            state->selectedTransportOrderId = order->id;
            const int routeId = order->routeId;
            if (routeId >= 0) state->selectedTransportRouteId = routeId;
            return;
        }
        rowRect.y += kTransportRowHeight;
    }
}

void DrawTransportUI(const UIState* state, World& world, Font font) {
    if (state == nullptr) return;
    const Country* countryPtr =
        FindCountryById(world, state->selectedCountryId);
    if (countryPtr == nullptr) return;
    const Country& country = *countryPtr;
    const TransportationSnapshot snapshot =
        world.getTransportationSnapshot();
    const TransportRows rows =
        RowsForCountry(country, world, snapshot);
    const TransportGeometry geometry = Geometry();
    // The transport desk replaces the world view, so it needs its own opaque
    // surface. Without it the map's province labels showed through the desk
    // text and made both unreadable.
    DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(),
                  {244, 247, 244, 255});
    const Rectangle back = CurrentUILayout().countryBackButton;
    DrawRectangleRec(back, {69, 91, 89, 255});
    DrawRectangleLinesEx(back, 1.0f, {151, 173, 166, 255});
    DrawTextEx(font, "地图", {back.x + 17.0f, back.y + 8.0f},
               kUiBodyFontSize, 0, RAYWHITE);
    Money relatedEscrow(0);
    for (const WarehouseOrderSnapshot* order : rows.orders)
        relatedEscrow += order->escrowed;
    Money relatedRailwayRevenue(0);
    int relatedProfitableRoutes = 0;
    int relatedRailwayBlockedRoutes = 0;
    int relatedUnprofitableRoutes = 0;
    for (const RouteSnapshot* route : rows.routes) {
        relatedRailwayRevenue += route->railwayRevenue;
        if (!route->active || route->distanceKm <= 0.0) continue;
        if (!route->railwayAvailable)
            ++relatedRailwayBlockedRoutes;
        else if (route->profitable)
            ++relatedProfitableRoutes;
        else
            ++relatedUnprofitableRoutes;
    }
    DrawTextEx(font, "运输与物流", {44, 160}, 28, 0,
               {33, 55, 53, 255});
    const std::string summaryLine =
        "规划周期 " + FormatChineseNumber(snapshot.cycle, 0) +
        "  已运输周期 " + FormatChineseNumber(snapshot.usageCycle, 0) +
        "  路线 " + FormatChineseNumber(static_cast<double>(rows.routes.size()), 0) +
        "  订单 " + FormatChineseNumber(static_cast<double>(rows.orders.size()), 0) +
        "  批次 " + FormatChineseNumber(static_cast<double>(rows.shipments.size()), 0) +
        "  相关托管 " + FormatChineseNumber(relatedEscrow.toDouble());
    DrawTextEx(font, summaryLine.c_str(), {44, 202}, 18, 0, DARKGRAY);
    const std::string profitLine =
        "盈利 " + FormatChineseNumber(relatedProfitableRoutes, 0) +
        "  亏损 " + FormatChineseNumber(relatedUnprofitableRoutes, 0) +
        "  铁路受阻 " + FormatChineseNumber(relatedRailwayBlockedRoutes, 0) +
        "  铁路收入 " + FormatChineseNumber(relatedRailwayRevenue.toDouble());
    DrawTextEx(font, profitLine.c_str(), {44, 226}, 16, 0, DARKGRAY);
    DrawRouteMap(state, country, world, rows, geometry, font);
    DrawBatchTable(state, rows, geometry, font);
    DrawOrderTable(state, rows, geometry, font);
    DrawInspector(state, world, rows, geometry, font);
}
