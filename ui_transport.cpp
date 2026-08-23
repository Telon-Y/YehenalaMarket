#include "ui_internal.h"

#include "map_layout.h"
#include "map_model.h"
#include "world_basemap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <unordered_set>
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

struct TransportMapData {
    bool attempted = false;
    std::vector<map_layout::ProvinceShape> shapes;
};

TransportMapData& MapData(World& world) {
    static TransportMapData data;
    if (!data.attempted) {
        data.attempted = true;
        // The map is compiled into YehenalaCore. Transport overlays must use
        // the same resource and must not depend on the process working directory.
        world_basemap::Data basemap =
            world_basemap::LoadEmbeddedNaturalEarthGeoJson();
        data.shapes = basemap.valid
            ? map_layout::CreateProvinceLayoutFromBasemap(basemap)
            : map_layout::CreateDefaultProvinceLayout();
        map_layout::BindProvinceIdsByStableKey(data.shapes, world);
    }
    return data;
}

Vector2 WarehousePoint(int warehouseId, const Rectangle& panel, World& world) {
    for (const map_layout::ProvinceShape& shape : MapData(world).shapes) {
        if (shape.provinceId < 0) continue;
        if (world.getProvinceById(shape.provinceId).getLocalMarketId() !=
            warehouseId)
            continue;
        return {panel.x + shape.labelAnchor.x / map_model::kWorldWidth * panel.width,
                panel.y + shape.labelAnchor.y / map_model::kWorldHeight * panel.height};
    }
    const float x = static_cast<float>((std::abs(warehouseId * 37) % 100) / 100.0);
    const float y = static_cast<float>((std::abs(warehouseId * 61) % 100) / 100.0);
    return {panel.x + 18.0f + x * std::max(1.0f, panel.width - 36.0f),
            panel.y + 28.0f + y * std::max(1.0f, panel.height - 48.0f)};
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
    case WarehouseOrderStatus::PendingLocalAllocation: return "Local pending";
    case WarehouseOrderStatus::WaitingForRoute: return "Waiting route";
    case WarehouseOrderStatus::AwaitingSupply: return "Awaiting supply";
    case WarehouseOrderStatus::Confirmed: return "Confirmed";
    case WarehouseOrderStatus::PartiallyConfirmed: return "Partial";
    case WarehouseOrderStatus::InTransit: return "In transit";
    case WarehouseOrderStatus::PartiallyFulfilled: return "Partial receipt";
    case WarehouseOrderStatus::Fulfilled: return "Fulfilled";
    case WarehouseOrderStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* KindLabel(WarehouseOrderKind kind) {
    switch (kind) {
    case WarehouseOrderKind::BuildingMaterialDemand: return "Root demand";
    case WarehouseOrderKind::WarehouseReplenishment: return "Replenishment";
    case WarehouseOrderKind::RemotePurchase: return "Remote purchase";
    case WarehouseOrderKind::SupplierProduction: return "Production";
    }
    return "Order";
}

const char* ReviewReasonLabel(InventoryReviewReason reason) {
    switch (reason) {
    case InventoryReviewReason::StockSufficient: return "Stock sufficient";
    case InventoryReviewReason::RequestSmoothedToZero:
        return "Smoothed to zero";
    case InventoryReviewReason::NewOrderCreated: return "New order";
    case InventoryReviewReason::ExistingOrderUpdated:
        return "Existing order";
    }
    return "Reviewed";
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
    if (goodIndex < 0 || goodIndex >= NUM_GOODS) return "Unknown good";
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
                  const TransportRows& rows, const TransportGeometry& geometry,
                  Font font) {
    DrawPanel(geometry.routeMap, {231, 238, 235, 255}, {156, 171, 167, 255});
    DrawTextEx(font, "Route map", {geometry.routeMap.x + 16, geometry.routeMap.y + 12},
               21, 0, {32, 53, 51, 255});
    DrawTextEx(font, TextFormat("%d active routes", static_cast<int>(rows.routes.size())),
               {geometry.routeMap.x + geometry.routeMap.width - 150,
                 geometry.routeMap.y + 14}, 17, 0, {83, 99, 96, 255});

    const Rectangle plot = {geometry.routeMap.x + 12, geometry.routeMap.y + 42,
                            geometry.routeMap.width - 24,
                            geometry.routeMap.height - 54};
    for (int i = 1; i < 5; ++i) {
        const float y = plot.y + plot.height * i / 5.0f;
        DrawLine(static_cast<int>(plot.x), static_cast<int>(y),
                 static_cast<int>(plot.x + plot.width), static_cast<int>(y),
                 {174, 192, 187, 110});
    }
    for (int i = 1; i < 8; ++i) {
        const float x = plot.x + plot.width * i / 8.0f;
        DrawLine(static_cast<int>(x), static_cast<int>(plot.y), static_cast<int>(x),
                 static_cast<int>(plot.y + plot.height), {174, 192, 187, 90});
    }
    for (const int provinceId : country.getProvinceIds()) {
        const int warehouseId = world.getProvinceById(provinceId).getLocalMarketId();
        DrawCircleV(WarehousePoint(warehouseId, plot, world), 4.0f,
                    {55, 87, 80, 210});
    }
    for (const RouteSnapshot* route : rows.routes) {
        const Vector2 start = WarehousePoint(route->sourceWarehouseId, plot, world);
        const Vector2 end = WarehousePoint(route->destinationWarehouseId, plot, world);
        const bool selected = route->id == state->selectedTransportRouteId;
        const Color line = selected ? Color{213, 156, 49, 255}
            : (!route->railwayAvailable ? Color{152, 71, 63, 190}
            : (!route->profitable ? Color{185, 113, 45, 190}
                                  : Color{64, 119, 124, 190}));
        DrawLineEx(start, end, selected ? 3.0f : 1.5f, line);
        const Vector2 direction = {end.x - start.x, end.y - start.y};
        const float length = std::sqrt(direction.x * direction.x +
                                       direction.y * direction.y);
        if (length > 12.0f) {
            const Vector2 unit = {direction.x / length, direction.y / length};
            const Vector2 normal = {-unit.y, unit.x};
            const Vector2 tip = {end.x - unit.x * 6.0f, end.y - unit.y * 6.0f};
            DrawTriangle(tip,
                         {tip.x - unit.x * 10.0f + normal.x * 4.0f,
                          tip.y - unit.y * 10.0f + normal.y * 4.0f},
                         {tip.x - unit.x * 10.0f - normal.x * 4.0f,
                          tip.y - unit.y * 10.0f - normal.y * 4.0f}, line);
        }
        DrawCircleV(start, selected ? 6.0f : 4.0f, line);
        DrawCircleV(end, selected ? 6.0f : 4.0f, line);
    }
    if (rows.routes.empty())
        DrawTextEx(font, "No routes configured for this country.",
                   {plot.x + 20, plot.y + plot.height * 0.5f}, 16, 0, GRAY);
}

void DrawBatchTable(const UIState* state, const TransportRows& rows,
                    const TransportGeometry& geometry, Font font) {
    DrawPanel(geometry.batches, {247, 250, 247, 255}, {174, 183, 179, 255});
    DrawTextEx(font, "Shipment batches", {geometry.batches.x + 14, geometry.batches.y + 10},
               20, 0, {33, 55, 53, 255});
    const float y0 = geometry.batches.y + 39;
    DrawTextEx(font, "Batch / order", {geometry.batches.x + 14, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Good", {geometry.batches.x + geometry.batches.width * 0.30f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Cargo", {geometry.batches.x + geometry.batches.width * 0.52f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "ETA", {geometry.batches.x + geometry.batches.width * 0.72f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Status", {geometry.batches.x + geometry.batches.width * 0.82f, y0},
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
        DrawTextEx(font, TextFormat("%.2f", shipment->cargo.toDouble()),
                   {geometry.batches.x + geometry.batches.width * 0.52f, y},
                   kTransportRowFontSize, 0,
                   DARKGRAY);
        DrawTextEx(font, TextFormat("%d", shipment->remainingCycles),
                   {geometry.batches.x + geometry.batches.width * 0.70f, y},
                   kTransportRowFontSize, 0,
                   shipment->remainingCycles > 0 ? ORANGE : GREEN);
        const WarehouseOrderSnapshot* order = OrderForShipment(rows, *shipment);
        DrawTextEx(font, order == nullptr ? "Shipment" : StatusLabel(order->status),
                   {geometry.batches.x + geometry.batches.width * 0.82f, y},
                   kTransportRowFontSize, 0,
                   order == nullptr ? GRAY : StatusColor(order->status));
        y += kTransportRowHeight;
    }
    if (rows.shipments.empty())
        DrawTextEx(font, "No active shipment batches.", {geometry.batches.x + 14, y},
                   kTransportRowFontSize, 0, GRAY);
}

void DrawOrderTable(const UIState* state, const TransportRows& rows,
                    const TransportGeometry& geometry, Font font) {
    DrawPanel(geometry.orders, {247, 250, 247, 255}, {174, 183, 179, 255});
    DrawTextEx(font, "Order pipeline", {geometry.orders.x + 14, geometry.orders.y + 10},
               20, 0, {33, 55, 53, 255});
    const float y0 = geometry.orders.y + 39;
    DrawTextEx(font, "ID / root", {geometry.orders.x + 14, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Type", {geometry.orders.x + geometry.orders.width * 0.27f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Good", {geometry.orders.x + geometry.orders.width * 0.50f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Progress", {geometry.orders.x + geometry.orders.width * 0.66f, y0},
               kTransportHeaderFontSize, 0, GRAY);
    DrawTextEx(font, "Route", {geometry.orders.x + geometry.orders.width * 0.88f, y0},
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
        DrawTextEx(font, TextFormat("%.1f / %.1f", order->received.toDouble(),
                                   order->requested.toDouble()),
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
        DrawTextEx(font, "No open orders in this country.", {geometry.orders.x + 14, y},
                   kTransportRowFontSize, 0, GRAY);
}

void DrawInspector(const UIState* state, World& world,
                   const TransportRows& rows, const TransportGeometry& geometry,
                   Font font) {
    DrawPanel(geometry.inspector, {241, 246, 242, 255}, {157, 172, 167, 255});
    DrawTextEx(font, "Route inspector", {geometry.inspector.x + 16,
                                          geometry.inspector.y + 14}, 21, 0,
               {32, 53, 51, 255});
    const RouteSnapshot* route = FindRoute(rows, state->selectedTransportRouteId);
    if (route == nullptr && !rows.routes.empty()) route = rows.routes.front();
    float y = geometry.inspector.y + 50.0f;
    if (route != nullptr) {
        const char* economics = !route->railwayAvailable
            ? "NO RAIL" : (route->profitable ? "PROFITABLE" : "NO MARGIN");
        DrawTextEx(font, TextFormat("Route #%d  %s", route->id, economics),
                   {geometry.inspector.x + 16, y}, 17, 0,
                   !route->railwayAvailable ? Color{152, 71, 63, 255} :
                   (route->profitable ? Color{42, 111, 82, 255}
                                      : Color{185, 113, 45, 255}));
        y += 28.0f;
        DrawTextEx(font, (WarehouseName(route->sourceWarehouseId, world) +
                          "  ->  " + WarehouseName(route->destinationWarehouseId, world)).c_str(),
                   {geometry.inspector.x + 16, y}, 16, 0, {47, 62, 61, 255});
        y += 26.0f;
        DrawTextEx(font, TextFormat("Good %s", GoodLabel(route->goodIndex)),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Distance %.0f km   ETA %d weeks",
                                   route->distanceKm, route->transitCycles),
                   {geometry.inspector.x + 16, y + 22}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Origin %.2f   destination %.2f",
                                   route->sourceUnitPrice.toDouble(),
                                   route->destinationUnitPrice.toDouble()),
                   {geometry.inspector.x + 16, y + 44}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Capacity price %.2f   cargo charge %.2f",
                                   route->railwayCapacityPricePerUnit.toDouble(),
                                   route->transportCostPerUnit.toDouble()),
                   {geometry.inspector.x + 16, y + 66}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Capacity per cargo %.4f   contract %.2f",
                                   route->transportCapacityPerUnit.toDouble(),
                                   route->unitPrice.toDouble()),
                   {geometry.inspector.x + 16, y + 88}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Last rail revenue %.2f   warehouse %.2f",
                                   route->railwayRevenue.toDouble(),
                                   route->warehouseProfit.toDouble()),
                   {geometry.inspector.x + 16, y + 110}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Used %.2f / %.2f   queued %.2f",
                                   route->usedCapacity.toDouble(),
                                   route->capacityPerCycle.toDouble(),
                                   route->queuedVolume.toDouble()),
                   {geometry.inspector.x + 16, y + 132}, kUiBodyFontSize, 0,
                   route->queuedVolume > Money(0) ? ORANGE : DARKGRAY);
        y += 168.0f;
    } else {
        DrawTextEx(font, "Select a route on the map to inspect it.",
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
    DrawTextEx(font, "Inventory review", {geometry.inspector.x + 16, y},
               19, 0, {32, 53, 51, 255});
    y += 27.0f;
    if (review != nullptr) {
        const double coverage = review->averageDemand > Money(1e-9)
            ? (review->onHand / review->averageDemand).toDouble() : 0.0;
        DrawTextEx(font,
                   TextFormat("%s  %s  cycle %d  %s",
                              WarehouseName(review->warehouseId, world).c_str(),
                              GoodLabel(review->goodIndex), review->cycle,
                              ReviewReasonLabel(review->reason)),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   {47, 62, 61, 255});
        y += 21.0f;
        DrawTextEx(font,
                   TextFormat("On hand %.2f  available %.2f  reserved %.2f",
                              review->onHand.toDouble(),
                              review->available.toDouble(),
                              review->reserved.toDouble()),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        DrawTextEx(font,
                   TextFormat("Demand %.2f  coverage %.1f weeks  target %.2f",
                              review->averageDemand.toDouble(), coverage,
                              review->targetStock.toDouble()),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        DrawTextEx(font,
                   TextFormat("Reorder %.2f  position %.2f  gap %.2f",
                              review->reorderPoint.toDouble(),
                              review->inventoryPosition.toDouble(),
                              review->rawGap.toDouble()),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   DARKGRAY);
        y += 20.0f;
        DrawTextEx(font,
                   TextFormat("Request %.2f  confirmed %.2f  transit %.2f",
                              review->plannedRequest.toDouble(),
                              review->confirmedInbound.toDouble(),
                              review->physicalInTransit.toDouble()),
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0,
                   review->rawGap > Money(0) ? ORANGE : DARKGRAY);
        y += 27.0f;
    } else {
        DrawTextEx(font, "No completed inventory review.",
                   {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, GRAY);
        y += 27.0f;
    }
    if (y > geometry.inspector.y + geometry.inspector.height - 90.0f)
        return;
    DrawLine(static_cast<int>(geometry.inspector.x + 14), static_cast<int>(y),
             static_cast<int>(geometry.inspector.x + geometry.inspector.width - 14),
             static_cast<int>(y), {179, 190, 186, 255});
    y += 18.0f;
    DrawTextEx(font, "Order lineage", {geometry.inspector.x + 16, y}, 19, 0,
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
        DrawTextEx(font, "Choose a batch or order row.",
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
    DrawTextEx(font, TextFormat("Requested %.2f   accepted %.2f",
                               selected->requested.toDouble(),
                               selected->accepted.toDouble()),
               {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
    y += 20.0f;
    DrawTextEx(font, TextFormat("Reserved %.2f   shipped %.2f   received %.2f",
                               selected->reserved.toDouble(), selected->shipped.toDouble(),
                               selected->received.toDouble()),
               {geometry.inspector.x + 16, y}, kUiBodyFontSize, 0, DARKGRAY);
    y += 28.0f;
    if (selected->routeId >= 0) {
        DrawTextEx(font, TextFormat("Origin %.2f + cargo rail %.2f = contract %.2f",
                                   selected->sourceUnitPrice.toDouble(),
                                   selected->railwayChargePerUnit.toDouble(),
                                   selected->contractPrice.toDouble()),
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
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    const Country& country = world.getCountryById(state->selectedCountryId);
    const TransportationSnapshot snapshot = world.getTransportationSnapshot();
    const TransportRows rows = RowsForCountry(country, world, snapshot);
    const TransportGeometry geometry = Geometry();
    const Vector2 mouse = GetMousePosition();
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

    const Rectangle plot = {geometry.routeMap.x + 12, geometry.routeMap.y + 42,
                            geometry.routeMap.width - 24,
                            geometry.routeMap.height - 54};
    if (CheckCollisionPointRec(mouse, plot)) {
        float bestDistance = 12.0f;
        const RouteSnapshot* best = nullptr;
        for (const RouteSnapshot* route : rows.routes) {
            const Vector2 start =
                WarehousePoint(route->sourceWarehouseId, plot, world);
            const Vector2 end =
                WarehousePoint(route->destinationWarehouseId, plot, world);
            const float distance = DistanceToSegment(mouse, start, end);
            if (distance < bestDistance) {
                bestDistance = distance;
                best = route;
            }
        }
        if (best != nullptr) state->selectedTransportRouteId = best->id;
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
    const Country& country = world.getCountryById(state->selectedCountryId);
    const TransportationSnapshot snapshot = world.getTransportationSnapshot();
    const TransportRows rows = RowsForCountry(country, world, snapshot);
    const TransportGeometry geometry = Geometry();
    Money relatedEscrow(0);
    for (const WarehouseOrderSnapshot* order : rows.orders)
        relatedEscrow += order->escrowed;
    Money relatedRailwayRevenue(0);
    Money relatedWarehouseProfit(0);
    int relatedProfitableRoutes = 0;
    int relatedRailwayBlockedRoutes = 0;
    int relatedUnprofitableRoutes = 0;
    for (const RouteSnapshot* route : rows.routes) {
        relatedRailwayRevenue += route->railwayRevenue;
        relatedWarehouseProfit += route->warehouseProfit;
        if (!route->active || route->distanceKm <= 0.0) continue;
        if (!route->railwayAvailable)
            ++relatedRailwayBlockedRoutes;
        else if (route->profitable)
            ++relatedProfitableRoutes;
        else
            ++relatedUnprofitableRoutes;
    }
    DrawTextEx(font, "Transport and logistics", {44, 160}, 28, 0,
               {33, 55, 53, 255});
    DrawTextEx(font,
               TextFormat("Planning cycle %d  Completed transport cycle %d  Routes %d  Orders %d  Batches %d  Related escrow %.2f",
                          snapshot.cycle,
                          snapshot.usageCycle,
                          static_cast<int>(rows.routes.size()),
                          static_cast<int>(rows.orders.size()),
                          static_cast<int>(rows.shipments.size()),
                          relatedEscrow.toDouble()),
               {44, 202}, 18, 0, DARKGRAY);
    DrawTextEx(font,
               TextFormat("Profitable %d  unprofitable %d  rail-blocked %d  rail revenue %.2f  warehouse profit %.2f",
                          relatedProfitableRoutes,
                          relatedUnprofitableRoutes,
                          relatedRailwayBlockedRoutes,
                          relatedRailwayRevenue.toDouble(),
                          relatedWarehouseProfit.toDouble()),
               {44, 226}, 16, 0, DARKGRAY);
    DrawRouteMap(state, country, world, rows, geometry, font);
    DrawBatchTable(state, rows, geometry, font);
    DrawOrderTable(state, rows, geometry, font);
    DrawInspector(state, world, rows, geometry, font);
}
