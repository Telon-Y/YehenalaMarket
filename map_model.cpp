#include "map_model.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace map_model {
namespace {
constexpr float kGeometryEpsilon = 0.0001f;

bool PointOnSegment(Point point, Point start, Point end) {
    const double dx = static_cast<double>(end.x) - start.x;
    const double dy = static_cast<double>(end.y) - start.y;
    const double pointDx = static_cast<double>(point.x) - start.x;
    const double pointDy = static_cast<double>(point.y) - start.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= kGeometryEpsilon * kGeometryEpsilon)
        return pointDx * pointDx + pointDy * pointDy <=
               kGeometryEpsilon * kGeometryEpsilon;
    const double cross = pointDx * dy - pointDy * dx;
    const double scale = std::max(1.0, std::abs(dx) + std::abs(dy));
    if (std::abs(cross) > static_cast<double>(kGeometryEpsilon) * scale)
        return false;
    const double dot = pointDx * dx + pointDy * dy;
    if (dot < -kGeometryEpsilon) return false;
    return dot <= lengthSquared + kGeometryEpsilon;
}

constexpr double kPi = 3.14159265358979323846;

double MercatorY(double latitude) {
    const double clamped = std::clamp(
        latitude, kWorldMinLatitude, kWorldMaxLatitude);
    const double radians = clamped * kPi / 180.0;
    return std::log(std::tan(kPi * 0.25 + radians * 0.5));
}

double MercatorMinY() { return MercatorY(kWorldMinLatitude); }
double MercatorMaxY() { return MercatorY(kWorldMaxLatitude); }
}  // namespace

Point ProjectLonLat(double longitude, double latitude) {
    const double normalizedLongitude = std::clamp(longitude, -180.0, 180.0);
    const double x = (normalizedLongitude + 180.0) / 360.0 * kWorldWidth;
    const double y = (MercatorMaxY() - MercatorY(latitude)) /
                     (MercatorMaxY() - MercatorMinY()) * kWorldHeight;
    return {static_cast<float>(x), static_cast<float>(y)};
}

double LongitudeFromWorldX(float x) {
    return static_cast<double>(x) / kWorldWidth * 360.0 - 180.0;
}

double LatitudeFromWorldY(float y) {
    const double fraction = std::clamp(
        static_cast<double>(y) / kWorldHeight, 0.0, 1.0);
    const double mercator = MercatorMaxY() -
                            fraction * (MercatorMaxY() - MercatorMinY());
    const double latitude = (2.0 * std::atan(std::exp(mercator)) - kPi * 0.5) *
                            180.0 / kPi;
    return std::clamp(latitude, kWorldMinLatitude, kWorldMaxLatitude);
}

float PositiveModulo(float value, float modulus) {
    if (!std::isfinite(value) || !std::isfinite(modulus) || modulus <= 0.0f)
        return 0.0f;
    float result = std::fmod(value, modulus);
    if (result < 0.0f) result += modulus;
    return result >= modulus ? 0.0f : result;
}

float NormalizeScrollX(float scrollX, float worldWidth) {
    return PositiveModulo(scrollX, worldWidth);
}

float EdgeScrollVelocity(float cursorScreenX, const Rect& viewport,
                         const EdgeScrollConfig& config) {
    if (viewport.width <= 0.0f || config.edgeWidth <= 0.0f ||
        config.maxSpeed <= 0.0f || !std::isfinite(cursorScreenX)) {
        return 0.0f;
    }
    const float edgeWidth = std::min(config.edgeWidth, viewport.width * 0.5f);
    if (edgeWidth <= 0.0f) return 0.0f;
    const float left = viewport.x;
    const float right = viewport.x + viewport.width;
    if (cursorScreenX < left + edgeWidth) {
        const float intensity = std::clamp(
            (left + edgeWidth - cursorScreenX) / edgeWidth, 0.0f, 1.0f);
        return -config.maxSpeed * intensity;
    }
    if (cursorScreenX > right - edgeWidth) {
        const float intensity = std::clamp(
            (cursorScreenX - (right - edgeWidth)) / edgeWidth, 0.0f, 1.0f);
        return config.maxSpeed * intensity;
    }
    return 0.0f;
}

float AdvanceScrollX(float scrollX, float velocity, float deltaTime,
                     float worldWidth) {
    if (!std::isfinite(deltaTime) || deltaTime <= 0.0f ||
        !std::isfinite(velocity)) {
        return NormalizeScrollX(scrollX, worldWidth);
    }
    const double advanced = static_cast<double>(scrollX) +
                            static_cast<double>(velocity) * deltaTime;
    if (!std::isfinite(advanced)) return NormalizeScrollX(scrollX, worldWidth);
    return NormalizeScrollX(static_cast<float>(advanced), worldWidth);
}

