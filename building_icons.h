#pragma once

#include "constants.h"
#include "commodity_icons.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>

// Pixel-art building glyphs. Main building badges use a 32x32 logical canvas
// with one screen pixel per cell whenever the destination permits it. Smaller
// table rows use nearest-neighbor projection of the same 32-cell source rather
// than maintaining a second, visually divergent sprite.
namespace building_icons {

inline Color Dim(Color c, bool enabled) {
    if (enabled) return c;
    const int gray = (30 * c.r + 59 * c.g + 11 * c.b) / 100;
    c.r = static_cast<unsigned char>((3 * gray + 186) / 4);
    c.g = static_cast<unsigned char>((3 * gray + 192) / 4);
    c.b = static_cast<unsigned char>((3 * gray + 188) / 4);
    return c;
}

inline Color PixelColor(char pixel, Color accent, bool enabled) {
    Color c{};
    switch (pixel) {
    case 'a': c = accent; break;
    case 's': c = {static_cast<unsigned char>(accent.r * 3 / 4), static_cast<unsigned char>(accent.g * 3 / 4), static_cast<unsigned char>(accent.b * 3 / 4), 255}; break;
    case 'd': c = {47, 60, 61, 255}; break;
    case 'l': c = {226, 218, 181, 255}; break;
    case 'w': c = {252, 247, 222, 255}; break;
    case 'b': c = {105, 171, 185, 255}; break;
    case 'y': c = {236, 192, 75, 255}; break;
    case 'g': c = {113, 158, 80, 255}; break;
    case 't': c = {143, 105, 66, 255}; break;
    case 'r': c = {182, 105, 78, 255}; break;
    case 'o': c = {147, 155, 157, 255}; break;
    default: return {0, 0, 0, 0};
    }
    return Dim(c, enabled);
}

inline void DrawBuildingPixelIcon(Rectangle bounds, int type,
                                  bool operational = true) {
    const int side = static_cast<int>(std::floor(std::min(bounds.width, bounds.height)));
    if (side <= 0) return;
    const int x = static_cast<int>(std::floor(bounds.x + (bounds.width - side) * 0.5f));
    const int y = static_cast<int>(std::floor(bounds.y + (bounds.height - side) * 0.5f));
    const Color border = operational ? Color{115, 137, 122, 255} : Color{161, 173, 165, 255};
    DrawRectangle(x, y, side, side, border);
    // The catalog is authored at 32 cells. A compact row projects that same
    // canvas into the available square while preserving hard pixel edges.
    if (side < 18) return;
    DrawRectangle(x + 1, y + 1, side - 2, side - 2,
                  operational ? Color{218, 230, 209, 255} : Color{225, 230, 224, 255});
    const std::array<Color, TYPE_COUNT> accents = {{
        {100, 145, 74, 255}, {191, 108, 63, 255}, {83, 142, 96, 255},
        {153, 90, 124, 255}, {124, 87, 147, 255}, {88, 101, 110, 255},
        {130, 132, 145, 255}, {172, 84, 62, 255}, {65, 116, 145, 255},
        {180, 119, 74, 255}, {194, 147, 55, 255}, {177, 143, 60, 255},
        {86, 111, 151, 255}, {110, 99, 151, 255}, {73, 127, 141, 255},
        {94, 137, 143, 255}, {73, 114, 121, 255}}};
    const int index = std::clamp(type, 0, TYPE_COUNT - 1);
    const Color accent = Dim(accents[index], operational);
    constexpr int logicalSize = 32;
    const int grid = std::min(side - 2, logicalSize);
    const int ox = x + (side - grid) / 2;
    const int oy = y + (side - grid) / 2;
    const auto P = [&](int gx, int gy, int gw, int gh, char color) {
        if (gw <= 0 || gh <= 0) return;
        const int left = ox + (gx * 2 * grid) / logicalSize;
        const int top = oy + (gy * 2 * grid) / logicalSize;
        const int right = ox + ((gx + gw) * 2 * grid) / logicalSize;
        const int bottom = oy + ((gy + gh) * 2 * grid) / logicalSize;
        if (right <= left || bottom <= top) return;
        DrawRectangle(left, top, right - left, bottom - top,
                      PixelColor(color, accent, operational));
    };
    // One-cell details are authored directly on the 32-cell canvas.
    const auto Q = [&](int gx, int gy, char color) {
        const int left = ox + (gx * grid) / logicalSize;
        const int top = oy + (gy * grid) / logicalSize;
        const int right = ox + ((gx + 1) * grid) / logicalSize;
        const int bottom = oy + ((gy + 1) * grid) / logicalSize;
        if (right <= left || bottom <= top) return;
        DrawRectangle(left, top, right - left, bottom - top,
                      PixelColor(color, accent, operational));
    };
    const auto Window = [&](int gx, int gy, int gw = 2, int gh = 2) { P(gx, gy, gw, gh, 'b'); };
    const auto Roof = [&](int gx, int gy, int width) {
        P(gx + width / 2, gy, 1, 1, 'd');
        for (int row = 1; row < 4; ++row) P(gx + width / 2 - row, gy + row, row * 2 + 1, 1, 'a');
    };
    switch (index) {
    case 0: // grain farm
        P(1, 11, 14, 2, 'g'); P(2, 13, 12, 1, 't');
        P(7, 6, 7, 6, 'd'); P(8, 7, 5, 5, 'l'); Window(9, 8); P(11, 9, 2, 3, 't');
        Roof(7, 3, 7); P(2, 5, 1, 6, 'g'); P(1, 6, 3, 1, 'y'); P(2, 4, 1, 1, 'y'); P(4, 7, 1, 4, 'g'); P(3, 8, 3, 1, 'y');
        Q(16, 14, 'w'); Q(18, 14, 'w'); Q(22, 16, 's'); Q(24, 18, 'w');
        break;
    case 1: // food factory
        P(2, 3, 2, 9, 'r'); P(3, 2, 1, 2, 'o'); P(5, 7, 9, 6, 'd'); P(6, 8, 7, 4, 'a');
        Window(7, 9); Window(10, 9); P(5, 5, 9, 2, 'a'); P(6, 6, 7, 1, 's'); P(2, 12, 12, 2, 'd');
        Q(12, 16, 'w'); Q(17, 16, 'w'); Q(23, 18, 'y'); Q(25, 18, 'w');
        break;
    case 2: // cotton plantation
        P(2, 11, 5, 2, 'g'); P(3, 5, 1, 7, 'g'); P(2, 5, 3, 2, 'w'); P(1, 6, 3, 2, 'w');
        P(9, 7, 5, 6, 'd'); Roof(9, 4, 5); Window(10, 8); P(11, 10, 2, 3, 't'); P(9, 13, 5, 1, 't');
        Q(6, 12, 'w'); Q(8, 10, 'g'); Q(22, 18, 'w');
        break;
    case 3: // clothes factory
        P(2, 6, 12, 7, 'd'); P(3, 7, 10, 5, 'a'); Window(4, 8); Window(8, 8); P(2, 4, 12, 2, 'a');
        P(7, 7, 2, 4, 'w'); P(6, 8, 4, 1, 'w'); P(5, 9, 6, 1, 'w'); P(4, 10, 8, 1, 'w'); P(6, 11, 4, 1, 'w'); P(7, 12, 2, 1, 'w');
        Q(8, 14, 'l'); Q(14, 14, 'w'); Q(20, 14, 'l'); Q(24, 20, 's');
        break;
    case 4: // luxury clothing atelier
        P(4, 4, 8, 9, 'd'); P(5, 5, 6, 7, 'l'); P(6, 6, 4, 4, 'a');
        P(7, 7, 2, 4, 'w'); P(5, 11, 6, 1, 'y'); P(6, 12, 4, 1, 'a'); P(6, 2, 4, 1, 'y'); P(7, 1, 2, 1, 'y');
        Q(13, 15, 'y'); Q(18, 15, 'w'); Q(20, 21, 's');
        break;
    case 5: // coal mine
        P(2, 4, 12, 9, 'o'); P(3, 5, 10, 8, 's'); P(4, 7, 8, 6, 'd'); P(5, 8, 6, 5, 't');
        P(5, 8, 1, 5, 'a'); P(10, 8, 1, 5, 'a'); P(1, 13, 14, 1, 'd'); P(3, 3, 10, 1, 'a');
        Q(8, 16, 'o'); Q(10, 18, 'd'); Q(14, 20, 'o'); Q(20, 22, 's');
        break;
    case 6: // iron mine headframe
        P(4, 2, 8, 1, 'd'); P(5, 3, 1, 9, 't'); P(10, 3, 1, 9, 't'); P(6, 5, 4, 1, 'a'); P(7, 6, 2, 6, 'd');
        P(9, 11, 5, 2, 'o'); P(8, 10, 1, 3, 't'); P(10, 10, 3, 1, 'r'); P(2, 13, 12, 1, 'd');
        Q(10, 8, 'l'); Q(20, 8, 's'); Q(22, 23, 'o'); Q(26, 22, 'w');
        break;
    case 7: // steel mill
        P(2, 5, 12, 8, 'd'); P(3, 6, 10, 6, 'a'); P(4, 7, 3, 5, 'r'); P(9, 7, 3, 5, 'r');
        P(6, 8, 4, 3, 'y'); P(6, 11, 4, 1, 'l'); P(3, 3, 2, 3, 'o'); P(11, 2, 2, 4, 'o'); P(2, 13, 12, 1, 'd');
        Q(7, 13, 'r'); Q(13, 15, 'y'); Q(20, 14, 'b'); Q(24, 12, 'o');
        break;
    case 8: // tool factory
        P(3, 6, 10, 7, 'd'); P(4, 7, 8, 5, 'a'); Window(5, 8); Window(8, 8); P(5, 3, 2, 2, 'o'); P(4, 2, 4, 1, 'd');
        P(6, 5, 1, 4, 't'); P(9, 4, 1, 6, 't'); P(8, 5, 4, 1, 'd'); P(2, 13, 12, 1, 'd');
        Q(10, 17, 'w'); Q(16, 17, 'b'); Q(22, 19, 'l'); Q(18, 8, 'o');
        break;
    case 9: // housing
        Roof(2, 2, 12); P(2, 6, 12, 7, 'd'); P(3, 7, 10, 5, 'l'); Window(4, 8); Window(9, 8); P(7, 9, 2, 4, 't'); P(3, 13, 10, 1, 'g');
        Q(8, 14, 'w'); Q(18, 14, 'w'); Q(14, 20, 's'); Q(22, 22, 'g');
        break;
    case 10: // construction crane
        P(2, 3, 11, 1, 'd'); P(3, 4, 9, 1, 'y'); P(9, 4, 1, 9, 't'); P(2, 4, 1, 8, 't'); P(3, 11, 9, 2, 'o'); P(5, 10, 3, 2, 'a'); P(11, 5, 2, 2, 'a');
        Q(6, 8, 'y'); Q(10, 8, 'w'); Q(22, 10, 'l'); Q(18, 24, 'o');
        break;
    case 11: // gold mine
        P(2, 4, 12, 9, 'o'); P(3, 5, 10, 8, 'a'); P(4, 7, 8, 6, 's'); P(5, 8, 6, 5, 't'); P(5, 8, 1, 5, 'y'); P(10, 8, 1, 5, 'y'); P(4, 2, 8, 1, 'y'); P(2, 13, 12, 1, 'd');
        Q(8, 14, 'y'); Q(12, 18, 'w'); Q(22, 20, 'y'); Q(26, 22, 'o');
        break;
    case 12: // central bank
        P(2, 5, 12, 8, 'd'); P(3, 6, 10, 1, 'a'); P(4, 7, 2, 5, 'l'); P(7, 7, 2, 5, 'l'); P(10, 7, 2, 5, 'l'); P(2, 13, 12, 1, 'a'); P(6, 2, 4, 2, 'y'); P(7, 1, 2, 1, 'y');
        Q(10, 18, 'w'); Q(16, 18, 'l'); Q(22, 18, 'w'); Q(16, 7, 'y');
        break;
    case 13: // financial district
        P(2, 6, 4, 7, 'd'); P(6, 3, 4, 10, 'a'); P(10, 5, 4, 8, 'd');
        Window(3, 8); Window(7, 5); Window(7, 8); Window(11, 7); Window(11, 10); P(2, 13, 12, 1, 'd');
        Q(7, 18, 'b'); Q(14, 12, 'l'); Q(22, 16, 'b'); Q(24, 22, 's');
        break;
    case 14: // industrial bank
        P(2, 5, 12, 8, 'd'); P(3, 6, 10, 2, 'a'); P(4, 8, 2, 4, 'l'); P(7, 8, 2, 4, 'l'); P(10, 8, 2, 4, 'l'); P(2, 13, 12, 1, 'a'); P(6, 3, 4, 2, 'y');
        Q(10, 17, 'w'); Q(16, 17, 'l'); Q(22, 17, 'w'); Q(18, 10, 'y');
        break;
    case 15: // savings bank
        P(3, 5, 10, 8, 'd'); P(4, 6, 8, 6, 'l'); P(5, 7, 2, 3, 'b'); P(9, 7, 2, 3, 'b'); P(6, 10, 4, 2, 'a'); P(2, 13, 12, 1, 'a'); P(7, 2, 2, 2, 'y'); P(6, 3, 4, 1, 'y');
        Q(11, 17, 'b'); Q(19, 17, 'w'); Q(15, 22, 's'); Q(20, 8, 'y');
        break;
    case 16: // railway hub
        P(2, 6, 12, 7, 'd'); P(3, 7, 10, 5, 'l'); P(4, 8, 2, 2, 'b'); P(7, 8, 2, 2, 'b'); P(10, 8, 2, 2, 'b');
        P(6, 2, 4, 3, 'd'); P(7, 3, 2, 1, 'y'); P(2, 13, 12, 1, 'a'); P(3, 14, 4, 1, 't'); P(9, 14, 4, 1, 't');
        Q(10, 16, 'b'); Q(16, 16, 'w'); Q(22, 16, 'b'); Q(14, 26, 'o'); Q(20, 26, 'o');
        break;
    }
}

// Draw the building first, then place a compact product badge inside its
// lower-right corner. Financial buildings pass outputGood = -1 and therefore
// keep their architectural icon without implying a manufactured good.
inline void DrawBuildingProductionIcon(Rectangle bounds, int type,
                                       int outputGood,
                                       bool operational = true) {
    DrawBuildingPixelIcon(bounds, type, operational);
    if (outputGood < 0 || outputGood >= NUM_GOODS) return;
    const int side = static_cast<int>(std::floor(std::min(bounds.width,
                                                          bounds.height)));
    if (side < 20) return;
    const int badgeSide = std::min(side, 8 * std::max(1, side / 20) + 2);
    const Rectangle badge = {
        bounds.x + bounds.width - badgeSide,
        bounds.y + bounds.height - badgeSide,
        static_cast<float>(badgeSide), static_cast<float>(badgeSide)};
    commodity_icons::DrawCommodityPixelIcon(badge, outputGood, operational);
}

}  // namespace building_icons
