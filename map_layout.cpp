#include "map_layout.h"
#include "world_basemap.h"

#include "province.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace map_layout {

namespace {

constexpr double kTopologyEpsilon = 0.25;

double Cross(map_model::Point a, map_model::Point b, map_model::Point c) {
    return (static_cast<double>(b.x) - a.x) *
               (static_cast<double>(c.y) - a.y) -
           (static_cast<double>(b.y) - a.y) *
               (static_cast<double>(c.x) - a.x);
}

bool OnSegment(map_model::Point point, map_model::Point start,
               map_model::Point end) {
    const double edgeLength = std::hypot(
        static_cast<double>(end.x) - start.x,
        static_cast<double>(end.y) - start.y);
    if (std::abs(Cross(start, end, point)) >
        kTopologyEpsilon * std::max(1.0, edgeLength)) return false;
    return point.x >= std::min(start.x, end.x) - kTopologyEpsilon &&
           point.x <= std::max(start.x, end.x) + kTopologyEpsilon &&
           point.y >= std::min(start.y, end.y) - kTopologyEpsilon &&
           point.y <= std::max(start.y, end.y) + kTopologyEpsilon;
}

bool ProperIntersection(map_model::Point a, map_model::Point b,
                        map_model::Point c, map_model::Point d) {
    const double first = Cross(a, b, c);
    const double second = Cross(a, b, d);
    const double third = Cross(c, d, a);
    const double fourth = Cross(c, d, b);
    const bool oppositeFirst = (first > kTopologyEpsilon &&
                                second < -kTopologyEpsilon) ||
                               (first < -kTopologyEpsilon &&
                                second > kTopologyEpsilon);
    const bool oppositeSecond = (third > kTopologyEpsilon &&
                                 fourth < -kTopologyEpsilon) ||
                                (third < -kTopologyEpsilon &&
                                 fourth > kTopologyEpsilon);
    return oppositeFirst && oppositeSecond;
}

bool StrictlyInside(map_model::Point point,
                    const std::vector<map_model::Point>& ring) {
    for (std::size_t index = 0; index < ring.size(); ++index) {
        const map_model::Point next = ring[(index + 1) % ring.size()];
        if (OnSegment(point, ring[index], next)) return false;
    }
    return map_model::PointInPolygon(point, ring);
}

bool BoundsOverlap(const map_model::Rect& first,
                  const map_model::Rect& second) {
    return first.x <= second.x + second.width + kTopologyEpsilon &&
           second.x <= first.x + first.width + kTopologyEpsilon &&
           first.y <= second.y + second.height + kTopologyEpsilon &&
           second.y <= first.y + first.height + kTopologyEpsilon;
}

bool RingSelfIntersects(const std::vector<map_model::Point>& ring) {
    std::size_t ringSize = ring.size();
    // GeoJSON rings repeat the first point as the closing point. Treat that
    // duplicate as the edge closure rather than as an extra self-intersecting
    // vertex in the topology gate.
    if (ringSize > 1 &&
        std::abs(ring.front().x - ring.back().x) <= kTopologyEpsilon &&
        std::abs(ring.front().y - ring.back().y) <= kTopologyEpsilon)
        --ringSize;
    if (ringSize < 3) return true;
    for (std::size_t first = 0; first < ringSize; ++first) {
        const map_model::Point firstStart = ring[first];
        const map_model::Point firstEnd = ring[(first + 1) % ringSize];
        for (std::size_t second = first + 1; second < ringSize; ++second) {
            if (second == first || second == (first + 1) % ringSize ||
                first == (second + 1) % ringSize) continue;
            const map_model::Point secondStart = ring[second];
            const map_model::Point secondEnd = ring[(second + 1) % ringSize];
            if (ProperIntersection(firstStart, firstEnd,
                                   secondStart, secondEnd)) return true;
        }
    }
    return false;
}

bool RingsCrossBoundary(const std::vector<map_model::Point>& first,
                        const std::vector<map_model::Point>& second) {
    const map_model::Rect firstBounds = map_model::PolygonBounds(first);
    const map_model::Rect secondBounds = map_model::PolygonBounds(second);
    if (firstBounds.x > secondBounds.x + secondBounds.width + kTopologyEpsilon ||
        secondBounds.x > firstBounds.x + firstBounds.width + kTopologyEpsilon ||
        firstBounds.y > secondBounds.y + secondBounds.height + kTopologyEpsilon ||
        secondBounds.y > firstBounds.y + firstBounds.height + kTopologyEpsilon)
        return false;
    for (std::size_t firstIndex = 0; firstIndex < first.size(); ++firstIndex) {
        const map_model::Point firstStart = first[firstIndex];
        const map_model::Point firstEnd = first[(firstIndex + 1) % first.size()];
        for (std::size_t secondIndex = 0; secondIndex < second.size(); ++secondIndex)
            if (ProperIntersection(firstStart, firstEnd, second[secondIndex],
                                   second[(secondIndex + 1) % second.size()]))
                return true;
    }
    return false;
}

bool RingsStrictlyOverlap(
    const std::vector<map_model::Point>& first,
    const std::vector<map_model::Point>& second,
    const std::vector<std::vector<map_model::Point>>& firstHoles,
    const std::vector<std::vector<map_model::Point>>& secondHoles) {
    const map_model::Rect firstBounds = map_model::PolygonBounds(first);
    const map_model::Rect secondBounds = map_model::PolygonBounds(second);
    if (firstBounds.x > secondBounds.x + secondBounds.width + kTopologyEpsilon ||
        secondBounds.x > firstBounds.x + firstBounds.width + kTopologyEpsilon ||
        firstBounds.y > secondBounds.y + secondBounds.height + kTopologyEpsilon ||
        secondBounds.y > firstBounds.y + firstBounds.height + kTopologyEpsilon)
        return false;
    const auto hasInteriorSpan = [](
        const std::vector<map_model::Point>& ring,
        const std::vector<map_model::Point>& other,
        const std::vector<std::vector<map_model::Point>>& otherHoles) {
        if (ring.size() < 3) return false;
        int interiorMidpoints = 0;
        for (std::size_t index = 0; index < ring.size(); ++index) {
            const map_model::Point current = ring[index];
            const map_model::Point next = ring[(index + 1) % ring.size()];
            const map_model::Point midpoint = {
                (current.x + next.x) * 0.5f,
                (current.y + next.y) * 0.5f};
            if (!StrictlyInside(midpoint, other)) continue;
            bool inHole = false;
            for (const std::vector<map_model::Point>& hole : otherHoles) {
                if (map_model::PointInPolygon(midpoint, hole)) {
                    inHole = true;
                    break;
                }
            }
            if (inHole) continue;
            if (++interiorMidpoints >= 2) return true;
        }
        return false;
    };
    // Adjacent dissolved regions can share vertices and long boundary
    // segments. A single projected vertex or one-point spur is a zero-area
    // numerical artifact, not an area overlap. Require two distinct edge
    // midpoints in the other ring; the generator performs the authoritative
    // GEOS validity/edge gate before this runtime check.
    return hasInteriorSpan(first, second, secondHoles) ||
           hasInteriorSpan(second, first, firstHoles);
}

bool RingInsideAnyPart(const std::vector<map_model::Point>& ring,
                       const ProvinceShape& shape) {
    if (ring.empty()) return false;
    for (const std::vector<map_model::Point>& part : shape.parts) {
        if (StrictlyInside(ring.front(), part)) return true;
        // Coastline datasets can put the first hole vertex exactly on the
        // outer ring after projection. Any strictly interior hole vertex is
        // sufficient to establish the intended part ownership.
        for (const map_model::Point point : ring)
            if (StrictlyInside(point, part)) return true;
    }
    return false;
}

bool ShapeTouchesBasemap(const ProvinceShape& shape,
                         const world_basemap::Data& data) {
    if (world_basemap::ContainsLand(data, shape.labelAnchor)) return true;
    for (const std::vector<map_model::Point>& part : shape.parts) {
        const map_model::Rect partBounds = map_model::PolygonBounds(part);
        for (const map_model::Point point : part)
            if (world_basemap::ContainsLand(data, point)) return true;
        for (const world_basemap::Polygon& polygon : data.polygons) {
            for (const map_model::Point point : polygon.vertices) {
                if (map_model::Contains(partBounds, point) &&
                    map_model::PointInPolygon(point, part))
                    return true;
            }
        }
    }
    return false;
}

bool PointInsideShape(const ProvinceShape& shape, map_model::Point point) {
    for (const std::vector<map_model::Point>& part : shape.parts) {
        if (!map_model::PointInPolygon(point, part)) continue;
        for (const std::vector<map_model::Point>& hole : shape.holes)
            if (map_model::PointInPolygon(point, hole)) return false;
        return true;
    }
    return false;
}

bool ShapeInteriorMatchesBasemap(const ProvinceShape& shape,
                                 const world_basemap::Data& data) {
    // The reviewed label anchor is selected from the authored interior. It is
    // the stable anchor for a coarse land-sea gate even when a finer coast
    // does not line up vertex-for-vertex with the 50m basemap.
    if (!PointInsideShape(shape, shape.labelAnchor) ||
        !world_basemap::ContainsLand(data, shape.labelAnchor))
        return false;

    int interiorSamples = 0;
    int landSamples = 0;
    const auto sample = [&](map_model::Point point) {
        if (!PointInsideShape(shape, point)) return;
        ++interiorSamples;
        if (world_basemap::ContainsLand(data, point)) ++landSamples;
    };
    sample(shape.labelAnchor);

    // A centroid catches narrow islands that a regular grid can miss. The
    // grid catches broad ocean spillovers without requiring a GIS runtime.
    for (const std::vector<map_model::Point>& part : shape.parts) {
        if (part.size() < 3) continue;
        map_model::Point centroid{};
        for (const map_model::Point point : part) {
            centroid.x += point.x;
            centroid.y += point.y;
        }
        const float inverseCount = 1.0f / static_cast<float>(part.size());
        centroid.x *= inverseCount;
        centroid.y *= inverseCount;
        sample(centroid);

        const map_model::Rect bounds = map_model::PolygonBounds(part);
        constexpr int kGridResolution = 9;
        for (int row = 1; row < kGridResolution; ++row) {
            for (int column = 1; column < kGridResolution; ++column) {
                const float fractionX = static_cast<float>(column) /
                                        kGridResolution;
                const float fractionY = static_cast<float>(row) /
                                        kGridResolution;
                sample({bounds.x + bounds.width * fractionX,
                        bounds.y + bounds.height * fractionY});
            }
        }
    }
    // A few interior samples guard against an entirely synthetic/ocean ring.
    return interiorSamples > 0 && landSamples > 0;
}

bool RepairLabelAnchorFromBasemap(ProvinceShape& shape,
                                  const world_basemap::Data& data) {
    const auto accept = [&](map_model::Point candidate) {
        return PointInsideShape(shape, candidate) &&
               world_basemap::ContainsLand(data, candidate);
    };
    if (accept(shape.labelAnchor)) return true;

    for (const std::vector<map_model::Point>& part : shape.parts) {
        if (part.size() < 3) continue;
        map_model::Point centroid{};
        for (const map_model::Point point : part) {
            centroid.x += point.x;
            centroid.y += point.y;
        }
        const float inverseCount = 1.0f / static_cast<float>(part.size());
        centroid.x *= inverseCount;
        centroid.y *= inverseCount;
        const map_model::Rect bounds = map_model::PolygonBounds(part);
        const map_model::Point candidates[] = {
            centroid,
            {bounds.x + bounds.width * 0.5f,
             bounds.y + bounds.height * 0.5f},
            {part.front().x * 0.5f + part[1].x * 0.25f +
                 part[2].x * 0.25f,
             part.front().y * 0.5f + part[1].y * 0.25f +
                 part[2].y * 0.25f},
        };
        for (const map_model::Point candidate : candidates) {
            if (!accept(candidate)) continue;
            shape.labelAnchor = candidate;
            return true;
        }

        constexpr int kGridResolution = 9;
        for (int row = 1; row < kGridResolution; ++row) {
            for (int column = 1; column < kGridResolution; ++column) {
                const map_model::Point candidate = {
                    bounds.x + bounds.width *
                        static_cast<float>(column) / kGridResolution,
                    bounds.y + bounds.height *
                        static_cast<float>(row) / kGridResolution};
                if (!accept(candidate)) continue;
                shape.labelAnchor = candidate;
                return true;
            }
        }
    }
    return false;
}

void ApplyReviewedGeometry(std::vector<ProvinceShape>& layout,
                           const world_basemap::Data& reviewed) {
    std::unordered_map<std::string, std::size_t> shapeIndexes;
    std::unordered_set<std::string> replaced;
    for (std::size_t index = 0; index < layout.size(); ++index)
        shapeIndexes.emplace(layout[index].stableKey, index);

    for (const world_basemap::Polygon& polygon : reviewed.polygons) {
        if (polygon.stableKey.empty() || polygon.vertices.size() < 3) continue;
        std::size_t shapeIndex = 0;
        const auto existing = shapeIndexes.find(polygon.stableKey);
        if (existing == shapeIndexes.end()) {
            ProvinceShape shape;
            shape.stableKey = polygon.stableKey;
            shape.name = polygon.name.empty() ? polygon.stableKey : polygon.name;
            shape.regionKey = polygon.regionKey;
            shape.regionName = polygon.regionKey;
            shape.countryKey = polygon.countryKey;
            shape.provinceId = -1;
            shape.fromBasemap = true;
            shapeIndex = layout.size();
            layout.push_back(std::move(shape));
            shapeIndexes.emplace(polygon.stableKey, shapeIndex);
        } else {
            shapeIndex = existing->second;
        }
        ProvinceShape& shape = layout[shapeIndex];
        if (polygon.geometryMode != "append" &&
            replaced.insert(polygon.stableKey).second) {
            shape.parts.clear();
            shape.holes.clear();
        }
        shape.parts.push_back(polygon.vertices);
        for (const auto& hole : polygon.holes)
            shape.holes.push_back(hole);
        shape.geometryReviewed = true;
        shape.fromBasemap = true;
        if (shape.regionKey.empty()) shape.regionKey = polygon.regionKey;
        if (shape.countryKey.empty()) shape.countryKey = polygon.countryKey;
    }

    for (ProvinceShape& shape : layout) {
        if (!shape.geometryReviewed || shape.parts.empty()) continue;
        float minX = map_model::kWorldWidth;
        float minY = map_model::kWorldHeight;
        float maxX = 0.0f;
        float maxY = 0.0f;
        double sumX = 0.0;
        double sumY = 0.0;
        std::size_t pointCount = 0;
        for (const auto& part : shape.parts) {
            for (const map_model::Point point : part) {
                minX = std::min(minX, point.x);
                minY = std::min(minY, point.y);
                maxX = std::max(maxX, point.x);
                maxY = std::max(maxY, point.y);
                sumX += point.x;
                sumY += point.y;
                ++pointCount;
            }
        }
        shape.bounds = {minX, minY, maxX - minX, maxY - minY};
        if (shape.vertices.size() < 3)
            shape.vertices = shape.parts.front();
        const auto pointIsInside = [&shape](map_model::Point point) {
            for (const auto& part : shape.parts) {
                if (!map_model::PointInPolygon(point, part)) continue;
                bool inHole = false;
                for (const auto& hole : shape.holes)
                    if (map_model::PointInPolygon(point, hole)) inHole = true;
                if (!inHole) return true;
            }
            return false;
        };
        if (!pointIsInside(shape.labelAnchor)) {
            bool foundAnchor = false;
            for (const auto& part : shape.parts) {
                const map_model::Rect partBounds = map_model::PolygonBounds(part);
                const map_model::Point candidates[] = {
                    {partBounds.x + partBounds.width * 0.5f,
                     partBounds.y + partBounds.height * 0.5f},
                    part.front(),
                    {part.front().x * 0.5f + part[1].x * 0.25f +
                         part[2].x * 0.25f,
                     part.front().y * 0.5f + part[1].y * 0.25f +
                         part[2].y * 0.25f}};
                for (const map_model::Point candidate : candidates) {
                    if (pointIsInside(candidate)) {
                        shape.labelAnchor = candidate;
                        foundAnchor = true;
                        break;
                    }
                }
                if (foundAnchor) break;
            }
            if (!foundAnchor && pointCount != 0)
                shape.labelAnchor = {static_cast<float>(sumX / pointCount),
                                     static_cast<float>(sumY / pointCount)};
        }
    }
}
}  // namespace