float ViewScale(const MapView& view) {
    if (view.viewport.height <= 0.0f || view.viewport.width <= 0.0f ||
        view.worldWidth <= 0.0f || view.worldHeight <= 0.0f ||
        !std::isfinite(view.viewport.height) ||
        !std::isfinite(view.viewport.width) ||
        !std::isfinite(view.worldWidth) ||
        !std::isfinite(view.worldHeight)) return 0.0f;
    const float baseScale = std::max(view.viewport.width / view.worldWidth,
                                     view.viewport.height / view.worldHeight);
    const float zoom = std::isfinite(view.zoom) ? std::clamp(view.zoom, 0.5f, 4.0f) : 1.0f;
    return baseScale * zoom;
}

bool Contains(const Rect& rect, Point point) {
    return rect.width >= 0.0f && rect.height >= 0.0f &&
           point.x >= rect.x && point.x <= rect.x + rect.width &&
           point.y >= rect.y && point.y <= rect.y + rect.height;
}

std::optional<Point> ScreenToWorld(const MapView& view, Point screenPoint) {
    if (!Contains(view.viewport, screenPoint)) return std::nullopt;
    const float scale = ViewScale(view);
    if (scale <= 0.0f || view.worldWidth <= 0.0f) return std::nullopt;
    Point result;
    result.x = NormalizeScrollX(
        view.scrollX + (screenPoint.x - view.viewport.x) / scale,
        view.worldWidth);
    result.y = view.worldOffsetY + (screenPoint.y - view.viewport.y) / scale;
    return result;
}

Point WorldToScreen(const MapView& view, Point worldPoint, int repeatIndex) {
    const float scale = ViewScale(view);
    const float normalizedScroll = NormalizeScrollX(view.scrollX,
                                                    view.worldWidth);
    return {
        view.viewport.x +
            (worldPoint.x - normalizedScroll +
             static_cast<float>(repeatIndex) * view.worldWidth) * scale,
        view.viewport.y + (worldPoint.y - view.worldOffsetY) * scale,
    };
}

std::vector<LoopCopy> VisibleLoopCopies(const MapView& view) {
    std::vector<LoopCopy> copies;
    const float scale = ViewScale(view);
    if (scale <= 0.0f || view.worldWidth <= 0.0f ||
        view.viewport.width <= 0.0f) return copies;
    const double period = static_cast<double>(view.worldWidth) * scale;
    const double left = view.viewport.x;
    const double right = left + view.viewport.width;
    const double baseOrigin = left -
        static_cast<double>(NormalizeScrollX(view.scrollX, view.worldWidth)) * scale;
    int firstIndex = static_cast<int>(std::floor((left - baseOrigin) / period));
    double origin = baseOrigin + static_cast<double>(firstIndex) * period;
    while (origin > left) { --firstIndex; origin -= period; }
    while (origin + period < left - kGeometryEpsilon) {
        ++firstIndex;
        origin += period;
    }
    // ViewScale makes one world at least as wide as the viewport. A second
    // copy is allowed only when the Pacific seam falls inside the viewport;
    // it is a partial seam fragment, never a second complete world.
    constexpr int maxCopies = 2;
    int repeatIndex = firstIndex;
    for (int count = 0; count < maxCopies && origin < right; ++count) {
        copies.push_back({repeatIndex, static_cast<float>(origin)});
        ++repeatIndex;
        origin += period;
    }
    return copies;
}

Rect PolygonBounds(const std::vector<Point>& vertices) {
    if (vertices.empty()) return {};
    float minX = vertices.front().x;
    float maxX = vertices.front().x;
    float minY = vertices.front().y;
    float maxY = vertices.front().y;
    for (const Point point : vertices) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
        minY = std::min(minY, point.y);
        maxY = std::max(maxY, point.y);
    }
    return {minX, minY, maxX - minX, maxY - minY};
}

bool PointInPolygon(Point point, const std::vector<Point>& vertices) {
    if (vertices.size() < 3) return false;
    bool inside = false;
    size_t previous = vertices.size() - 1;
    for (size_t current = 0; current < vertices.size(); ++current) {
        const Point a = vertices[previous];
        const Point b = vertices[current];
        if (PointOnSegment(point, a, b)) return true;
        const bool crosses = (a.y > point.y) != (b.y > point.y);
        if (crosses) {
            const double intersectionX = static_cast<double>(b.x - a.x) *
                                             (point.y - a.y) / (b.y - a.y) + a.x;
            if (static_cast<double>(point.x) < intersectionX) inside = !inside;
        }
        previous = current;
    }
    return inside;
}

}  // namespace map_model