#include "ui_map_internal.h"

#include "country_palette.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace ui_map {
BoundMapData& GetMapData(World& world) {
    static BoundMapData data;
    if (data.shapes.empty()) {
        const auto started = std::chrono::steady_clock::now();
        // Loading the embedded resource here makes the executable's map
        // dependency explicit; no working-directory file is consulted.
        data.basemap = world_basemap::LoadEmbeddedNaturalEarthGeoJson();
        data.shapes = data.basemap.valid
            ? map_layout::CreateProvinceLayoutFromBasemap(data.basemap)
            : map_layout::CreateDefaultProvinceLayout();
        data.binding = map_layout::BindProvinceIdsByStableKey(data.shapes, world);
        if (!data.basemap.valid)
            data.binding.missingKeys.push_back("embedded-basemap");

        // A geometry or coverage failure must never leave a stale province id
        // interactive. Keep the basemap visible for diagnostics, but fail
        // closed for selection and province-panel navigation.
        if (!data.binding.complete()) {
            for (map_layout::ProvinceShape& shape : data.shapes)
                shape.provinceId = -1;
        }
        BuildMapRenderCache(data);
        const double milliseconds = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - started).count();
        std::printf(
            "Map cache ready: %zu triangles, %zu outline points in %.1f ms.\n",
            data.renderTriangleCount, data.renderPointCount, milliseconds);
    }
    return data;
}

bool TryWarehouseWorldPoint(int warehouseId, const BoundMapData& data,
                            const World& world,
                            map_model::Point* result) {
    if (result == nullptr) return false;
    for (const map_layout::ProvinceShape& shape : data.shapes) {
        if (shape.provinceId < 0) continue;
        if (world.getProvinceById(shape.provinceId).getLocalMarketId() !=
            warehouseId) {
            continue;
        }
        *result = shape.labelAnchor;
        return true;
    }
    return false;
}

map_model::MapView GetMapView(float scrollX, float scrollY, float zoom) {
    constexpr float toolbarHeight = 108.0f;
    const map_model::Rect viewport = {
        0.0f, toolbarHeight, static_cast<float>(GetScreenWidth()),
        static_cast<float>(GetScreenHeight()) - toolbarHeight};
    const float baseScale = std::max(viewport.width / map_model::kWorldWidth,
                                      viewport.height / map_model::kWorldHeight);
    const float safeZoom = std::isfinite(zoom)
        ? std::clamp(zoom, 0.5f, 4.0f) : 1.0f;
    const float scale = baseScale * safeZoom;
    const float visibleWorldHeight = scale > 0.0f
        ? viewport.height / scale : map_model::kWorldHeight;
    const float maxOffsetY = std::max(
        0.0f, map_model::kWorldHeight - visibleWorldHeight);
    const float safeScrollY = std::isfinite(scrollY)
        ? std::clamp(scrollY, 0.0f, 1.0f) : 0.5f;
    const float offsetY = maxOffsetY * safeScrollY;
    return {viewport, scrollX, map_model::kWorldWidth,
            map_model::kWorldHeight, offsetY, safeZoom};
}

Color CountryPoliticalColor(const World& world, const Country& country) {
    const country_palette::RgbaColor color =
        country_palette::politicalColorForCountry(world, country);
    return {color.r, color.g, color.b, color.a};
}
Color Blend(Color from, Color to, float amount) {
    amount = std::clamp(amount, 0.0f, 1.0f);
    auto channel = [amount](unsigned char a, unsigned char b) {
        return static_cast<unsigned char>(a + (b - a) * amount);
    };
    return {channel(from.r, to.r), channel(from.g, to.g),
            channel(from.b, to.b), from.a};
}

std::vector<Vector2> ScreenPolygon(
    const std::vector<map_model::Point>& vertices,
    const map_model::MapView& view, int repeatIndex) {
    std::vector<Vector2> result;
    result.reserve(vertices.size());
    for (const map_model::Point point : vertices) {
        const map_model::Point screen =
            map_model::WorldToScreen(view, point, repeatIndex);
        result.push_back({screen.x, screen.y});
    }
    return result;
}
float TriangleCross(Vector2 a, Vector2 b, Vector2 c) {
    return (b.x - a.x) * (c.y - a.y) -
           (b.y - a.y) * (c.x - a.x);
}