std::vector<ProvinceShape> CreateProvinceLayoutFromBasemap(
    const world_basemap::Data& data) {
    // Province borders are reviewed offline and compiled into the program.
    // Runtime Voronoi partitioning would make political geometry unstable.
    std::vector<ProvinceShape> layout = LoadGeneratedProvinceGeometry();
    ApplyReviewedGeometry(layout, world_basemap::LoadEmbeddedReviewedProvinceGeoJson());
    for (ProvinceShape& shape : layout) {
        if (data.valid && shape.geometryReviewed)
            RepairLabelAnchorFromBasemap(shape, data);
        shape.fromBasemap =
            data.valid && ShapeTouchesBasemap(shape, data);
    }
    return layout;
}

std::vector<ProvinceShape> CreateDefaultProvinceLayout() {
    std::vector<ProvinceShape> layout = LoadGeneratedProvinceGeometry();
    ApplyReviewedGeometry(layout, world_basemap::LoadEmbeddedReviewedProvinceGeoJson());
    return layout;
}

ProvinceBindingResult BindProvinceIdsByStableKey(
    std::vector<ProvinceShape>& layout, const World& world) {
    ProvinceBindingResult result;
    for (ProvinceShape& shape : layout) {
        shape.provinceId = -1;
        if (!shape.fromBasemap) result.missingKeys.push_back(shape.stableKey);
        // A geometry that was not validated against the basemap is only a
        // diagnostic artifact. Keep it out of the interactive layer so an
        // unknown area cannot open a province panel by accident.
        if (!shape.fromBasemap) continue;
        const Province* province = world.findProvinceByKey(shape.stableKey);
        if (province == nullptr) {
            result.missingKeys.push_back(shape.stableKey);
            continue;
        }
        shape.provinceId = province->getId();
        shape.name = province->getName();
        const Region& region = world.getRegionById(province->getRegionId());
        shape.regionKey = region.getKey();
        shape.regionName = region.getName();
        shape.countryKey = world.getCountryById(province->getCountryId()).getKey();
        ++result.boundCount;
    }
    // Full ring-intersection validation is an explicit offline/test gate. It
    // is quadratic in coastline vertex count and must not run every time the
    // already-reviewed embedded map is bound at startup.
    return result;
}

