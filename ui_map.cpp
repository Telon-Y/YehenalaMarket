#include "ui_internal.h"
#include "number_format.h"

#include "ui_map_internal.h"

#include "map_layout.h"
#include "map_model.h"
#include "world_basemap.h"
#include "country_palette.h"
#include "ui_country_internal.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <utility>
#include <string>
#include <vector>

using namespace ui_map;

namespace {

std::string CompactMetric(double value) {
    if (!std::isfinite(value)) return "--";
    char buffer[32];
    const double magnitude = std::fabs(value);
    if (magnitude >= 100000000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f亿", value / 100000000.0);
    } else if (magnitude >= 10000.0) {
        std::snprintf(buffer, sizeof(buffer), "%.1f万", value / 10000.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f", value);
    }
    return buffer;
}

void DrawPlayerCountryHud(const UIState* state, const World& world, Font font) {
    if (state->view != UIView::WorldMap || state->playerCountryId < 0) return;
    const CountrySnapshot country =
        world.getCountrySnapshot(state->playerCountryId);
    const Rectangle flag = {14.0f, 10.0f, 52.0f, 34.0f};
    ui_country::DrawFlag(country, flag);
    DrawTextEx(font, country.name.c_str(), {78.0f, 6.0f},
               18.0f, 0.0f, RAYWHITE);
    const std::string metrics =
        "国内生产总值 " + CompactMetric(country.gdp.toDouble()) +
        "  人口 " + CompactMetric(country.population) +
        "  满意度 " +
        CompactMetric(country.averageSatisfaction * 100.0) + "%";
    DrawTextEx(font, metrics.c_str(), {78.0f, 29.0f},
               14.0f, 0.0f, {190, 207, 201, 255});
}

void DrawCountrySelectionPrompt(const UIState* state, const World& world,
                                Font font) {
    if (state->view != UIView::WorldMap || state->playerCountryId >= 0) return;
    std::string label = "选择国家";
    if (state->hoveredProvinceId >= 0) {
        const Province& province =
            world.getProvinceById(state->hoveredProvinceId);
        label += "  " +
            world.getCountryById(province.getCountryId()).getName();
    }
    const Vector2 textSize =
        MeasureTextEx(font, label.c_str(), 24.0f, 0.0f);
    const Rectangle band = {
        (static_cast<float>(GetScreenWidth()) - textSize.x) * 0.5f - 22.0f,
        124.0f, textSize.x + 44.0f, 48.0f
    };
    DrawRectangleRec(band, {31, 45, 43, 235});
    DrawRectangleLinesEx(band, 1.0f, {218, 178, 80, 255});
    DrawTextEx(font, label.c_str(),
               {band.x + 22.0f, band.y + 10.0f},
               24.0f, 0.0f, RAYWHITE);
}


Color LogisticsRouteColor(const RouteSnapshot& route) {
    if (!route.active) return {116, 121, 119, 210};
    const double capacity = std::max(0.0, route.capacityPerCycle.toDouble());
    const float utilization = capacity > 0.0
        ? static_cast<float>(std::clamp(
              route.usedCapacity.toDouble() / capacity, 0.0, 1.0))
        : 0.0f;
    Color color = Blend({47, 132, 100, 235}, {197, 67, 50, 245},
                        utilization);
    if (route.queuedVolume > Money(0))
        color = Blend(color, {218, 151, 42, 245}, 0.38f);
    return color;
}

void DrawLogisticsRoutes(const TransportationSnapshot& snapshot,
                         const BoundMapData& data, const World& world,
                         const map_model::MapView& view) {
    const std::vector<map_model::LoopCopy> copies =
        map_model::VisibleLoopCopies(view);
    for (const RouteSnapshot& route : snapshot.routes) {
        map_model::Point source;
        map_model::Point destination;
        if (!TryWarehouseWorldPoint(route.sourceWarehouseId, data, world,
                                    &source) ||
            !TryWarehouseWorldPoint(route.destinationWarehouseId, data, world,
                                    &destination))
            continue;

        float deltaX = destination.x - source.x;
        if (deltaX > view.worldWidth * 0.5f)
            destination.x -= view.worldWidth;
        else if (deltaX < -view.worldWidth * 0.5f)
            destination.x += view.worldWidth;

        const double capacity =
            std::max(0.0, route.capacityPerCycle.toDouble());
        const float utilization = capacity > 0.0
            ? static_cast<float>(std::clamp(
                  route.usedCapacity.toDouble() / capacity, 0.0, 1.0))
            : 0.0f;
        const float queued = capacity > 0.0
            ? static_cast<float>(std::clamp(
                  route.queuedVolume.toDouble() / capacity, 0.0, 1.0))
            : (route.queuedVolume > Money(0) ? 1.0f : 0.0f);
        const float thickness =
            1.5f + utilization * 3.5f + queued * 1.5f;
        const Color color = LogisticsRouteColor(route);
        for (const map_model::LoopCopy copy : copies) {
            const Vector2 start = {
                map_model::WorldToScreen(view, source, copy.repeatIndex).x,
                map_model::WorldToScreen(view, source, copy.repeatIndex).y};
            const Vector2 end = {
                map_model::WorldToScreen(view, destination,
                                         copy.repeatIndex).x,
                map_model::WorldToScreen(view, destination,
                                         copy.repeatIndex).y};
            DrawLineEx(start, end, thickness, color);
            DrawCircleV(start, std::max(2.5f, thickness * 0.75f), color);
            DrawCircleV(end, std::max(2.5f, thickness * 0.75f), color);
        }
    }
}

void DrawSpeedControls(const UIState* state, Font font) {
    DrawRectangle(0, 0, GetScreenWidth(), 56, {38, 43, 45, 255});
    if (state->view == UIView::ProvinceDetail ||
        state->view == UIView::CommodityMarket) return;
    const UILayout layout = CurrentUILayout();
    const char* labels[5] = {
        state->paused ? "继续" : "暂停", "1 倍", "2 倍", "5 倍", "不限速"};
    const int speeds[5] = {0, 1, 2, 5, -1};
    for (int i = 0; i < 5; ++i) {
        const bool active = i == 0
            ? state->paused
            : (!state->paused && state->simulationSpeed == speeds[i]);
        const Color fill = active ? Color{75, 145, 112, 255}
                                  : Color{65, 72, 74, 255};
        const Rectangle button =
            layout.speedButtons[static_cast<std::size_t>(i)];
        DrawRectangleRec(button, fill);
        DrawRectangleLinesEx(button, 1,
                             active ? Color{139, 211, 169, 255}
                                    : Color{93, 101, 103, 255});
        const Vector2 size = MeasureTextEx(font, labels[i], 20, 0);
        DrawTextEx(font, labels[i],
                   {button.x + (button.width - size.x) * 0.5f,
                    button.y + 9},
                   20, 0, RAYWHITE);
    }
}

void DrawMapModeToggle(const UIState* state, Font font) {
    const Rectangle control = CurrentUILayout().mapModeToggle;
    const float segmentWidth = control.width * 0.5f;
    const Rectangle political =
        {control.x, control.y, segmentWidth, control.height};
    const Rectangle logistics =
        {control.x + segmentWidth, control.y, segmentWidth, control.height};
    const Color selected = {43, 86, 75, 245};
    const Color idle = {238, 241, 236, 245};
    DrawRectangleRec(political,
                     state->mapMode == MapMode::Political ? selected : idle);
    DrawRectangleRec(logistics,
                     state->mapMode == MapMode::Logistics ? selected : idle);
    DrawRectangleLinesEx(control, 1.0f, {64, 79, 75, 255});
    DrawLine(static_cast<int>(control.x + segmentWidth),
             static_cast<int>(control.y),
             static_cast<int>(control.x + segmentWidth),
             static_cast<int>(control.y + control.height),
             {64, 79, 75, 255});
    const char* labels[] = {"政治", "物流"};
    const Rectangle segments[] = {political, logistics};
    for (int index = 0; index < 2; ++index) {
        const Vector2 size = MeasureTextEx(font, labels[index], 17, 0);
        const Color textColor =
            (index == 0 && state->mapMode == MapMode::Political) ||
                    (index == 1 && state->mapMode == MapMode::Logistics)
                ? RAYWHITE : Color{47, 62, 59, 255};
        DrawTextEx(font, labels[index],
                   {segments[index].x +
                        (segments[index].width - size.x) * 0.5f,
                    segments[index].y + 9.0f},
                   17, 0, textColor);
    }
}

std::string MapBreadcrumb(const UIState* state, const World& world) {
    const std::string mode = state->mapMode == MapMode::Logistics
        ? "物流地图" : "政治地图";
    if (state->hoveredProvinceId < 0) return "世界 / " + mode;
    const Province& province = world.getProvinceById(state->hoveredProvinceId);
    const Region& region = world.getRegionById(province.getRegionId());
    const Continent& continent =
        world.getContinentById(region.getContinentId());
    const Country& country = world.getCountryById(province.getCountryId());
    return mode + " / " + continent.getName() + " / " + region.getName() + " / " +
           country.getName() + " / " + province.getName();
}

void DrawMapToolbar(const UIState* state, const World& world, Font font) {
    DrawRectangle(0, 56, GetScreenWidth(), 52, {244, 245, 241, 255});
    DrawLine(0, 107, GetScreenWidth(), 107, {174, 179, 174, 255});
    DrawTextEx(font, "世界市场", {22, 64}, 32, 0, {35, 42, 43, 255});
    const bool compact = GetScreenWidth() < 1500;
    std::string breadcrumb = MapBreadcrumb(state, world);
    if (compact && state->hoveredProvinceId >= 0) {
        breadcrumb =
            std::string(state->mapMode == MapMode::Logistics
                            ? "物流" : "政治") +
            " / " +
            world.getProvinceById(state->hoveredProvinceId).getName();
    }
    DrawTextEx(font, breadcrumb.c_str(),
               {compact ? 170.0f : 205.0f, 72},
               compact ? 20.0f : 22.0f, 0, {72, 82, 83, 255});

    const int week = world.getProvinceCount() > 0
        ? world.getMarket(0).getStepCount()
        : 0;
    const char* summary = compact
        ? TextFormat("第 %d 周", week)
        : TextFormat("第 %d 周    %d 个国家    %d 个省份",
                     week, world.getCountryCount(), world.getProvinceCount());
    const Vector2 summarySize = MeasureTextEx(font, summary, 20, 0);
    DrawTextEx(font, summary,
               {static_cast<float>(GetScreenWidth()) - summarySize.x - 24, 73},
               20, 0, {53, 67, 68, 255});
}

void DrawProvinceLabel(const map_layout::ProvinceShape& shape,
                       const map_model::MapView& view, int repeatIndex,
                       Font font, bool emphasized,
                       std::vector<Rectangle>& occupiedLabels) {
    const float scale = map_model::ViewScale(view);
    const float availableWidth = shape.bounds.width * scale - 8.0f;
    const float availableHeight = shape.bounds.height * scale;
    const float fontSize = emphasized ? 18.0f : 16.0f;
    const Vector2 textSize =
        MeasureTextEx(font, shape.name.c_str(), fontSize, 0);
    if (!emphasized &&
        (textSize.x > availableWidth || availableHeight < fontSize + 5.0f)) {
        return;
    }
    const map_model::Point anchor =
        map_model::WorldToScreen(view, shape.labelAnchor, repeatIndex);
    if (anchor.x + textSize.x * 0.5f < view.viewport.x ||
        anchor.x - textSize.x * 0.5f >
            view.viewport.x + view.viewport.width) {
        return;
    }
    const Vector2 position =
        {anchor.x - textSize.x * 0.5f, anchor.y - textSize.y * 0.5f};
    const Rectangle labelBounds = {
        position.x - 3.0f, position.y - 2.0f,
        textSize.x + 6.0f, textSize.y + 4.0f};
    if (labelBounds.y + labelBounds.height < view.viewport.y ||
        labelBounds.y > view.viewport.y + view.viewport.height)
        return;
    if (!emphasized) {
        for (const Rectangle existing : occupiedLabels) {
            if (CheckCollisionRecs(labelBounds, existing)) return;
        }
    }
    occupiedLabels.push_back(labelBounds);
    DrawTextEx(font, shape.name.c_str(), {position.x + 1, position.y + 1},
               fontSize, 0, {255, 255, 255, 170});
    DrawTextEx(font, shape.name.c_str(), position, fontSize, 0,
               {33, 42, 42, 255});
}

void DrawProvinceTooltip(const UIState* state, const World& world, Font font) {
    if (state->hoveredProvinceId < 0) return;
    const Province& province = world.getProvinceById(state->hoveredProvinceId);
    const Country& country = world.getCountryById(province.getCountryId());
    const Region& region = world.getRegionById(province.getRegionId());
    const LocalMarket& market = province.getLocalMarket();

    Vector2 mouse = GetMousePosition();
    Rectangle panel = {mouse.x + 20, mouse.y + 20, 330, 126};
    if (panel.x + panel.width > GetScreenWidth() - 8)
        panel.x = mouse.x - panel.width - 20;
    if (panel.y + panel.height > GetScreenHeight() - 8)
        panel.y = mouse.y - panel.height - 20;

    DrawRectangleRounded({panel.x + 4, panel.y + 5, panel.width, panel.height},
                         0.05f, 6, {29, 35, 36, 65});
    DrawRectangleRounded(panel, 0.05f, 6, {250, 250, 247, 248});
    DrawRectangleLinesEx(panel, 1, {102, 112, 110, 255});
    DrawTextEx(font, province.getName().c_str(),
               {panel.x + 16, panel.y + 12}, 24, 0, {28, 38, 39, 255});
    const std::string affiliation =
        country.getName() + " / " + region.getName();
    DrawTextEx(font, affiliation.c_str(),
               {panel.x + 16, panel.y + 44}, 17, 0, {80, 91, 91, 255});

    const std::string metrics =
        "国内生产总值 " + FormatChineseNumber(market.getGDP().toDouble()) +
        "    人口 " + FormatChineseNumber(market.getPopulation());
    DrawTextEx(font, metrics.c_str(), {panel.x + 16, panel.y + 72},
               18, 0, {36, 63, 57, 255});
    const bool choosingCountry = state->playerCountryId < 0;
    const bool playerCountry =
        province.getCountryId() == state->playerCountryId;
    const char* action = choosingCountry
        ? "点击选择该国家"
        : playerCountry ? "打开国家管理" : "外国省份";
    DrawTextEx(font, action, {panel.x + 16, panel.y + 100},
               16, 0, {45, 104, 78, 255});
}

void DrawEdgeIndicators(const map_model::MapView& view) {
    constexpr float edgeWidth = 42.0f;
    DrawRectangle(static_cast<int>(view.viewport.x),
                  static_cast<int>(view.viewport.y),
                  static_cast<int>(edgeWidth),
                  static_cast<int>(view.viewport.height),
                  {37, 64, 67, 38});
    DrawRectangle(static_cast<int>(view.viewport.x + view.viewport.width -
                                   edgeWidth),
                  static_cast<int>(view.viewport.y),
                  static_cast<int>(edgeWidth),
                  static_cast<int>(view.viewport.height),
                  {37, 64, 67, 38});
    DrawRectangle(static_cast<int>(view.viewport.x),
                  static_cast<int>(view.viewport.y),
                  static_cast<int>(view.viewport.width),
                  static_cast<int>(edgeWidth),
                  {37, 64, 67, 38});
    DrawRectangle(static_cast<int>(view.viewport.x),
                  static_cast<int>(view.viewport.y + view.viewport.height -
                                   edgeWidth),
                  static_cast<int>(view.viewport.width),
                  static_cast<int>(edgeWidth),
                  {37, 64, 67, 38});

    const float middleY = view.viewport.y + view.viewport.height * 0.5f;
    DrawTriangle({view.viewport.x + 28, middleY - 12},
                 {view.viewport.x + 14, middleY},
                 {view.viewport.x + 28, middleY + 12},
                 {246, 248, 245, 185});
    const float right = view.viewport.x + view.viewport.width;
    DrawTriangle({right - 28, middleY - 12}, {right - 14, middleY},
                 {right - 28, middleY + 12}, {246, 248, 245, 185});
    const float middleX = view.viewport.x + view.viewport.width * 0.5f;
    const float top = view.viewport.y;
    DrawTriangle({middleX - 12, top + 28}, {middleX, top + 14},
                 {middleX + 12, top + 28}, {246, 248, 245, 185});
    const float bottom = view.viewport.y + view.viewport.height;
    DrawTriangle({middleX - 12, bottom - 28}, {middleX, bottom - 14},
                 {middleX + 12, bottom - 28}, {246, 248, 245, 185});
}

float VerticalScrollRange(const map_model::MapView& view) {
    const float scale = map_model::ViewScale(view);
    if (scale <= 0.0f) return 0.0f;
    return std::max(
        0.0f, view.worldHeight - view.viewport.height / scale);
}

void DrawGoodsEntryButton(Font font, Rectangle bounds) {
    const Vector2 mouse = GetMousePosition();
    const bool hovered = CheckCollisionPointRec(mouse, bounds);
    const Vector2 center = {
        bounds.x + bounds.width * 0.5f,
        bounds.y + bounds.height * 0.5f
    };
    DrawCircleV(center, bounds.width * 0.5f,
                hovered ? Color{62, 82, 77, 255}
                        : Color{38, 51, 50, 244});
    DrawCircleLines(static_cast<int>(center.x), static_cast<int>(center.y),
                    bounds.width * 0.5f, {219, 181, 87, 255});

    const Color wood = {191, 139, 61, 255};
    const Color woodLine = {93, 66, 34, 255};
    const auto drawCrate = [&](Rectangle crate) {
        DrawRectangleRec(crate, wood);
        DrawRectangleLinesEx(crate, 1.2f, woodLine);
        DrawLineEx({crate.x + 2.0f, crate.y + 2.0f},
                   {crate.x + crate.width - 2.0f,
                    crate.y + crate.height - 2.0f},
                   1.0f, woodLine);
        DrawLineEx({crate.x + crate.width - 2.0f, crate.y + 2.0f},
                   {crate.x + 2.0f, crate.y + crate.height - 2.0f},
                   1.0f, woodLine);
    };
    drawCrate({center.x - 7.0f, center.y - 16.0f, 14.0f, 14.0f});
    drawCrate({center.x - 15.0f, center.y, 14.0f, 14.0f});
    drawCrate({center.x + 1.0f, center.y, 14.0f, 14.0f});

    if (hovered) {
        const Rectangle tip = {
            bounds.x + bounds.width + 9.0f, bounds.y + 11.0f,
            92.0f, 30.0f
        };
        DrawRectangleRec(tip, {30, 38, 37, 242});
        DrawTextEx(font, "商品市场", {tip.x + 10.0f, tip.y + 7.0f},
                   16.0f, 0.0f, RAYWHITE);
    }
}

}  // namespace

void HandleWorldMapInput(UIState* state, World& world) {
    if (!state->mapInputEnabled) {
        if (state->mapDragActive) SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        state->mapDragActive = false;
        return;
    }
    Vector2 mouse = GetMousePosition();
    const UILayout layout = CurrentUILayout();
    const bool mouseLeft = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (state->view == UIView::WorldMap &&
        state->playerCountryId >= 0 &&
        CheckCollisionPointRec(mouse, layout.goodsButton)) {
        state->hoveredProvinceId = -1;
        if (mouseLeft) {
            state->selectedGood = -1;
            state->goodsScroll = 0;
            state->panelConsumesInput = true;
            state->view = UIView::CommodityMarket;
        }
        return;
    }
    const bool windowFocused = IsWindowFocused();
    if (state->mapDragActive) {
        if (!windowFocused ||
            !IsMouseButtonDown(MOUSE_BUTTON_MIDDLE)) {
            state->mapDragActive = false;
            state->hoveredProvinceId = -1;
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
            return;
        }
        const map_model::MapView dragView = GetMapView(
            state->mapScrollX, state->mapScrollY, state->mapZoom);
        const Vector2 mouseDelta = GetMouseDelta();
        const map_model::Point dragged =
            map_model::DragScrollByScreenDelta(
                dragView, state->mapScrollY,
                {mouseDelta.x, mouseDelta.y});
        state->mapScrollX = dragged.x;
        state->mapScrollY = dragged.y;
        state->hoveredProvinceId = -1;
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        return;
    }
    if (state->view != UIView::WorldMap) {
        const Rectangle blockingPanel =
            state->view == UIView::ProvinceDetail ||
            state->view == UIView::CommodityMarket
                ? layout.provincePanel
                : layout.countryPanel;
        if (CheckCollisionPointRec(mouse, blockingPanel)) {
            state->hoveredProvinceId = -1;
            return;
        }
    }
    const int speeds[5] = {0, 1, 2, 5, -1};
    for (int i = 0; i < 5; ++i) {
        if (!mouseLeft ||
            !CheckCollisionPointRec(
                mouse, layout.speedButtons[static_cast<std::size_t>(i)])) {
            continue;
        }
        if (i == 0) {
            state->paused = !state->paused;
        } else {
            state->paused = false;
            state->simulationSpeed = speeds[i];
        }
        return;
    }

    if (CheckCollisionPointRec(mouse, layout.mapModeToggle)) {
        state->hoveredProvinceId = -1;
        if (mouseLeft) {
            const float split =
                layout.mapModeToggle.x + layout.mapModeToggle.width * 0.5f;
            state->mapMode = mouse.x < split
                ? MapMode::Political : MapMode::Logistics;
        }
        return;
    }

    map_model::MapView view = GetMapView(
        state->mapScrollX, state->mapScrollY, state->mapZoom);
    const map_model::Point screenMouse = {mouse.x, mouse.y};
    const bool mouseInMap = map_model::Contains(view.viewport, screenMouse);
    if (windowFocused && mouseInMap &&
        IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        state->mapDragActive = true;
        state->hoveredProvinceId = -1;
        SetMouseCursor(MOUSE_CURSOR_RESIZE_ALL);
        return;
    }
    if (windowFocused) {
        constexpr float maxPanSpeed = 900.0f;
        map_model::Point velocity{};
        if (mouseInMap) {
            velocity.x = map_model::EdgeScrollVelocity(
                mouse.x, view.viewport, {96.0f, maxPanSpeed});
            const map_model::Rect verticalAxis = {
                view.viewport.y, 0.0f, view.viewport.height, 0.0f};
            velocity.y = map_model::EdgeScrollVelocity(
                mouse.y, verticalAxis, {96.0f, maxPanSpeed});
        }
        int keyboardX = 0;
        int keyboardY = 0;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) --keyboardX;
        if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) ++keyboardX;
        if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) --keyboardY;
        if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) ++keyboardY;
        velocity.x += static_cast<float>(keyboardX) * maxPanSpeed;
        velocity.y += static_cast<float>(keyboardY) * maxPanSpeed;
        velocity = map_model::LimitPanVelocity(velocity, maxPanSpeed);
        const float deltaTime = std::min(GetFrameTime(), 0.05f);
        state->mapScrollX = map_model::AdvanceScrollX(
            state->mapScrollX, velocity.x, deltaTime);
        state->mapScrollY = map_model::AdvanceScrollY(
            state->mapScrollY, velocity.y, deltaTime,
            VerticalScrollRange(view));
        view = GetMapView(
            state->mapScrollX, state->mapScrollY, state->mapZoom);
    }

    if (windowFocused && mouseInMap) {
        const float wheel = GetMouseWheelMove();
        if (wheel != 0.0f) {
            const auto anchor = map_model::ScreenToWorld(view, screenMouse);
            const float currentZoom = std::isfinite(state->mapZoom)
                ? state->mapZoom : 1.0f;
            state->mapZoom = std::clamp(
                currentZoom + wheel * 0.12f, 0.75f, 3.0f);
            view = GetMapView(
                state->mapScrollX, state->mapScrollY, state->mapZoom);
            if (anchor.has_value()) {
                const float scale = map_model::ViewScale(view);
                if (scale > 0.0f) {
                    state->mapScrollX = map_model::NormalizeScrollX(
                        anchor->x - (mouse.x - view.viewport.x) / scale,
                        view.worldWidth);
                    const float verticalRange = VerticalScrollRange(view);
                    if (verticalRange > 0.0f) {
                        const float desiredOffsetY =
                            anchor->y -
                            (mouse.y - view.viewport.y) / scale;
                        state->mapScrollY = std::clamp(
                            desiredOffsetY / verticalRange, 0.0f, 1.0f);
                    }
                    view = GetMapView(
                        state->mapScrollX, state->mapScrollY,
                        state->mapZoom);
                }
            }
        }
    }

    const auto* hovered = map_layout::HitTestProvinceAtScreen(
        GetMapData(world).shapes, view, screenMouse);
    state->hoveredProvinceId = hovered != nullptr ? hovered->provinceId : -1;

    if (mouseLeft && hovered != nullptr && hovered->provinceId >= 0) {
        const Province& province = world.getProvinceById(hovered->provinceId);
        if (state->playerCountryId >= 0 &&
            province.getCountryId() != state->playerCountryId) {
            return;
        }
        if (!world.switchProvinceById(hovered->provinceId)) return;
        if (state->playerCountryId < 0) {
            state->playerCountryId = province.getCountryId();
            state->selectedCountryId = state->playerCountryId;
            state->selectedProvinceId = hovered->provinceId;
            state->countrySelectedProvinceId = hovered->provinceId;
            state->panelConsumesInput = false;
            return;
        }
        NavigateToCountry(state, state->playerCountryId,
                          hovered->provinceId);
    }
}