bool PointInTriangle(Vector2 point, Vector2 a, Vector2 b, Vector2 c) {
    const float first = TriangleCross(a, b, point);
    const float second = TriangleCross(b, c, point);
    const float third = TriangleCross(c, a, point);
    // Points on an ear diagonal are not interior blockers. Treating them as
    // blockers makes detailed Natural Earth coastlines fail ear clipping.
    constexpr float epsilon = 0.0001f;
    if (std::abs(first) <= epsilon || std::abs(second) <= epsilon ||
        std::abs(third) <= epsilon)
        return false;
    const bool hasNegative = first < 0.0f || second < 0.0f || third < 0.0f;
    const bool hasPositive = first > 0.0f || second > 0.0f || third > 0.0f;
    return !(hasNegative && hasPositive);
}

[[maybe_unused]] bool DrawPolygonFill(const std::vector<Vector2>& vertices, Color color) {
    if (vertices.size() < 3) return false;
    std::vector<Vector2> polygon;
    polygon.reserve(vertices.size());
    for (const Vector2 point : vertices) {
        if (polygon.empty() ||
            std::abs(point.x - polygon.back().x) > 0.01f ||
            std::abs(point.y - polygon.back().y) > 0.01f)
            polygon.push_back(point);
    }
    if (polygon.size() > 1 &&
        std::abs(polygon.front().x - polygon.back().x) <= 0.01f &&
        std::abs(polygon.front().y - polygon.back().y) <= 0.01f)
        polygon.pop_back();
    if (polygon.size() < 3) return false;

    // Natural Earth contains long runs of collinear coastline samples.
    // Removing them makes the triangulation stable without changing the
    // rendered outline at map scale.
    bool removedCollinear = true;
    while (removedCollinear && polygon.size() >= 3) {
        removedCollinear = false;
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const Vector2 previous =
                polygon[(index + polygon.size() - 1) % polygon.size()];
            const Vector2 current = polygon[index];
            const Vector2 next = polygon[(index + 1) % polygon.size()];
            if (std::abs(TriangleCross(previous, current, next)) > 0.001f)
                continue;
            polygon.erase(polygon.begin() +
                          static_cast<std::ptrdiff_t>(index));
            removedCollinear = true;
            break;
        }
    }
    if (polygon.size() < 3) return false;

    float areaTwice = 0.0f;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Vector2 current = polygon[index];
        const Vector2 next = polygon[(index + 1) % polygon.size()];
        areaTwice += current.x * next.y - next.x * current.y;
    }
    if (std::abs(areaTwice) <= 0.0001f) return false;
    const bool counterClockwise = areaTwice > 0.0f;
    std::vector<int> remaining;
    remaining.reserve(polygon.size());
    for (int index = 0; index < static_cast<int>(polygon.size()); ++index)
        remaining.push_back(index);

    // Build the complete triangulation before issuing draw calls. A failed
    // ear cut must leave the polygon unfilled instead of leaving a partial,
    // misleading political-color patch on screen.
    std::vector<std::array<Vector2, 3>> triangles;
    triangles.reserve(polygon.size() - 2);
    const int maxIterations = static_cast<int>(polygon.size() * polygon.size());
    int iterations = 0;
    while (remaining.size() > 3 && iterations++ < maxIterations) {
        bool clipped = false;
        for (std::size_t cursor = 0; cursor < remaining.size(); ++cursor) {
            const int previous =
                remaining[(cursor + remaining.size() - 1) % remaining.size()];
            const int current = remaining[cursor];
            const int next = remaining[(cursor + 1) % remaining.size()];
            const float cross = TriangleCross(
                polygon[previous], polygon[current], polygon[next]);
            if ((counterClockwise && cross <= 0.001f) ||
                (!counterClockwise && cross >= -0.001f))
                continue;
            bool containsVertex = false;
            for (const int candidate : remaining) {
                if (candidate == previous || candidate == current ||
                    candidate == next)
                    continue;
                if (PointInTriangle(polygon[candidate], polygon[previous],
                                    polygon[current], polygon[next])) {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex) continue;
            triangles.push_back({polygon[previous], polygon[current],
                                 polygon[next]});
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(cursor));
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (remaining.size() != 3) return false;
    triangles.push_back({polygon[remaining[0]], polygon[remaining[1]],
                         polygon[remaining[2]]});
    for (const auto& triangle : triangles) {
        if (TriangleCross(triangle[0], triangle[1], triangle[2]) > 0.0f)
            DrawTriangle(triangle[0], triangle[2], triangle[1], color);
        else
            DrawTriangle(triangle[0], triangle[1], triangle[2], color);
    }
    return true;
}

double WorldCrossStable(map_model::Point a, map_model::Point b,
                        map_model::Point c) {
    return (static_cast<double>(b.x) - a.x) *
               (static_cast<double>(c.y) - a.y) -
           (static_cast<double>(b.y) - a.y) *
               (static_cast<double>(c.x) - a.x);
}

bool WorldPointInTriangleStable(map_model::Point point,
                                map_model::Point a, map_model::Point b,
                                map_model::Point c) {
    const double first = WorldCrossStable(a, b, point);
    const double second = WorldCrossStable(b, c, point);
    const double third = WorldCrossStable(c, a, point);
    constexpr double epsilon = 1.0e-8;
    if (std::abs(first) <= epsilon || std::abs(second) <= epsilon ||
        std::abs(third) <= epsilon)
        return false;
    const bool hasNegative = first < 0.0 || second < 0.0 || third < 0.0;
    const bool hasPositive = first > 0.0 || second > 0.0 || third > 0.0;
    return !(hasNegative && hasPositive);
}

double PointSegmentDistanceSquared(map_model::Point point,
                                   map_model::Point start,
                                   map_model::Point end) {
    const double deltaX = static_cast<double>(end.x) - start.x;
    const double deltaY = static_cast<double>(end.y) - start.y;
    const double lengthSquared = deltaX * deltaX + deltaY * deltaY;
    if (lengthSquared <= 1.0e-12) {
        const double offsetX = static_cast<double>(point.x) - start.x;
        const double offsetY = static_cast<double>(point.y) - start.y;
        return offsetX * offsetX + offsetY * offsetY;
    }
    const double projection = std::clamp(
        ((static_cast<double>(point.x) - start.x) * deltaX +
         (static_cast<double>(point.y) - start.y) * deltaY) / lengthSquared,
        0.0, 1.0);
    const double projectedX = start.x + projection * deltaX;
    const double projectedY = start.y + projection * deltaY;
    const double offsetX = point.x - projectedX;
    const double offsetY = point.y - projectedY;
    return offsetX * offsetX + offsetY * offsetY;
}

std::vector<map_model::Point> SimplifyOpenPolyline(
    const std::vector<map_model::Point>& points, double toleranceSquared) {
    if (points.size() <= 2) return points;
    std::vector<unsigned char> keep(points.size(), 0);
    keep.front() = 1;
    keep.back() = 1;
    std::vector<std::pair<std::size_t, std::size_t>> pending{
        {0, points.size() - 1}};
    while (!pending.empty()) {
        const auto [first, last] = pending.back();
        pending.pop_back();
        double farthestDistance = toleranceSquared;
        std::size_t farthest = first;
        for (std::size_t index = first + 1; index < last; ++index) {
            const double distance = PointSegmentDistanceSquared(
                points[index], points[first], points[last]);
            if (distance <= farthestDistance) continue;
            farthestDistance = distance;
            farthest = index;
        }
        if (farthest == first) continue;
        keep[farthest] = 1;
        pending.push_back({first, farthest});
        pending.push_back({farthest, last});
    }
    std::vector<map_model::Point> simplified;
    simplified.reserve(points.size());
    for (std::size_t index = 0; index < points.size(); ++index)
        if (keep[index] != 0) simplified.push_back(points[index]);
    return simplified;
}

std::vector<map_model::Point> SimplifyClosedPolygon(
    const std::vector<map_model::Point>& vertices, float tolerance) {
    std::vector<map_model::Point> polygon;
    polygon.reserve(vertices.size());
    for (const map_model::Point point : vertices) {
        if (polygon.empty() ||
            std::abs(point.x - polygon.back().x) > 0.0001f ||
            std::abs(point.y - polygon.back().y) > 0.0001f)
            polygon.push_back(point);
    }
    if (polygon.size() > 1 &&
        std::abs(polygon.front().x - polygon.back().x) <= 0.0001f &&
        std::abs(polygon.front().y - polygon.back().y) <= 0.0001f)
        polygon.pop_back();
    if (polygon.size() <= 4 || tolerance <= 0.0f) return polygon;

    const auto farthestFrom = [&polygon](std::size_t origin) {
        std::size_t farthest = origin;
        double farthestDistance = -1.0;
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const double deltaX =
                static_cast<double>(polygon[index].x) - polygon[origin].x;
            const double deltaY =
                static_cast<double>(polygon[index].y) - polygon[origin].y;
            const double distance = deltaX * deltaX + deltaY * deltaY;
            if (distance <= farthestDistance) continue;
            farthestDistance = distance;
            farthest = index;
        }
        return farthest;
    };
    std::size_t first = farthestFrom(0);
    std::size_t second = farthestFrom(first);
    if (first == second) return polygon;

    const auto makeChain = [&polygon](std::size_t begin, std::size_t end) {
        std::vector<map_model::Point> chain;
        chain.push_back(polygon[begin]);
        while (begin != end) {
            begin = (begin + 1) % polygon.size();
            chain.push_back(polygon[begin]);
        }
        return chain;
    };
    const double toleranceSquared =
        static_cast<double>(tolerance) * tolerance;
    std::vector<map_model::Point> firstChain =
        SimplifyOpenPolyline(makeChain(first, second), toleranceSquared);
    std::vector<map_model::Point> secondChain =
        SimplifyOpenPolyline(makeChain(second, first), toleranceSquared);

    std::vector<map_model::Point> simplified = std::move(firstChain);
    if (secondChain.size() > 2)
        simplified.insert(simplified.end(), secondChain.begin() + 1,
                          secondChain.end() - 1);
    return simplified.size() >= 3 ? simplified : polygon;
}