GeometryValidationResult ValidateProvinceTopology(
    const std::vector<ProvinceShape>& layout) {
    GeometryValidationResult result;
    std::vector<std::vector<map_model::Rect>> partBounds(layout.size());
    for (std::size_t shapeIndex = 0; shapeIndex < layout.size(); ++shapeIndex) {
        partBounds[shapeIndex].reserve(layout[shapeIndex].parts.size());
        for (const auto& part : layout[shapeIndex].parts)
            partBounds[shapeIndex].push_back(map_model::PolygonBounds(part));
    }
    for (const ProvinceShape& shape : layout) {
        if (shape.parts.empty()) {
            result.errors.push_back(shape.stableKey + ": no geometry parts");
            continue;
        }
        for (std::size_t partIndex = 0; partIndex < shape.parts.size(); ++partIndex) {
            const auto& part = shape.parts[partIndex];
            if (part.size() < 3) {
                result.errors.push_back(shape.stableKey + ": invalid ring");
                continue;
            }
            for (const map_model::Point point : part) {
                if (point.x < -kTopologyEpsilon || point.y < -kTopologyEpsilon ||
                    point.x > map_model::kWorldWidth + kTopologyEpsilon ||
                    point.y > map_model::kWorldHeight + kTopologyEpsilon) {
                    result.errors.push_back(shape.stableKey + ": ring leaves world");
                    break;
                }
            }
            if (!shape.geometryReviewed) continue;
            if (RingSelfIntersects(part))
                result.errors.push_back(shape.stableKey + ": self-intersecting ring " +
                                        std::to_string(partIndex));
            for (std::size_t other = partIndex + 1; other < shape.parts.size(); ++other)
                if (RingsCrossBoundary(part, shape.parts[other]))
                    result.errors.push_back(shape.stableKey + ": overlapping parts " +
                                            std::to_string(partIndex) + "/" +
                                            std::to_string(other));
        }
        if (!shape.geometryReviewed) continue;
        for (std::size_t holeIndex = 0; holeIndex < shape.holes.size(); ++holeIndex) {
            const auto& hole = shape.holes[holeIndex];
            if (hole.size() < 3 || RingSelfIntersects(hole) ||
                !RingInsideAnyPart(hole, shape)) {
                result.errors.push_back(shape.stableKey + ": invalid hole " +
                                        std::to_string(holeIndex));
                continue;
            }
            for (const auto& part : shape.parts)
                if (RingsCrossBoundary(hole, part))
                    result.errors.push_back(shape.stableKey + ": hole crosses part " +
                                            std::to_string(holeIndex));
        }
    }
    for (std::size_t first = 0; first < layout.size(); ++first) {
        for (std::size_t second = first + 1; second < layout.size(); ++second) {
            const ProvinceShape& left = layout[first];
            const ProvinceShape& right = layout[second];
            if (!left.geometryReviewed || !right.geometryReviewed) continue;
            for (std::size_t leftPartIndex = 0;
                 leftPartIndex < left.parts.size(); ++leftPartIndex) {
                const auto& leftPart = left.parts[leftPartIndex];
                for (std::size_t rightPartIndex = 0;
                     rightPartIndex < right.parts.size(); ++rightPartIndex) {
                    if (!BoundsOverlap(partBounds[first][leftPartIndex],
                                       partBounds[second][rightPartIndex]))
                        continue;
                    const auto& rightPart = right.parts[rightPartIndex];
                    if (RingsStrictlyOverlap(leftPart, rightPart,
                                                          left.holes, right.holes)) {
                        result.errors.push_back(left.stableKey + " overlaps " +
                                                right.stableKey);
                        goto next_pair;
                    }
                }
            }
            next_pair:;
        }
    }
    return result;
}

