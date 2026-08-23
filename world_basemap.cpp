#include "world_basemap.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <fstream>
#include <iterator>
#include <utility>

namespace world_basemap {

const char* EmbeddedNaturalEarthGeoJson(std::size_t* size);
const char* EmbeddedReviewedProvinceGeoJson(std::size_t* size);

namespace {
using Point = map_model::Point;

float SignedArea(const std::vector<Point>& polygon) {
    if (polygon.size() < 3) return 0.0f;
    double area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const Point current = polygon[index];
        const Point next = polygon[(index + 1) % polygon.size()];
        area += static_cast<double>(current.x) * next.y -
                static_cast<double>(next.x) * current.y;
    }
    return static_cast<float>(area * 0.5);
}

std::vector<Point> ClipVertical(const std::vector<Point>& input,
                                float limit, bool keepGreater) {
    if (input.empty()) return {};
    const auto inside = [limit, keepGreater](Point point) {
        return keepGreater ? point.x >= limit : point.x <= limit;
    };
    const auto intersection = [limit](Point start, Point end) {
        const float denominator = end.x - start.x;
        if (std::abs(denominator) < 0.0001f) return start;
        const float fraction = (limit - start.x) / denominator;
        return Point{limit, start.y + (end.y - start.y) * fraction};
    };
    std::vector<Point> output;
    Point previous = input.back();
    bool previousInside = inside(previous);
    for (const Point current : input) {
        const bool currentInside = inside(current);
        if (currentInside != previousInside)
            output.push_back(intersection(previous, current));
        if (currentInside) output.push_back(current);
        previous = current;
        previousInside = currentInside;
    }
    return output;
}

std::vector<std::vector<Point>> SplitDatelineRing(std::vector<Point> ring) {
    if (ring.size() < 3) return {};
    const float width = map_model::kWorldWidth;
    for (std::size_t index = 1; index < ring.size(); ++index) {
        while (ring[index].x - ring[index - 1].x > width * 0.5f)
            ring[index].x -= width;
        while (ring[index].x - ring[index - 1].x < -width * 0.5f)
            ring[index].x += width;
    }
    float minX = ring.front().x;
    float maxX = ring.front().x;
    for (const Point point : ring) {
        minX = std::min(minX, point.x);
        maxX = std::max(maxX, point.x);
    }
    const int firstCopy = static_cast<int>(std::floor(minX / width)) - 1;
    const int lastCopy = static_cast<int>(std::ceil(maxX / width)) + 1;
    std::vector<std::vector<Point>> result;
    for (int copy = firstCopy; copy <= lastCopy; ++copy) {
        std::vector<Point> shifted = ring;
        const float offset = static_cast<float>(copy) * width;
        for (Point& point : shifted) point.x -= offset;
        shifted = ClipVertical(shifted, 0.0f, true);
        shifted = ClipVertical(shifted, width, false);
        if (shifted.size() >= 3 && std::abs(SignedArea(shifted)) > 0.0001f)
            result.push_back(std::move(shifted));
    }
    return result;
}

class JsonReader {
public:
    explicit JsonReader(const std::string& source) : source(source) {}

    bool parse(Data& output) {
        skipWhitespace();
        if (!parseObject([this, &output](const std::string& key) {
                if (key == "features") return parseFeatures(output);
                return skipValue();
            })) {
            if (error.empty()) error = "invalid GeoJSON root";
            output.error = error;
            return false;
        }
        output.valid = !output.polygons.empty();
        if (!output.valid && error.empty()) error = "GeoJSON contains no polygons";
        output.error = error;
        return output.valid;
    }

private:
    const std::string& source;
    std::size_t position = 0;
    std::string error;

