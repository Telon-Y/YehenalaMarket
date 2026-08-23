#pragma once

#include "map_layout.h"
#include "map_model.h"
#include "ui_internal.h"
#include "world_basemap.h"

#include <array>
#include <vector>

namespace ui_map {

using WorldTriangle = std::array<map_model::Point, 3>;

struct CachedWorldPolygon {
    std::vector<map_model::Point> outline;
    std::vector<WorldTriangle> triangles;
    map_model::Point fanCenter{};
    bool usesTriangleFan = false;
};

struct CachedBasemapPolygon {
    CachedWorldPolygon exterior;
    std::vector<CachedWorldPolygon> holes;
};

struct CachedProvinceShape {
    std::vector<CachedWorldPolygon> parts;
    std::vector<CachedWorldPolygon> holes;
};

struct BoundMapData {
    world_basemap::Data basemap;
    std::vector<map_layout::ProvinceShape> shapes;
    map_layout::ProvinceBindingResult binding;
    std::vector<CachedBasemapPolygon> basemapRender;
    std::vector<CachedProvinceShape> provinceRender;
    std::size_t renderTriangleCount = 0;
    std::size_t renderPointCount = 0;
};

void BuildMapRenderCache(BoundMapData& data);
BoundMapData& GetMapData(World& world);
map_model::MapView GetMapView(float scrollX, float zoom);
Color CountryPoliticalColor(const World& world, const Country& country);
Color Blend(Color from, Color to, float amount);
bool DrawCachedPolygonFill(const CachedWorldPolygon& polygon,
                           const map_model::MapView& view, int repeatIndex,
                           Color color);
void DrawCachedPolygonOutline(const CachedWorldPolygon& polygon,
                              const map_model::MapView& view, int repeatIndex,
                              float thickness, Color color);
void DrawWorldBasemap(const BoundMapData& data,
                      const map_model::MapView& view,
                      const std::vector<map_model::LoopCopy>& copies);
void DrawMapGrid(const map_model::MapView& view);

}  // namespace ui_map