bool TriangulateWorldPolygonStable(
    const std::vector<map_model::Point>& vertices,
    std::vector<WorldTriangle>& triangles) {
    triangles.clear();
    if (vertices.size() < 3) return false;
    std::vector<map_model::Point> polygon;
    polygon.reserve(vertices.size());
    for (const map_model::Point point : vertices) {
        if (polygon.empty() ||
            std::abs(point.x - polygon.back().x) > 0.0001f ||
            std::abs(point.y - polygon.back().y) > 0.0001f)
            polygon.push_back(point);
    }
    if (polygon.size() > 1 &&
        std::abs(polygon.front().x - polygon.back().x) <= 0.0001f &&
        std::abs(polygon.front().y - polygon.back().y) <= 0.0001f)
        polygon.pop_back();
    if (polygon.size() < 3) return false;

    // Triangulation is deliberately performed before projection. The world
    // polygon never changes while the map is being panned, so a screen-space
    // translation cannot make a different ear win due to float rounding.
    bool removedCollinear = true;
    while (removedCollinear && polygon.size() >= 3) {
        removedCollinear = false;
        for (std::size_t index = 0; index < polygon.size(); ++index) {
            const map_model::Point previous =
                polygon[(index + polygon.size() - 1) % polygon.size()];
            const map_model::Point current = polygon[index];
            const map_model::Point next = polygon[(index + 1) % polygon.size()];
            if (std::abs(WorldCrossStable(previous, current, next)) > 1.0e-7)
                continue;
            polygon.erase(polygon.begin() +
                          static_cast<std::ptrdiff_t>(index));
            removedCollinear = true;
            break;
        }
    }
    if (polygon.size() < 3) return false;

    double areaTwice = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const map_model::Point current = polygon[index];
        const map_model::Point next = polygon[(index + 1) % polygon.size()];
        areaTwice += static_cast<double>(current.x) * next.y -
                     static_cast<double>(next.x) * current.y;
    }
    if (std::abs(areaTwice) <= 1.0e-8) return false;
    const bool counterClockwise = areaTwice > 0.0;
    std::vector<int> remaining;
    remaining.reserve(polygon.size());
    for (int index = 0; index < static_cast<int>(polygon.size()); ++index)
        remaining.push_back(index);

    triangles.reserve(polygon.size() - 2);
    const int maxIterations = static_cast<int>(polygon.size() * polygon.size());
    int iterations = 0;
    while (remaining.size() > 3 && iterations++ < maxIterations) {
        bool clipped = false;
        for (std::size_t cursor = 0; cursor < remaining.size(); ++cursor) {
            const int previous =
                remaining[(cursor + remaining.size() - 1) % remaining.size()];
            const int current = remaining[cursor];
            const int next = remaining[(cursor + 1) % remaining.size()];
            const double cross = WorldCrossStable(
                polygon[previous], polygon[current], polygon[next]);
            if ((counterClockwise && cross <= 1.0e-7) ||
                (!counterClockwise && cross >= -1.0e-7))
                continue;
            bool containsVertex = false;
            for (const int candidate : remaining) {
                if (candidate == previous || candidate == current ||
                    candidate == next)
                    continue;
                if (WorldPointInTriangleStable(
                        polygon[candidate], polygon[previous], polygon[current],
                        polygon[next])) {
                    containsVertex = true;
                    break;
                }
            }
            if (containsVertex) continue;
            triangles.push_back({polygon[previous], polygon[current],
                                 polygon[next]});
            remaining.erase(remaining.begin() +
                            static_cast<std::ptrdiff_t>(cursor));
            clipped = true;
            break;
        }
        if (!clipped) return false;
    }
    if (remaining.size() != 3) return false;
    triangles.push_back({polygon[remaining[0]], polygon[remaining[1]],
                         polygon[remaining[2]]});
    return true;
}