void DrawWorldMapUI(const UIState* state, World& world, Font font,
                    double elapsedSeconds) {
    (void)elapsedSeconds;
    DrawSpeedControls(state, font);
    DrawPlayerCountryHud(state, world, font);
    DrawMapToolbar(state, world, font);

    const map_model::MapView view = GetMapView(
        state->mapScrollX, state->mapScrollY, state->mapZoom);
    DrawRectangleRec({view.viewport.x, view.viewport.y, view.viewport.width,
                      view.viewport.height},
                     {205, 224, 224, 255});
    DrawMapGrid(view);
    BeginScissorMode(static_cast<int>(view.viewport.x),
                     static_cast<int>(view.viewport.y),
                     static_cast<int>(view.viewport.width),
                     static_cast<int>(view.viewport.height));

    BoundMapData& data = GetMapData(world);
    const auto copies = map_model::VisibleLoopCopies(view);
    DrawWorldBasemap(data, view, copies);
    constexpr Color provinceWater = {205, 224, 224, 255};
    for (const map_model::LoopCopy copy : copies) {
        for (std::size_t shapeIndex = 0;
             shapeIndex < data.shapes.size(); ++shapeIndex) {
            const auto& shape = data.shapes[shapeIndex];
            if (shape.provinceId < 0) continue;
            const CachedProvinceShape& cached =
                data.provinceRender[shapeIndex];
            const Province& province = world.getProvinceById(shape.provinceId);
            const Country& country = world.getCountryById(province.getCountryId());
            Color fill = state->mapMode == MapMode::Political
                ? CountryPoliticalColor(world, country)
                : Color{205, 213, 207, 255};
            const bool hovered =
                shape.provinceId == state->hoveredProvinceId;
            const bool selected =
                shape.provinceId == state->selectedProvinceId ||
                (state->view == UIView::WorldMap &&
                 province.getCountryId() == state->playerCountryId);
            for (const CachedWorldPolygon& part : cached.parts) {
                DrawCachedPolygonFill(
                    part, view, copy.repeatIndex, fill);
                DrawCachedPolygonOutline(
                    part, view, copy.repeatIndex, hovered ? 2.5f : 1.0f,
                    hovered ? Color{29, 63, 57, 255}
                            : Color{76, 83, 78, 210});
                if (selected)
                    DrawCachedPolygonOutline(
                        part, view, copy.repeatIndex, 3.0f,
                        {225, 175, 45, 255});
            }
            // Restore this shape's holes before the next province is painted.
            // Water holes remain water, while a later nested province (Punjab
            // inside north India) can occupy its reviewed land exclusion.
            for (const CachedWorldPolygon& hole : cached.holes) {
                DrawCachedPolygonFill(
                    hole, view, copy.repeatIndex, provinceWater);
                DrawCachedPolygonOutline(
                    hole, view, copy.repeatIndex, 0.8f,
                    {137, 157, 149, 185});
            }
        }
    }
    if (state->mapMode == MapMode::Logistics) {
        const TransportationSnapshot& snapshot =
            world.getTransportationSnapshot();
        DrawLogisticsRoutes(snapshot, data, world, view);
    }
    std::vector<Rectangle> occupiedLabels;
    for (const map_model::LoopCopy copy : copies) {
        for (const auto& shape : data.shapes) {
            if (shape.provinceId < 0) continue;
            const bool emphasized =
                shape.provinceId == state->hoveredProvinceId ||
                shape.provinceId == state->selectedProvinceId;
            DrawProvinceLabel(shape, view, copy.repeatIndex, font,
                              emphasized, occupiedLabels);
        }
    }

    DrawEdgeIndicators(view);
    EndScissorMode();
    if (!data.binding.complete()) {
        const char* warning = TextFormat(
            "地图数据未绑定：%d",
            static_cast<int>(data.binding.missingKeys.size()));
        DrawRectangle(18, 190, 260, 38, {142, 55, 48, 235});
        DrawTextEx(font, warning, {30, 198}, 18, 0, WHITE);
    }
    DrawMapModeToggle(state, font);
    DrawCountrySelectionPrompt(state, world, font);
    if (state->view == UIView::WorldMap &&
        state->playerCountryId >= 0)
        DrawGoodsEntryButton(font, CurrentUILayout().goodsButton);
    if (!CheckCollisionPointRec(GetMousePosition(),
                                CurrentUILayout().mapModeToggle) &&
        !CheckCollisionPointRec(GetMousePosition(),
                                CurrentUILayout().goodsButton))
        DrawProvinceTooltip(state, world, font);
}