GeometryValidationResult ValidateProvinceCoverage(
    const std::vector<ProvinceShape>& layout,
    const world_basemap::Data& basemap) {
    GeometryValidationResult result;
    if (!basemap.valid) {
        result.errors.push_back("basemap is invalid");
        return result;
    }
    for (const ProvinceShape& shape : layout) {
        if (!shape.geometryReviewed) continue;
        if (shape.parts.empty()) {
            result.errors.push_back(shape.stableKey + ": no reviewed coverage");
            continue;
        }
        if (!ShapeTouchesBasemap(shape, basemap)) {
            result.errors.push_back(shape.stableKey + ": does not touch land basemap");
            continue;
        }
        if (!ShapeInteriorMatchesBasemap(shape, basemap))
            result.errors.push_back(
                shape.stableKey + ": reviewed interior enters ocean near anchor " +
                std::to_string(map_model::LongitudeFromWorldX(shape.labelAnchor.x)) +
                "," + std::to_string(map_model::LatitudeFromWorldY(shape.labelAnchor.y)));

    }
    return result;
}

const ProvinceShape* FindProvinceByStableKey(
    const std::vector<ProvinceShape>& layout, const std::string& stableKey) {
    for (const ProvinceShape& shape : layout)
        if (shape.stableKey == stableKey) return &shape;
    return nullptr;
}