CachedWorldPolygon CacheWorldPolygon(
    const std::vector<map_model::Point>& vertices, float tolerance) {
    CachedWorldPolygon cached;
    cached.outline = SimplifyClosedPolygon(vertices, tolerance);
    cached.bounds = map_model::PolygonBounds(cached.outline);
    if (TriangulateWorldPolygonStable(cached.outline, cached.triangles))
        return cached;
    if (cached.outline.size() < 3) return cached;

    // Natural Earth contains a few self-touching coastlines that cannot be
    // ear-clipped. Retain the previous deterministic centroid-fan fallback,
    // but calculate it once rather than rebuilding it every frame.
    cached.usesTriangleFan = true;
    for (const map_model::Point point : cached.outline) {
        cached.fanCenter.x += point.x;
        cached.fanCenter.y += point.y;
    }
    const float inverseCount =
        1.0f / static_cast<float>(cached.outline.size());
    cached.fanCenter.x *= inverseCount;
    cached.fanCenter.y *= inverseCount;
    return cached;
}

void CountCachedPolygon(BoundMapData& data,
                        const CachedWorldPolygon& polygon) {
    data.renderPointCount += polygon.outline.size();
    data.renderTriangleCount += polygon.usesTriangleFan
        ? polygon.outline.size() : polygon.triangles.size();
}

