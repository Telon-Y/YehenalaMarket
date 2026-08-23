#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

class World;
class Country;

namespace country_palette {

struct RgbaColor {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t a;

    constexpr bool operator==(const RgbaColor& other) const {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }
};

struct Entry {
    const char* code;
    RgbaColor color;
};

const std::array<Entry, 18>& entries();
RgbaColor colorForCode(std::string_view code);

// Resolve a country through its validated overlord chain. The map and tests
// use this same policy so subject countries cannot drift to their own color.
std::string politicalCountryCode(const World& world, const Country& country);
RgbaColor politicalColorForCountry(const World& world, const Country& country);

}  // namespace country_palette
