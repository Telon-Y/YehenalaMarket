#pragma once

#include "map_model.h"

#include <cstddef>
#include <string>
#include <vector>

namespace world_basemap {

struct Polygon {
    std::vector<map_model::Point> vertices;
    // Interior rings are retained so callers can avoid filling lakes or
    // enclaves when rendering the low-detail basemap.
    std::vector<std::vector<map_model::Point>> holes;
    std::string name;
    std::string admin;
    std::string isoA3;
    // Reviewed province resources carry an explicit binding key. Natural
    // Earth country resources leave these fields empty.
    std::string stableKey;
    std::string regionKey;
    std::string countryKey;
    std::string geometryMode;
};

struct Data {
    std::vector<Polygon> polygons;
    bool valid = false;
    std::string error;
};

// Parses a Natural Earth FeatureCollection and projects lon/lat into the map
// model's clipped 3840x2718 Web-Mercator world space. Polygon holes are retained
// so the renderer can keep lakes and enclaves out of the neutral land base.
Data ParseNaturalEarthGeoJson(const std::string& json);
Data LoadNaturalEarthGeoJson(const std::string& path);

// Loads the copy compiled into the application. The external GeoJSON file
// may still be copied beside the executable for inspection, but the
// executable remains usable when it is distributed on its own.
Data LoadEmbeddedNaturalEarthGeoJson();

// Loads the offline-reviewed province geometry compiled into the application.
Data LoadEmbeddedReviewedProvinceGeoJson();

// Returns true when a projected point lies on a land polygon in the
// basemap. Interior rings are treated as water so callers can keep the
// visual base layer and the interactive province layer separate.
bool ContainsLand(const Data& data, map_model::Point point);

}  // namespace world_basemap