void BuildMapRenderCache(BoundMapData& data) {
    const float worldUnitsPerDegree = map_model::kWorldWidth / 360.0f;
    // Preserve roughly sub-pixel detail at the maximum supported zoom instead
    // of submitting source vertices that cannot affect the final image.
    const float basemapTolerance = worldUnitsPerDegree * 0.30f;
    const float provinceTolerance = worldUnitsPerDegree * 0.15f;

    data.renderTriangleCount = 0;
    data.renderPointCount = 0;
    data.basemapRender.clear();
    data.basemapRender.reserve(data.basemap.polygons.size());
    for (const world_basemap::Polygon& source : data.basemap.polygons) {
        CachedBasemapPolygon cached;
        cached.exterior =
            CacheWorldPolygon(source.vertices, basemapTolerance);
        CountCachedPolygon(data, cached.exterior);
        cached.holes.reserve(source.holes.size());
        for (const auto& hole : source.holes) {
            cached.holes.push_back(
                CacheWorldPolygon(hole, basemapTolerance));
            CountCachedPolygon(data, cached.holes.back());
        }
        data.basemapRender.push_back(std::move(cached));
    }

    data.provinceRender.clear();
    data.provinceRender.reserve(data.shapes.size());
    for (const map_layout::ProvinceShape& shape : data.shapes) {
        CachedProvinceShape cached;
        if (shape.parts.empty()) {
            cached.parts.push_back(
                CacheWorldPolygon(shape.vertices, provinceTolerance));
            CountCachedPolygon(data, cached.parts.back());
        } else {
            cached.parts.reserve(shape.parts.size());
            for (const auto& part : shape.parts) {
                cached.parts.push_back(
                    CacheWorldPolygon(part, provinceTolerance));
                CountCachedPolygon(data, cached.parts.back());
            }
        }
        cached.holes.reserve(shape.holes.size());
        for (const auto& hole : shape.holes) {
            cached.holes.push_back(
                CacheWorldPolygon(hole, provinceTolerance));
            CountCachedPolygon(data, cached.holes.back());
        }
        data.provinceRender.push_back(std::move(cached));
    }
}