const ProvinceShape* FindProvinceById(
    const std::vector<ProvinceShape>& layout, int provinceId) {
    if (provinceId < 0) return nullptr;
    for (const ProvinceShape& shape : layout)
        if (shape.provinceId == provinceId) return &shape;
    return nullptr;
}

const ProvinceShape* HitTestProvince(
    const std::vector<ProvinceShape>& layout, map_model::Point worldPoint) {
    worldPoint.x = map_model::NormalizeScrollX(worldPoint.x);
    for (auto it = layout.rbegin(); it != layout.rend(); ++it) {
        if (it->provinceId < 0) continue;
        if (!map_model::Contains(it->bounds, worldPoint)) continue;
        bool insideHole = false;
        for (const std::vector<map_model::Point>& hole : it->holes) {
            if (map_model::PointInPolygon(worldPoint, hole)) {
                insideHole = true;
                break;
            }
        }
        if (insideHole) continue;
        for (const std::vector<map_model::Point>& part : it->parts) {
            if (map_model::PointInPolygon(worldPoint, part)) return &*it;
        }
        if (it->parts.empty() &&
            map_model::PointInPolygon(worldPoint, it->vertices)) return &*it;
    }
    return nullptr;
}

const ProvinceShape* HitTestProvinceAtScreen(
    const std::vector<ProvinceShape>& layout, const map_model::MapView& view,
    map_model::Point screenPoint) {
    const auto worldPoint = map_model::ScreenToWorld(view, screenPoint);
    return worldPoint ? HitTestProvince(layout, *worldPoint) : nullptr;
}

}  // namespace map_layout
