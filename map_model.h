#pragma once

#include <optional>
#include <vector>

namespace map_model {

constexpr float kWorldWidth = 3840.0f;
constexpr float kWorldHeight = 2718.0f;
// The map uses a clipped Web-Mercator world. Antarctica is outside the
// playable map while the northern limit avoids the pole's singularity.
constexpr double kWorldMinLatitude = -60.0;
constexpr double kWorldMaxLatitude = 85.0;

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// scrollX is the world-space X coordinate at the viewport's left edge.
// worldOffsetY is the world-space Y coordinate at the viewport's top edge;
// wide screens may crop the polar margins to keep one world at a time.
struct MapView {
    Rect viewport;
    float scrollX = 0.0f;
    float worldWidth = kWorldWidth;
    float worldHeight = kWorldHeight;
    float worldOffsetY = 0.0f;
    float zoom = 1.0f;
};

struct EdgeScrollConfig {
    float edgeWidth = 96.0f;
    float maxSpeed = 720.0f;
};

struct LoopCopy {
    int repeatIndex = 0;
    float screenOriginX = 0.0f;
};

float PositiveModulo(float value, float modulus);
float NormalizeScrollX(float scrollX, float worldWidth = kWorldWidth);

// Returns world units per second. Negative values move toward the left edge
// of the world and positive values move toward the right edge.
float EdgeScrollVelocity(float cursorScreenX, const Rect& viewport,
                         const EdgeScrollConfig& config = {});
float AdvanceScrollX(float scrollX, float velocity, float deltaTime,
                     float worldWidth = kWorldWidth);

float ViewScale(const MapView& view);
bool Contains(const Rect& rect, Point point);
std::optional<Point> ScreenToWorld(const MapView& view, Point screenPoint);
Point WorldToScreen(const MapView& view, Point worldPoint, int repeatIndex = 0);

// Geographic helpers shared by the basemap loader and authored historical
// province geometry. The inverse is used for latitude grid lines and checks.
Point ProjectLonLat(double longitude, double latitude);
double LongitudeFromWorldX(float x);
double LatitudeFromWorldY(float y);

// Returns every horizontally repeated copy that intersects the viewport.
// Drawing each result covers the viewport without a seam, including when the
// viewport is wider than one complete world.
std::vector<LoopCopy> VisibleLoopCopies(const MapView& view);

Rect PolygonBounds(const std::vector<Point>& vertices);
bool PointInPolygon(Point point, const std::vector<Point>& vertices);

}  // namespace map_model