namespace {

bool CachedPolygonVisible(const CachedWorldPolygon& polygon,
                          const map_model::MapView& view, int repeatIndex) {
    if (polygon.outline.size() < 3) return false;
    const map_model::Point first = map_model::WorldToScreen(
        view, {polygon.bounds.x, polygon.bounds.y}, repeatIndex);
    const map_model::Point second = map_model::WorldToScreen(
        view,
        {polygon.bounds.x + polygon.bounds.width,
         polygon.bounds.y + polygon.bounds.height},
        repeatIndex);
    constexpr float margin = 4.0f;
    const float left = std::min(first.x, second.x);
    const float right = std::max(first.x, second.x);
    const float top = std::min(first.y, second.y);
    const float bottom = std::max(first.y, second.y);
    return right >= view.viewport.x - margin &&
           left <= view.viewport.x + view.viewport.width + margin &&
           bottom >= view.viewport.y - margin &&
           top <= view.viewport.y + view.viewport.height + margin;
}

}  // namespace

bool DrawCachedPolygonFill(const CachedWorldPolygon& polygon,
                           const map_model::MapView& view, int repeatIndex,
                           Color color) {
    if (!CachedPolygonVisible(polygon, view, repeatIndex)) return false;
    if (polygon.usesTriangleFan) {
        const std::vector<Vector2> screen =
            ScreenPolygon(polygon.outline, view, repeatIndex);
        if (screen.size() < 3) return false;
        const map_model::Point center =
            map_model::WorldToScreen(view, polygon.fanCenter, repeatIndex);
        std::vector<Vector2> fan;
        fan.reserve(screen.size() + 2);
        fan.push_back({center.x, center.y});
        fan.insert(fan.end(), screen.begin(), screen.end());
        fan.push_back(screen.front());
        DrawTriangleFan(fan.data(), static_cast<int>(fan.size()), color);
        return true;
    }
    if (polygon.triangles.empty()) return false;
    for (const WorldTriangle& triangle : polygon.triangles) {
        const map_model::Point first =
            map_model::WorldToScreen(view, triangle[0], repeatIndex);
        const map_model::Point second =
            map_model::WorldToScreen(view, triangle[1], repeatIndex);
        const map_model::Point third =
            map_model::WorldToScreen(view, triangle[2], repeatIndex);
        const Vector2 firstVector = {first.x, first.y};
        const Vector2 secondVector = {second.x, second.y};
        const Vector2 thirdVector = {third.x, third.y};
        if (WorldCrossStable(triangle[0], triangle[1], triangle[2]) > 0.0)
            DrawTriangle(firstVector, thirdVector, secondVector, color);
        else
            DrawTriangle(firstVector, secondVector, thirdVector, color);
    }
    return true;
}