    void skipWhitespace() {
        while (position < source.size() &&
               std::isspace(static_cast<unsigned char>(source[position]))) ++position;
    }
    bool fail(const char* message) {
        if (error.empty()) error = message;
        return false;
    }
    bool consume(char expected) {
        skipWhitespace();
        if (position >= source.size() || source[position] != expected)
            return fail("unexpected GeoJSON token");
        ++position;
        return true;
    }
    bool parseHexQuad(std::uint32_t& result) {
        if (position > source.size() || source.size() - position < 4)
            return fail("incomplete GeoJSON Unicode escape");
        result = 0;
        for (int index = 0; index < 4; ++index) {
            const unsigned char value =
                static_cast<unsigned char>(source[position++]);
            std::uint32_t digit = 0;
            if (value >= '0' && value <= '9')
                digit = value - '0';
            else if (value >= 'a' && value <= 'f')
                digit = value - 'a' + 10;
            else if (value >= 'A' && value <= 'F')
                digit = value - 'A' + 10;
            else
                return fail("invalid GeoJSON Unicode escape");
            result = (result << 4) | digit;
        }
        return true;
    }
    static void appendUtf8(std::string& result, std::uint32_t codepoint) {
        if (codepoint <= 0x7F) {
            result.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7FF) {
            result.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else if (codepoint <= 0xFFFF) {
            result.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
            result.push_back(
                static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        } else {
            result.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
            result.push_back(
                static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
            result.push_back(
                static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
            result.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
        }
    }
    bool parseString(std::string& result) {
        skipWhitespace();
        if (position >= source.size() || source[position] != '"')
            return fail("expected GeoJSON string");
        ++position;
        result.clear();
        while (position < source.size()) {
            const unsigned char value =
                static_cast<unsigned char>(source[position++]);
            if (value == '"') return true;
            if (value == '\\') {
                if (position >= source.size()) return fail("bad GeoJSON escape");
                const char escaped = source[position++];
                switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    std::uint32_t codepoint = 0;
                    if (!parseHexQuad(codepoint)) return false;
                    if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
                        if (position > source.size() ||
                            source.size() - position < 6 ||
                            source[position] != '\\' ||
                            source[position + 1] != 'u')
                            return fail("missing GeoJSON low surrogate");
                        position += 2;
                        std::uint32_t low = 0;
                        if (!parseHexQuad(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF)
                            return fail("invalid GeoJSON low surrogate");
                        codepoint = 0x10000 +
                            ((codepoint - 0xD800) << 10) +
                            (low - 0xDC00);
                    } else if (codepoint >= 0xDC00 &&
                               codepoint <= 0xDFFF) {
                        return fail("unexpected GeoJSON low surrogate");
                    }
                    appendUtf8(result, codepoint);
                    break;
                }
                default: return fail("unsupported GeoJSON escape");
                }
            } else {
                if (value < 0x20)
                    return fail("unescaped control character in GeoJSON string");
                result.push_back(static_cast<char>(value));
            }
        }
        return fail("unterminated GeoJSON string");
    }
    bool parseOptionalString(std::string& result) {
        skipWhitespace();
        if (position < source.size() && source[position] == 'n') return skipValue();
        if (position < source.size() && source[position] == '"') return parseString(result);
        return skipValue();
    }
    bool parseNumber(double& result) {
        skipWhitespace();
        if (position >= source.size()) return fail("expected GeoJSON number");
        const char* begin = source.c_str() + position;
        char* end = nullptr;
        result = std::strtod(begin, &end);
        if (end == begin) return fail("expected GeoJSON number");
        position += static_cast<std::size_t>(end - begin);
        return std::isfinite(result);
    }
    bool parseObject(const std::function<bool(const std::string&)>& handler) {
        if (!consume('{')) return false;
        skipWhitespace();
        if (position < source.size() && source[position] == '}') { ++position; return true; }
        while (position < source.size()) {
            std::string key;
            if (!parseString(key) || !consume(':') || !handler(key)) return false;
            skipWhitespace();
            if (position < source.size() && source[position] == ',') { ++position; continue; }
            if (position < source.size() && source[position] == '}') { ++position; return true; }
            return fail("malformed GeoJSON object");
        }
        return fail("unterminated GeoJSON object");
    }
    bool parseArray(const std::function<bool()>& itemParser) {
        if (!consume('[')) return false;
        skipWhitespace();
        if (position < source.size() && source[position] == ']') { ++position; return true; }
        while (position < source.size()) {
            if (!itemParser()) return false;
            skipWhitespace();
            if (position < source.size() && source[position] == ',') { ++position; continue; }
            if (position < source.size() && source[position] == ']') { ++position; return true; }
            return fail("malformed GeoJSON array");
        }
        return fail("unterminated GeoJSON array");
    }
    bool captureValue(std::string& result) {
        skipWhitespace();
        const std::size_t begin = position;
        if (!skipValue()) return false;
        result.assign(source, begin, position - begin);
        return true;
    }

    bool skipValue() {
        skipWhitespace();
        if (position >= source.size()) return fail("missing GeoJSON value");
        if (source[position] == '{') return parseObject([this](const std::string&) { return skipValue(); });
        if (source[position] == '[') return parseArray([this]() { return skipValue(); });
        if (source[position] == '"') { std::string ignored; return parseString(ignored); }
        if (source.compare(position, 4, "true") == 0) { position += 4; return true; }
        if (source.compare(position, 5, "false") == 0) { position += 5; return true; }
        if (source.compare(position, 4, "null") == 0) { position += 4; return true; }
        double ignored = 0.0;
        return parseNumber(ignored);
    }
    bool parseProperties(std::string& name, std::string& admin,
                         std::string& isoA3, std::string& stableKey,
                         std::string& regionKey, std::string& countryKey,
                         std::string& geometryMode) {
        return parseObject([this, &name, &admin, &isoA3, &stableKey,
                            &regionKey, &countryKey, &geometryMode](const std::string& key) {
            std::string value;
            if (key == "NAME" || key == "NAME_EN") {
                if (!parseOptionalString(value)) return false;
                if (name.empty()) name = value;
                return true;
            }
            if (key == "ADMIN") { if (!parseOptionalString(admin)) return false; return true; }
            if (key == "ISO_A3" || key == "ADM0_A3") { if (!parseOptionalString(isoA3)) return false; return true; }
            if (key == "stableKey") { if (!parseOptionalString(stableKey)) return false; return true; }
            if (key == "regionKey") { if (!parseOptionalString(regionKey)) return false; return true; }
            if (key == "countryKey") { if (!parseOptionalString(countryKey)) return false; return true; }
            if (key == "geometryMode") { if (!parseOptionalString(geometryMode)) return false; return true; }
            return skipValue();
        });
    }    bool parsePoint(Point& point) {
        double longitude = 0.0, latitude = 0.0;
        if (!consume('[') || !parseNumber(longitude) || !consume(',') || !parseNumber(latitude)) return false;
        while (true) {
            skipWhitespace();
            if (position >= source.size()) return fail("unterminated GeoJSON point");
            if (source[position] == ']') { ++position; point = map_model::ProjectLonLat(longitude, latitude); return true; }
            if (!consume(',')) return false;
            double ignored = 0.0;
            if (!parseNumber(ignored)) return false;
        }
    }
    bool parseRing(std::vector<Point>& ring) {
        ring.clear();
        return parseArray([this, &ring]() { Point point; if (!parsePoint(point)) return false; ring.push_back(point); return true; });
    }
    bool parsePolygon(Data& output) {
        std::vector<Point> outer;
        std::vector<std::vector<Point>> holes;
        bool first = true;
        if (!consume('[')) return false;
        skipWhitespace();
        if (position < source.size() && source[position] == ']') { ++position; return true; }
        while (position < source.size()) {
            std::vector<Point> ring;
            if (!parseRing(ring)) return false;
            if (first) { outer = std::move(ring); first = false; }
            else if (ring.size() >= 3) holes.push_back(std::move(ring));
            skipWhitespace();
            if (position < source.size() && source[position] == ',') { ++position; continue; }
            if (position < source.size() && source[position] == ']') { ++position; break; }
            return fail("malformed GeoJSON polygon");
        }
        if (outer.size() < 3) return true;
        for (std::vector<Point> part : SplitDatelineRing(std::move(outer))) {
            Polygon polygon;
            polygon.vertices = std::move(part);
            for (std::vector<Point>& hole : holes) {
                for (std::vector<Point> holePart : SplitDatelineRing(hole)) {
                    if (!holePart.empty() && map_model::PointInPolygon(holePart.front(), polygon.vertices))
                        polygon.holes.push_back(std::move(holePart));
                }
            }
            output.polygons.push_back(std::move(polygon));
        }
        return true;
    }
    bool parseCoordinates(const std::string& type, Data& output) {
        if (type == "Polygon") return parsePolygon(output);
        if (type == "MultiPolygon") return parseArray([this, &output]() { return parsePolygon(output); });
        return skipValue();
    }
    bool parseGeometry(Data& output) {
        skipWhitespace();
        if (position >= source.size())
            return fail("missing GeoJSON geometry");
        if (source[position] == 'n') return skipValue();
        std::string type;
        std::string coordinates;
        bool hasCoordinates = false;
        const bool parsed = parseObject(
            [this, &type, &coordinates,
             &hasCoordinates](const std::string& key) {
            if (key == "type") return parseString(type);
            if (key == "coordinates") {
                hasCoordinates = true;
                return captureValue(coordinates);
            }
            return skipValue();
        });
        if (!parsed || !hasCoordinates) return parsed;

        JsonReader coordinateReader(coordinates);
        if (!coordinateReader.parseCoordinates(type, output)) {
            if (error.empty()) {
                error = coordinateReader.error.empty()
                    ? "invalid GeoJSON coordinates"
                    : coordinateReader.error;
            }
            return false;
        }
        coordinateReader.skipWhitespace();
        if (coordinateReader.position != coordinates.size())
            return fail("trailing GeoJSON coordinates");
        return true;
    }
    bool parseFeature(Data& output) {
        const std::size_t firstPolygon = output.polygons.size();
        std::string name, admin, isoA3, stableKey, regionKey, countryKey,
                    geometryMode;
        const bool parsed = parseObject([this, &output, &name, &admin, &isoA3,
                                         &stableKey, &regionKey, &countryKey,
                                         &geometryMode](const std::string& key) {
            if (key == "geometry") return parseGeometry(output);
            if (key == "properties") {
                return parseProperties(name, admin, isoA3, stableKey, regionKey,
                                       countryKey, geometryMode);
            }
            return skipValue();
        });
        if (!parsed) return false;
        const std::size_t polygonEnd = output.polygons.size();
        for (std::size_t index = firstPolygon; index < polygonEnd; ++index) {
            output.polygons[index].name = name;
            output.polygons[index].admin = admin;
            output.polygons[index].isoA3 = isoA3;
            output.polygons[index].stableKey = stableKey;
            output.polygons[index].regionKey = regionKey;
            output.polygons[index].countryKey = countryKey;
            output.polygons[index].geometryMode = geometryMode;
        }
        if (name == "Antarctica" || admin == "Antarctica" || isoA3 == "ATA")
            output.polygons.erase(output.polygons.begin() + static_cast<std::ptrdiff_t>(firstPolygon),
                                  output.polygons.begin() + static_cast<std::ptrdiff_t>(polygonEnd));
        return true;
    }
    bool parseFeatures(Data& output) {
        return parseArray([this, &output]() { return parseFeature(output); });
    }
};
}  // namespace

Data ParseNaturalEarthGeoJson(const std::string& json) {
    Data output;
    JsonReader reader(json);
    reader.parse(output);
    if (!output.valid && output.error.empty()) output.error = "invalid GeoJSON";
    return output;
}

Data LoadNaturalEarthGeoJson(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {{}, false, "unable to open GeoJSON: " + path};
    const std::string contents((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return ParseNaturalEarthGeoJson(contents);
}

Data LoadEmbeddedNaturalEarthGeoJson() {
    std::size_t size = 0;
    const char* contents = EmbeddedNaturalEarthGeoJson(&size);
    if (contents == nullptr || size == 0) return {{}, false, "embedded GeoJSON resource is empty"};
    return ParseNaturalEarthGeoJson(std::string(contents, size));
}

Data LoadEmbeddedReviewedProvinceGeoJson() {
    std::size_t size = 0;
    const char* contents = EmbeddedReviewedProvinceGeoJson(&size);
    if (contents == nullptr || size == 0) return {{}, false, "embedded reviewed GeoJSON resource is empty"};
    return ParseNaturalEarthGeoJson(std::string(contents, size));
}

bool ContainsLand(const Data& data, map_model::Point point) {
    for (const Polygon& polygon : data.polygons) {
        if (!map_model::Contains(map_model::PolygonBounds(polygon.vertices),
                                 point) ||
            !map_model::PointInPolygon(point, polygon.vertices))
            continue;
        bool inHole = false;
        for (const std::vector<map_model::Point>& hole : polygon.holes) {
            if (map_model::PointInPolygon(point, hole)) {
                inHole = true;
                break;
            }
        }
        if (!inHole) return true;
    }
    return false;
}

}  // namespace world_basemap