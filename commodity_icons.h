#pragma once

#include "constants.h"
#include "raylib.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string_view>

// Commodity badges use a 16x16 pixel grid. A 10px compact row samples the
// same sprites at 8x8, while an 18px badge shows every authored pixel cell.
namespace commodity_icons {
namespace detail {

constexpr int kGridSize = 16;
using Sprite = std::array<std::string_view, kGridSize>;

inline constexpr std::array<Sprite, 13> kSprites{{
    Sprite{{ // Grain: a golden wheat ear with a brown stem.
        "......yyyy......",
        "......yyyy......",
        "....yyttyyyy....",
        "....yyttyyyy....",
        "..yyyyttyy......",
        "..yyyyttyy......",
        "....yyttyyyy....",
        "....yyttyyyy....",
        "..yyyyttyy......",
        "..yyyyttyy......",
        "......tt........",
        "......tt........",
        "......tt........",
        "......tt........",
        "................",
        "................"}},
    Sprite{{ // Processed food: a loaf with pale scoring and a dark crust.
        "................",
        "................",
        "....oooooooo....",
        "....oooooooo....",
        "..oohhhhhhhhoo..",
        "..oohhhhhhhhoo..",
        "oohhwwhhwwhhhhoo",
        "oohhwwhhwwhhhhoo",
        "oohhhhwwhhhhhhoo",
        "oohhhhwwhhhhhhoo",
        "..oottttttttoo..",
        "..oottttttttoo..",
        "....oooooooo....",
        "....oooooooo....",
        "................",
        "................"}},
    Sprite{{ // Fabric: a blue cloth roll and its light folded edge.
        "................",
        "................",
        "..oooooooooooo..",
        "..oooooooooooo..",
        "oobbbbbbbbaaaaoo",
        "oobbbbbbbbaaaaoo",
        "oobbbbbbaabboo..",
        "oobbbbbbaabboo..",
        "oobbwwwwbboo....",
        "oobbwwwwbboo....",
        "oobbbbbbbboo....",
        "oobbbbbbbboo....",
        "..oooooooo......",
        "..oooooooo......",
        "................",
        "................"}},
    Sprite{{ // Clothes: a short blue shirt with sleeves and a light collar.
        "....oo....oo....",
        "....oo....oo....",
        "..oobbbbbbbboo..",
        "..oobbbbbbbboo..",
        "oobbwwbbbbwwbboo",
        "oobbwwbbbbwwbboo",
        "oooobbbbbbbboooo",
        "oooobbbbbbbboooo",
        "....oobbbboo....",
        "....oobbbboo....",
        "....oobbbboo....",
        "....oobbbboo....",
        "....oooooooo....",
        "....oooooooo....",
        "................",
        "................"}},
    Sprite{{ // Luxury clothes: a long purple coat with gold trim.
        "....ooyyyyoo....",
        "....ooyyyyoo....",
        "..oovvwwwwvvoo..",
        "..oovvwwwwvvoo..",
        "oovvwwvvvvwwvvoo",
        "oovvwwvvvvwwvvoo",
        "oooovvyyvvvvoooo",
        "oooovvyyvvvvoooo",
        "....oovvvvoo....",
        "....oovvvvoo....",
        "..oovvvvvvvvoo..",
        "..oovvvvvvvvoo..",
        "..ooyyyyyyyyoo..",
        "..ooyyyyyyyyoo..",
        "..oooooooooooo..",
        "..oooooooooooo.."}},
    Sprite{{ // Coal: an uneven dark lump with a small cool highlight.
        "................",
        "................",
        "......oooooo....",
        "......oooooo....",
        "....ooccccssoo..",
        "....ooccccssoo..",
        "..ooccccccccccoo",
        "..ooccccccccccoo",
        "ooccccssccccccoo",
        "ooccccssccccccoo",
        "ooccccccccccoo..",
        "ooccccccccccoo..",
        "..ooccccccoo....",
        "..ooccccccoo....",
        "....oooooo......",
        "....oooooo......"}},
    Sprite{{ // Iron: raw ore, distinguished by its rust-colored seam.
        "................",
        "................",
        "....oooooooo....",
        "....oooooooo....",
        "..oorrmmmmmmoo..",
        "..oorrmmmmmmoo..",
        "oorrmmrrrrmmmmoo",
        "oorrmmrrrrmmmmoo",
        "oommrrrrrrmmmmoo",
        "oommrrrrrrmmmmoo",
        "..oorrmmmmmmoo..",
        "..oorrmmmmmmoo..",
        "....oooooooo....",
        "....oooooooo....",
        "................",
        "................"}},
    Sprite{{ // Steel: a straight, reflective I-beam.
        "..oooooooooooo..",
        "..oooooooooooo..",
        "..oommmmmmmmoo..",
        "..oommmmmmmmoo..",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "....oossssoo....",
        "..oommmmmmmmoo..",
        "..oommmmmmmmoo..",
        "..oooooooooooo..",
        "..oooooooooooo.."}},
    Sprite{{ // Tools: a broad metal hammer head and a wooden handle.
        "..oooooooo......",
        "..oooooooo......",
        "oommmmmmmmoo....",
        "oommmmmmmmoo....",
        "..oooottoo......",
        "..oooottoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......ttoo......",
        "......oo........",
        "......oo........"}},
    Sprite{{ // Housing: a house key, representing the housing service.
        "..oooooo........",
        "..oooooo........",
        "ooyyyyyyoo......",
        "ooyyyyyyoo......",
        "ooyyooyyoo......",
        "ooyyooyyoo......",
        "..ooyyyyoo......",
        "..ooyyyyoo......",
        "......yyoo......",
        "......yyoo......",
        "......yyoooo....",
        "......yyoooo....",
        "......yyyyyyoo..",
        "......yyyyyyoo..",
        "......oooooo....",
        "......oooooo...."}},
    Sprite{{ // Construction capacity: a yellow hard hat with a wide brim.
        "................",
        "................",
        "......yyyy......",
        "......yyyy......",
        "....yyyyyyyy....",
        "....yyyyyyyy....",
        "..yyyyyyyyyyyy..",
        "..yyyyyyyyyyyy..",
        "..yyyywwwwyyyy..",
        "..yyyywwwwyyyy..",
        "ooyyyyyyyyyyyyoo",
        "ooyyyyyyyyyyyyoo",
        "..oooooooooooo..",
        "..oooooooooooo..",
        "................",
        "................"}},
    Sprite{{ // Precious metal: two offset gold ingots with bright faces.
        "................",
        "................",
        "......oooooooo..",
        "......oooooooo..",
        "....ooyyyywwyyoo",
        "....ooyyyywwyyoo",
        "....oooooooooooo",
        "....oooooooooooo",
        "..oooooooo......",
        "..oooooooo......",
        "ooyyyywwyyoo....",
        "ooyyyywwyyoo....",
        "oooooooooooo....",
        "oooooooooooo....",
        "................",
        "................"}},
    Sprite{{ // Transport capacity: a train front, windows, lamps and rails.
        "....oooooooo....",
        "....oooooooo....",
        "..oobbbbbbbboo..",
        "..oobbbbbbbboo..",
        "..oowwwwwwwwoo..",
        "..oowwwwwwwwoo..",
        "..oobbbbbbbboo..",
        "..oobbbbbbbboo..",
        "..oobbyyyybboo..",
        "..oobbyyyybboo..",
        "..oooooooooooo..",
        "..oooooooooooo..",
        "....oo....oo....",
        "....oo....oo....",
        "..oooo....oooo..",
        "..oooo....oooo.."}},
}};

constexpr bool IsPalettePixel(char pixel) {
    switch (pixel) {
    case '.': case 'o': case 'w': case 'y': case 't': case 'h':
    case 'b': case 'a': case 'v': case 'c': case 's': case 'm': case 'r':
        return true;
    default:
        return false;
    }
}

constexpr bool ValidSprites() {
    for (const Sprite& sprite : kSprites) {
        for (const std::string_view row : sprite) {
            if (row.size() != kGridSize) return false;
            for (const char pixel : row) {
                if (!IsPalettePixel(pixel)) return false;
            }
        }
    }
    return true;
}

static_assert(kSprites.size() == NUM_GOODS,
              "Every commodity requires a pixel icon.");
static_assert(ValidSprites(),
              "Commodity sprites must have sixteen rows of sixteen palette cells.");

inline Color Desaturate(Color color, bool enabled) {
    if (enabled) return color;
    const int gray = (30 * color.r + 59 * color.g + 11 * color.b) / 100;
    // Slightly lift disabled ink while retaining enough contrast on the face.
    color.r = static_cast<unsigned char>((4 * gray + 226) / 5);
    color.g = static_cast<unsigned char>((4 * gray + 230) / 5);
    color.b = static_cast<unsigned char>((4 * gray + 224) / 5);
    return color;
}

inline Color PixelColor(char pixel, bool enabled) {
    Color color{};
    switch (pixel) {
    case 'o': color = {43, 53, 55, 255}; break;
    case 'w': color = {255, 249, 224, 255}; break;
    case 'y': color = {239, 192, 65, 255}; break;
    case 't': color = {136, 87, 49, 255}; break;
    case 'h': color = {210, 148, 79, 255}; break;
    case 'b': color = {65, 128, 165, 255}; break;
    case 'a': color = {158, 205, 217, 255}; break;
    case 'v': color = {126, 78, 151, 255}; break;
    case 'c': color = {66, 71, 79, 255}; break;
    case 's': color = {97, 118, 133, 255}; break;
    case 'm': color = {170, 190, 198, 255}; break;
    case 'r': color = {179, 105, 67, 255}; break;
    default: return {0, 0, 0, 0};
    }
    return Desaturate(color, enabled);
}

}  // namespace detail

inline void DrawCommodityPixelIcon(Rectangle bounds, int good,
                                   bool enabled = true) {
    if (good < 0 || good >= NUM_GOODS ||
        !std::isfinite(bounds.x) || !std::isfinite(bounds.y) ||
        !std::isfinite(bounds.width) || !std::isfinite(bounds.height) ||
        bounds.width <= 0.0f || bounds.height <= 0.0f) return;

    // Inset fractional bounds to whole pixels so neither the square frame nor
    // a glyph cell can spill into a neighboring label or outside a clipped row.
    const int left = static_cast<int>(std::ceil(bounds.x));
    const int top = static_cast<int>(std::ceil(bounds.y));
    const int right = static_cast<int>(std::floor(bounds.x + bounds.width));
    const int bottom = static_cast<int>(std::floor(bounds.y + bounds.height));
    const int side = std::min(right - left, bottom - top);
    const int logicalGrid = side >= detail::kGridSize + 2
        ? detail::kGridSize : detail::kGridSize / 2;
    if (side < logicalGrid + 2) return;
    const int x = left + (right - left - side) / 2;
    const int y = top + (bottom - top - side) / 2;

    DrawRectangle(x, y, side, side,
                  detail::Desaturate(Color{81, 94, 88, 255}, enabled));
    DrawRectangle(x + 1, y + 1, side - 2, side - 2,
                  detail::Desaturate(Color{251, 246, 224, 255}, enabled));

    const int cell = (side - 2) / logicalGrid;
    const int grid = cell * logicalGrid;
    const int originX = x + (side - grid) / 2;
    const int originY = y + (side - grid) / 2;
    const detail::Sprite& sprite =
        detail::kSprites[static_cast<std::size_t>(good)];
    for (int row = 0; row < logicalGrid; ++row) {
        for (int column = 0; column < logicalGrid; ++column) {
            const int sourceRow = logicalGrid == detail::kGridSize ? row : row * 2;
            const int sourceColumn = logicalGrid == detail::kGridSize ? column : column * 2;
            const char pixel = sprite[static_cast<std::size_t>(sourceRow)]
                                     [static_cast<std::size_t>(sourceColumn)];
            if (pixel == '.') continue;
            DrawRectangle(originX + column * cell, originY + row * cell,
                          cell, cell, detail::PixelColor(pixel, enabled));
        }
    }
}

}  // namespace commodity_icons