void DrawCachedPolygonOutline(const CachedWorldPolygon& polygon,
                              const map_model::MapView& view, int repeatIndex,
                              float thickness, Color color) {
    if (polygon.outline.size() < 2 ||
        !CachedPolygonVisible(polygon, view, repeatIndex)) return;
    if (thickness <= 1.05f) {
        static thread_local std::vector<Vector2> screen;
        screen.clear();
        screen.reserve(polygon.outline.size() + 1);
        for (const map_model::Point point : polygon.outline) {
            const map_model::Point current =
                map_model::WorldToScreen(view, point, repeatIndex);
            screen.push_back({current.x, current.y});
        }
        screen.push_back(screen.front());
        constexpr std::size_t maxStripPoints = 2048;
        for (std::size_t begin = 0; begin + 1 < screen.size();
             begin += maxStripPoints - 1) {
            const std::size_t count = std::min(
                maxStripPoints, screen.size() - begin);
            DrawLineStrip(screen.data() + begin,
                          static_cast<int>(count), color);
        }
        return;
    }
    map_model::Point previous = map_model::WorldToScreen(
        view, polygon.outline.back(), repeatIndex);
    for (const map_model::Point point : polygon.outline) {
        const map_model::Point current =
            map_model::WorldToScreen(view, point, repeatIndex);
        DrawLineEx({previous.x, previous.y}, {current.x, current.y},
                   thickness, color);
        previous = current;
    }
}

void DrawWorldBasemap(const BoundMapData& data,
                      const map_model::MapView& view,
                      const std::vector<map_model::LoopCopy>& copies) {
    if (!data.basemap.valid) return;

    // The basemap is deliberately neutral. It closes the visible land
    // silhouette where the reviewed province layer has no definition, while
    // only the province layer below participates in selection and navigation.
    constexpr Color land = {188, 204, 193, 255};
    constexpr Color water = {205, 224, 224, 255};
    constexpr Color coast = {137, 157, 149, 185};
    for (const map_model::LoopCopy copy : copies) {
        for (const CachedBasemapPolygon& polygon : data.basemapRender) {
            DrawCachedPolygonFill(
                polygon.exterior, view, copy.repeatIndex, land);
            for (const CachedWorldPolygon& hole : polygon.holes)
                DrawCachedPolygonFill(hole, view, copy.repeatIndex, water);
            DrawCachedPolygonOutline(
                polygon.exterior, view, copy.repeatIndex, 0.7f, coast);
        }
    }
}
void DrawMapGrid(const map_model::MapView& view) {
    const Color grid = {146, 172, 176, 55};
    const float scale = map_model::ViewScale(view);
    for (int latitude = -45; latitude <= 75; latitude += 15) {
        const float worldY = static_cast<float>(
            map_model::ProjectLonLat(0.0, latitude).y);
        const float y = view.viewport.y +
            (worldY - view.worldOffsetY) * scale;
        DrawLine(static_cast<int>(view.viewport.x), static_cast<int>(y),
                 static_cast<int>(view.viewport.x + view.viewport.width),
                 static_cast<int>(y), grid);
    }
    for (const map_model::LoopCopy copy :
         map_model::VisibleLoopCopies(view)) {
        for (int longitude = 0; longitude <= 3840; longitude += 480) {
            const float x = copy.screenOriginX + longitude * scale;
            if (x < view.viewport.x || x > view.viewport.x + view.viewport.width)
                continue;
            DrawLine(static_cast<int>(x), static_cast<int>(view.viewport.y),
                     static_cast<int>(x),
                     static_cast<int>(view.viewport.y + view.viewport.height),
                     grid);
        }
    }
}


}  // namespace ui_map
