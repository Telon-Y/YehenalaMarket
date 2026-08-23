#include "country_palette.h"
#include "world.h"

namespace country_palette {
namespace {

constexpr std::array<Entry, 18> kEntries = {{
    {"CHI", {242, 230, 167, 255}},
    {"FRA", {71, 119, 184, 255}},
    {"GBR", {200, 75, 75, 255}},
    {"PRU", {138, 106, 74, 255}},
    {"RUS", {142, 190, 111, 255}},
    {"AUS", {230, 228, 221, 255}},
    {"USA", {157, 203, 231, 255}},
    {"MEX", {78, 150, 98, 255}},
    {"BRA", {62, 155, 87, 255}},
    {"JAP", {184, 58, 66, 255}},
    {"SWE", {121, 198, 213, 255}},
    {"LCO", {229, 138, 50, 255}},
    {"ITA", {213, 108, 92, 255}},
    {"TUR", {196, 125, 105, 255}},
    {"PER", {184, 132, 83, 255}},
    {"IBE", {211, 164, 74, 255}},
    {"SWI", {205, 205, 205, 255}},
    {"NOR", {121, 198, 213, 255}},
}};

}  // namespace

const std::array<Entry, 18>& entries() {
    return kEntries;
}

RgbaColor colorForCode(std::string_view code) {
    for (const Entry& entry : kEntries)
        if (code == entry.code) return entry.color;
    return code.empty() ? RgbaColor{180, 184, 181, 255}
                        : RgbaColor{156, 164, 160, 255};
}

std::string politicalCountryCode(const World& world, const Country& country) {
    const Country* current = &country;
    for (int depth = 0; depth <= world.getCountryCount(); ++depth) {
        const int overlord = current->getOverlordCountryId();
        if (overlord < 0) return current->getCountryCode();
        try {
            current = &world.getCountryById(overlord);
        } catch (...) {
            return country.getCountryCode();
        }
    }
    return country.getCountryCode();
}

RgbaColor politicalColorForCountry(const World& world,
                                   const Country& country) {
    return colorForCode(politicalCountryCode(world, country));
}

}  // namespace country_palette
