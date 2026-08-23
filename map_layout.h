#pragma once

#include "map_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace world_basemap { struct Data; }
class World;

namespace map_layout {

struct ProvinceShape {
    std::string stableKey;
    std::string name;
    std::string regionKey;
    std::string regionName;
    std::string countryKey;
    int provinceId = -1;
    // A macro-province may contain islands or several source polygons. Keep
    // every part for rendering and hit testing instead of bridging them with
    // a convex hull.
    std::vector<std::vector<map_model::Point>> parts;
    // Water rings inside a macro-province (for example the Caspian and Aral
    // seas) remain holes in both rendering and hit testing.
    std::vector<std::vector<map_model::Point>> holes;
    std::vector<map_model::Point> vertices;
    map_model::Point labelAnchor;
    map_model::Rect bounds;
    bool fromBasemap = false;
    bool geometryReviewed = false;
};

struct ProvinceBindingResult {
    std::size_t boundCount = 0;
    std::vector<std::string> missingKeys;

    bool complete() const { return missingKeys.empty(); }
};

struct GeometryValidationResult {
    std::vector<std::string> errors;

    bool valid() const { return errors.empty(); }
};

// A stable-key province layer. All 69 shapes use
// a Pacific seam so no province itself crosses the horizontal loop boundary.
std::vector<ProvinceShape> CreateDefaultProvinceLayout();

std::vector<ProvinceShape> CreateProvinceLayoutFromBasemap(
    const world_basemap::Data& data);

// Offline-reviewed geometry snapshot. Runtime layout creation must not invent
// province borders by partitioning a country polygon.
std::vector<ProvinceShape> LoadGeneratedProvinceGeometry();

ProvinceBindingResult BindProvinceIdsByStableKey(
    std::vector<ProvinceShape>& layout, const World& world);

// Validates the final rings consumed by both rendering and hit testing. The
// check permits shared administrative borders, but rejects strict interior
// overlap, self-intersections, invalid holes, and world-bound violations.
GeometryValidationResult ValidateProvinceTopology(
    const std::vector<ProvinceShape>& layout);

// Checks reviewed province interiors against the neutral land basemap. This
// is intentionally separate from ring topology: a syntactically valid ring
// can still spill into an ocean, and an authored water hole must remain water.
GeometryValidationResult ValidateProvinceCoverage(
    const std::vector<ProvinceShape>& layout,
    const world_basemap::Data& basemap);

const ProvinceShape* FindProvinceByStableKey(
    const std::vector<ProvinceShape>& layout, const std::string& stableKey);
const ProvinceShape* FindProvinceById(
    const std::vector<ProvinceShape>& layout, int provinceId);
const ProvinceShape* HitTestProvince(
    const std::vector<ProvinceShape>& layout, map_model::Point worldPoint);
const ProvinceShape* HitTestProvinceAtScreen(
    const std::vector<ProvinceShape>& layout, const map_model::MapView& view,
    map_model::Point screenPoint);

}  // namespace map_layout
