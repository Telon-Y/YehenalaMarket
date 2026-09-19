#include "ui_country_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace ui_country {
const std::string& CountryCode(const CountrySnapshot& country) {
    return country.countryCode.empty() ? country.tag : country.countryCode;
}

Color TagColor(const std::string& tag, int salt) {
    std::uint32_t hash = 2166136261u + static_cast<std::uint32_t>(salt);
    for (unsigned char value : tag) {
        hash ^= value;
        hash *= 16777619u;
    }
    return {static_cast<unsigned char>(55 + (hash & 0x9f)),
            static_cast<unsigned char>(55 + ((hash >> 8) & 0x9f)),
            static_cast<unsigned char>(55 + ((hash >> 16) & 0x9f)), 255};
}

template <std::size_t N>
void DrawHorizontalBands(Rectangle flag,
                         const std::array<Color, N>& colors) {
    for (std::size_t index = 0; index < N; ++index) {
        DrawRectangleRec({
            flag.x,
            flag.y + flag.height * static_cast<float>(index) /
                static_cast<float>(N),
            flag.width,
            flag.height / static_cast<float>(N) + 1.0f
        }, colors[index]);
    }
}

template <std::size_t N>
void DrawVerticalBands(Rectangle flag, const std::array<Color, N>& colors) {
    for (std::size_t index = 0; index < N; ++index) {
        DrawRectangleRec({
            flag.x + flag.width * static_cast<float>(index) /
                static_cast<float>(N),
            flag.y,
            flag.width / static_cast<float>(N) + 1.0f,
            flag.height
        }, colors[index]);
    }
}

void DrawStar(Vector2 center, float outerRadius, float innerRadius,
              int points, Color color, float rotation = -90.0f) {
    if (points < 3) return;
    std::vector<Vector2> vertices;
    vertices.reserve(static_cast<std::size_t>(points) * 2);
    constexpr float pi = 3.14159265358979323846f;
    for (int index = 0; index < points * 2; ++index) {
        const float angle = (rotation + index * 180.0f / points) * pi / 180.0f;
        const float radius = index % 2 == 0 ? outerRadius : innerRadius;
        vertices.push_back({
            center.x + std::cos(angle) * radius,
            center.y + std::sin(angle) * radius
        });
    }
    DrawTriangleFan(vertices.data(), static_cast<int>(vertices.size()), color);
}

void DrawCrescent(Rectangle flag, Vector2 center, float radius,
                  Color crescentColor, Color cutoutColor, float offset) {
    (void)flag;
    DrawCircleV(center, radius, crescentColor);
    DrawCircleV({center.x + offset, center.y - radius * 0.10f},
                radius * 0.82f, cutoutColor);
}

void DrawUnionJack(Rectangle flag) {
    DrawRectangleRec(flag, {35, 68, 132, 255});
    const Vector2 topLeft = {flag.x, flag.y};
    const Vector2 topRight = {flag.x + flag.width, flag.y};
    const Vector2 bottomLeft = {flag.x, flag.y + flag.height};
    const Vector2 bottomRight = {flag.x + flag.width, flag.y + flag.height};
    const float shortSide = std::min(flag.width, flag.height);
    const float whiteDiagonal = std::max(1.0f, shortSide * 0.20f);
    const float redDiagonal = std::max(1.0f, shortSide * 0.09f);
    DrawLineEx(topLeft, bottomRight, whiteDiagonal, RAYWHITE);
    DrawLineEx(topRight, bottomLeft, whiteDiagonal, RAYWHITE);
    DrawLineEx(topLeft, bottomRight, redDiagonal, {207, 45, 52, 255});
    DrawLineEx(topRight, bottomLeft, redDiagonal, {207, 45, 52, 255});
    DrawRectangleRec({flag.x + flag.width * 0.40f, flag.y,
                      flag.width * 0.20f, flag.height}, RAYWHITE);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.36f,
                      flag.width, flag.height * 0.28f}, RAYWHITE);
    DrawRectangleRec({flag.x + flag.width * 0.455f, flag.y,
                      flag.width * 0.09f, flag.height}, {207, 45, 52, 255});
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.455f,
                      flag.width, flag.height * 0.09f}, {207, 45, 52, 255});
}

void DrawQingFlag(Rectangle flag) {
    constexpr Color yellow = {254, 205, 33, 255};
    constexpr Color dragonBlue = {30, 89, 153, 255};
    constexpr Color pearlRed = {205, 39, 43, 255};

    DrawRectangleRec(flag, {242, 240, 229, 255});
    DrawTriangle({flag.x, flag.y},
                 {flag.x, flag.y + flag.height},
                 {flag.x + flag.width, flag.y + flag.height * 0.5f},
                 yellow);

    const std::array<Vector2, 6> body = {{
        {flag.x + flag.width * 0.22f, flag.y + flag.height * 0.56f},
        {flag.x + flag.width * 0.31f, flag.y + flag.height * 0.36f},
        {flag.x + flag.width * 0.43f, flag.y + flag.height * 0.39f},
        {flag.x + flag.width * 0.48f, flag.y + flag.height * 0.58f},
        {flag.x + flag.width * 0.37f, flag.y + flag.height * 0.68f},
        {flag.x + flag.width * 0.28f, flag.y + flag.height * 0.58f},
    }};
    const float bodyWidth = std::max(1.0f, flag.height * 0.075f);
    for (std::size_t index = 1; index < body.size(); ++index)
        DrawLineEx(body[index - 1], body[index], bodyWidth, dragonBlue);
    const Vector2 head = {
        flag.x + flag.width * 0.55f,
        flag.y + flag.height * 0.39f};
    DrawLineEx(body[2], head, bodyWidth, dragonBlue);
    DrawCircleV(head, std::max(1.5f, flag.height * 0.075f), dragonBlue);
    DrawLineEx(
        head,
        {head.x + flag.width * 0.08f, head.y - flag.height * 0.08f},
        std::max(1.0f, flag.height * 0.035f), dragonBlue);
    DrawLineEx(
        body[1],
        {body[1].x - flag.width * 0.05f, body[1].y - flag.height * 0.12f},
        std::max(1.0f, flag.height * 0.035f), dragonBlue);
    DrawLineEx(
        body[4],
        {body[4].x + flag.width * 0.05f, body[4].y + flag.height * 0.12f},
        std::max(1.0f, flag.height * 0.035f), dragonBlue);
    DrawCircleV(
        {flag.x + flag.width * 0.70f, flag.y + flag.height * 0.28f},
        std::max(1.5f, flag.height * 0.065f), pearlRed);
}

void DrawBritishIndiaFlag(Rectangle flag) {
    constexpr Color ensignRed = {200, 16, 47, 255};
    constexpr Color badgeGold = {253, 213, 2, 255};
    constexpr Color badgeBlue = {31, 82, 145, 255};

    DrawRectangleRec(flag, ensignRed);
    DrawUnionJack({
        flag.x, flag.y, flag.width * 0.5f, flag.height * 0.5f});
    const Vector2 badge = {
        flag.x + flag.width * 0.75f,
        flag.y + flag.height * 0.5f};
    DrawStar(badge, flag.height * 0.22f, flag.height * 0.16f,
             16, badgeGold);
    DrawCircleV(badge, flag.height * 0.115f, badgeBlue);
    DrawCircleV(badge, flag.height * 0.078f, RAYWHITE);
    DrawStar(badge, flag.height * 0.065f, flag.height * 0.027f,
             5, badgeBlue);
}

void DrawPrussianFlag(Rectangle flag) {
    constexpr Color eagle = {15, 17, 18, 255};
    constexpr Color gold = {226, 174, 38, 255};
    constexpr Color crownRed = {174, 35, 42, 255};

    DrawRectangleRec(flag, RAYWHITE);
    const Vector2 center = {
        flag.x + flag.width * 0.51f,
        flag.y + flag.height * 0.56f};
    DrawTriangle(
        {center.x - flag.width * 0.02f, center.y - flag.height * 0.08f},
        {center.x - flag.width * 0.31f, center.y - flag.height * 0.32f},
        {center.x - flag.width * 0.18f, center.y + flag.height * 0.20f},
        eagle);
    DrawTriangle(
        {center.x + flag.width * 0.02f, center.y - flag.height * 0.08f},
        {center.x + flag.width * 0.31f, center.y - flag.height * 0.32f},
        {center.x + flag.width * 0.18f, center.y + flag.height * 0.20f},
        eagle);
    DrawEllipse(static_cast<int>(center.x), static_cast<int>(center.y),
                flag.width * 0.085f, flag.height * 0.22f, eagle);
    const Vector2 head = {
        center.x + flag.width * 0.06f,
        center.y - flag.height * 0.19f};
    DrawCircleV(head, flag.height * 0.07f, eagle);
    DrawTriangle(
        {head.x + flag.width * 0.04f, head.y - flag.height * 0.02f},
        {head.x + flag.width * 0.13f, head.y + flag.height * 0.01f},
        {head.x + flag.width * 0.04f, head.y + flag.height * 0.05f},
        gold);
    DrawLineEx(
        {center.x - flag.width * 0.04f, center.y + flag.height * 0.18f},
        {center.x - flag.width * 0.12f, center.y + flag.height * 0.31f},
        std::max(1.0f, flag.height * 0.035f), eagle);
    DrawLineEx(
        {center.x + flag.width * 0.04f, center.y + flag.height * 0.18f},
        {center.x + flag.width * 0.12f, center.y + flag.height * 0.31f},
        std::max(1.0f, flag.height * 0.035f), eagle);

    const float crownY = flag.y + flag.height * 0.17f;
    DrawRectangleRec({
        center.x - flag.width * 0.075f, crownY,
        flag.width * 0.15f, flag.height * 0.055f}, gold);
    DrawTriangle(
        {center.x - flag.width * 0.075f, crownY},
        {center.x - flag.width * 0.04f, crownY - flag.height * 0.10f},
        {center.x, crownY}, gold);
    DrawTriangle(
        {center.x, crownY},
        {center.x + flag.width * 0.04f, crownY - flag.height * 0.12f},
        {center.x + flag.width * 0.075f, crownY}, gold);
    DrawCircleV(
        {center.x, crownY - flag.height * 0.02f},
        std::max(1.0f, flag.height * 0.025f), crownRed);
}

void DrawRussianImperialFlag(Rectangle flag) {
    DrawHorizontalBands(flag, std::array<Color, 3>{{
        {10, 10, 10, 255}, {255, 205, 1, 255}, RAYWHITE}});
}

void DrawHabsburgFlag(Rectangle flag) {
    DrawHorizontalBands(flag, std::array<Color, 2>{{
        {10, 10, 10, 255}, {255, 204, 0, 255}}});
}

void DrawRhineConfederationFlag(Rectangle flag) {
    DrawHorizontalBands(flag, std::array<Color, 3>{{
        {0, 153, 0, 255}, RAYWHITE, {0, 0, 153, 255}}});
}

void DrawNordicCross(Rectangle flag, Color background,
                     Color cross, Color border) {
    DrawRectangleRec(flag, background);
    DrawRectangleRec({flag.x + flag.width * 0.34f, flag.y,
                      flag.width * 0.18f, flag.height}, border);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.36f,
                      flag.width, flag.height * 0.28f}, border);
    DrawRectangleRec({flag.x + flag.width * 0.385f, flag.y,
                      flag.width * 0.09f, flag.height}, cross);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.455f,
                      flag.width, flag.height * 0.09f}, cross);
}

void DrawMapleLeaf(Rectangle flag, Color color) {
    const Vector2 center = {flag.x + flag.width * 0.5f,
                            flag.y + flag.height * 0.5f};
    DrawStar(center, flag.height * 0.31f, flag.height * 0.12f,
             11, color, -90.0f);
    DrawRectangleRec({center.x - flag.width * 0.035f,
                      center.y + flag.height * 0.16f,
                      flag.width * 0.07f, flag.height * 0.22f}, color);
}

void DrawSouthAfricaFlag(Rectangle flag) {
    const Color green = {0, 119, 73, 255};
    const Color red = {222, 56, 49, 255};
    const Color blue = {0, 35, 149, 255};
    const Color gold = {255, 184, 0, 255};
    DrawRectangleRec(flag, green);
    DrawRectangleRec({flag.x, flag.y, flag.width, flag.height * 0.38f}, red);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.62f,
                      flag.width, flag.height * 0.38f}, blue);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.34f,
                      flag.width, flag.height * 0.32f}, RAYWHITE);
    DrawRectangleRec({flag.x, flag.y + flag.height * 0.43f,
                      flag.width, flag.height * 0.14f}, green);
    const Vector2 left = {flag.x, flag.y + flag.height * 0.5f};
    DrawTriangle(left,
                 {flag.x + flag.width * 0.46f, flag.y},
                 {flag.x + flag.width * 0.46f, flag.y + flag.height},
                 gold);
    DrawTriangle(left,
                 {flag.x + flag.width * 0.36f, flag.y + flag.height * 0.5f},
                 {flag.x + flag.width * 0.46f, flag.y + flag.height * 0.5f},
                 {0, 0, 0, 255});
}

void DrawFlag(const CountrySnapshot& country, Rectangle flag) {
    const std::string& tag = CountryCode(country);
    if (tag == "CHI") {
        DrawQingFlag(flag);
    } else if (tag == "JAP") {
        DrawRectangleRec(flag, RAYWHITE);
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.27f, {188, 48, 58, 255});
    } else if (tag == "RUS") {
        DrawRussianImperialFlag(flag);
    } else if (tag == "EIN") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {174, 38, 45, 255}, RAYWHITE, {31, 63, 120, 255}}});
    } else if (tag == "IDC") {
        DrawRectangleRec(flag, {245, 205, 66, 255});
        DrawRectangleRec({flag.x + flag.width * 0.44f, flag.y,
                          flag.width * 0.12f, flag.height}, {191, 41, 52, 255});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.18f, {191, 41, 52, 255});
    } else if (tag == "IND") {
        DrawBritishIndiaFlag(flag);
    } else if (tag == "TUR") {
        DrawRectangleRec(flag, {222, 41, 45, 255});
        DrawCrescent(flag, {flag.x + flag.width * 0.43f,
                            flag.y + flag.height * 0.5f},
                     flag.height * 0.25f, RAYWHITE, {222, 41, 45, 255},
                     flag.width * 0.08f);
        DrawStar({flag.x + flag.width * 0.67f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.11f, flag.height * 0.045f, 5, RAYWHITE);
    } else if (tag == "PER") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {35, 112, 66, 255}, RAYWHITE, {208, 47, 54, 255}}});
        DrawRectangleRec({flag.x + flag.width * 0.47f,
                          flag.y + flag.height * 0.38f,
                          flag.width * 0.06f, flag.height * 0.24f},
                         {211, 164, 44, 255});
    } else if (tag == "PRU") {
        DrawPrussianFlag(flag);
    } else if (tag == "WGS") {
        DrawRhineConfederationFlag(flag);
    } else if (tag == "BAV") {
        DrawRectangleRec(flag, RAYWHITE);
        const int columns = 6;
        const int rows = 4;
        for (int row = 0; row < rows; ++row) {
            for (int column = 0; column < columns; ++column) {
                if ((row + column) % 2 != 0) continue;
                DrawRectangleRec({
                    flag.x + flag.width * static_cast<float>(column) / columns,
                    flag.y + flag.height * static_cast<float>(row) / rows,
                    flag.width / columns + 1.0f,
                    flag.height / rows + 1.0f
                }, {37, 91, 169, 255});
            }
        }
    } else if (tag == "AUS") {
        DrawHabsburgFlag(flag);
    } else if (tag == "FRA") {
        DrawVerticalBands(flag, std::array<Color, 3>{{
            {35, 79, 153, 255}, RAYWHITE, {208, 53, 63, 255}}});
    } else if (tag == "GBR") {
        DrawUnionJack(flag);
    } else if (tag == "LCO") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {174, 38, 45, 255}, RAYWHITE, {31, 63, 120, 255}}});
    } else if (tag == "SWI") {
        DrawRectangleRec(flag, {214, 39, 45, 255});
        DrawRectangleRec({flag.x + flag.width * 0.42f, flag.y + flag.height * 0.20f,
                          flag.width * 0.16f, flag.height * 0.60f}, RAYWHITE);
        DrawRectangleRec({flag.x + flag.width * 0.29f, flag.y + flag.height * 0.39f,
                          flag.width * 0.42f, flag.height * 0.22f}, RAYWHITE);
    } else if (tag == "IBE") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {208, 53, 63, 255}, {239, 194, 55, 255}, {208, 53, 63, 255}}});
        DrawCircleV({flag.x + flag.width * 0.30f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.11f, {122, 72, 44, 255});
    } else if (tag == "ITA") {
        DrawVerticalBands(flag, std::array<Color, 3>{{
            {39, 123, 75, 255}, RAYWHITE, {208, 53, 63, 255}}});
    } else if (tag == "NOR") {
        DrawNordicCross(flag, {207, 45, 52, 255},
                        {28, 83, 143, 255}, RAYWHITE);
    } else if (tag == "SWE") {
        DrawNordicCross(flag, {35, 91, 149, 255},
                        {244, 198, 49, 255}, {35, 91, 149, 255});
    } else if (tag == "FIN") {
        DrawNordicCross(flag, RAYWHITE, {37, 89, 145, 255}, RAYWHITE);
    } else if (tag == "EGY") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {207, 45, 52, 255}, RAYWHITE, {20, 20, 20, 255}}});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.12f, {211, 164, 44, 255});
    } else if (tag == "TRP") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {207, 45, 52, 255}, {20, 20, 20, 255}, {35, 123, 72, 255}}});
    } else if (tag == "ALG") {
        DrawVerticalBands(flag, std::array<Color, 2>{{
            {35, 123, 72, 255}, RAYWHITE}});
        DrawCrescent(flag, {flag.x + flag.width * 0.52f,
                            flag.y + flag.height * 0.5f},
                     flag.height * 0.24f, {207, 45, 52, 255}, RAYWHITE,
                     flag.width * 0.06f);
        DrawStar({flag.x + flag.width * 0.64f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.10f, flag.height * 0.04f, 5,
                 {207, 45, 52, 255});
    } else if (tag == "MOR") {
        DrawRectangleRec(flag, {195, 47, 53, 255});
        DrawStar({flag.x + flag.width * 0.5f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.25f, flag.height * 0.08f, 5,
                 {35, 123, 72, 255});
    } else if (tag == "WAF") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {35, 123, 72, 255}, {239, 194, 55, 255},
            {207, 45, 52, 255}}});
        DrawStar({flag.x + flag.width * 0.5f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.12f, flag.height * 0.05f, 5,
                 {20, 20, 20, 255});
    } else if (tag == "CAF") {
        DrawHorizontalBands(flag, std::array<Color, 4>{{
            {37, 89, 145, 255}, RAYWHITE,
            {35, 123, 72, 255}, {239, 194, 55, 255}}});
        DrawRectangleRec({flag.x + flag.width * 0.46f, flag.y,
                          flag.width * 0.08f, flag.height}, {207, 45, 52, 255});
    } else if (tag == "SAF") {
        DrawSouthAfricaFlag(flag);
    } else if (tag == "MAD") {
        DrawRectangleRec(flag, {35, 123, 72, 255});
        DrawRectangleRec({flag.x, flag.y, flag.width * 0.33f, flag.height},
                         RAYWHITE);
        DrawRectangleRec({flag.x + flag.width * 0.33f, flag.y,
                          flag.width * 0.67f, flag.height * 0.5f},
                         {207, 45, 52, 255});
    } else if (tag == "CAN") {
        DrawVerticalBands(flag, std::array<Color, 3>{{
            {207, 45, 52, 255}, RAYWHITE, {207, 45, 52, 255}}});
        DrawMapleLeaf(flag, {207, 45, 52, 255});
    } else if (tag == "USA") {
        for (int stripe = 0; stripe < 13; ++stripe) {
            DrawRectangleRec({
                flag.x, flag.y + flag.height * stripe / 13.0f,
                flag.width, flag.height / 13.0f + 1.0f
            }, stripe % 2 == 0
                ? Color{188, 49, 58, 255} : RAYWHITE);
        }
        DrawRectangleRec({flag.x, flag.y, flag.width * 0.42f,
                          flag.height * 0.54f}, {38, 69, 135, 255});
        for (int row = 0; row < 3; ++row)
            for (int column = 0; column < 5; ++column)
                DrawStar({flag.x + flag.width * (0.08f + column * 0.075f),
                          flag.y + flag.height * (0.10f + row * 0.16f)},
                         flag.height * 0.025f, flag.height * 0.010f,
                         5, RAYWHITE);
    } else if (tag == "MEX") {
        DrawVerticalBands(flag, std::array<Color, 3>{{
            {35, 123, 72, 255}, RAYWHITE, {207, 45, 52, 255}}});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.16f, {148, 91, 47, 255});
        DrawStar({flag.x + flag.width * 0.5f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.09f, flag.height * 0.04f, 5,
                 {35, 123, 72, 255});
    } else if (tag == "CUB") {
        for (int stripe = 0; stripe < 5; ++stripe) {
            DrawRectangleRec({
                flag.x, flag.y + flag.height * stripe / 5.0f,
                flag.width, flag.height / 5.0f + 1.0f
            }, stripe % 2 == 0
                ? Color{37, 89, 145, 255} : RAYWHITE);
        }
        DrawTriangle({flag.x, flag.y},
                     {flag.x, flag.y + flag.height},
                     {flag.x + flag.width * 0.48f, flag.y + flag.height * 0.5f},
                     {207, 45, 52, 255});
        DrawStar({flag.x + flag.width * 0.16f,
                  flag.y + flag.height * 0.5f},
                 flag.height * 0.11f, flag.height * 0.045f, 5, RAYWHITE);
    } else if (tag == "BRA") {
        DrawRectangleRec(flag, {35, 123, 72, 255});
        DrawPoly({flag.x + flag.width * 0.5f,
                  flag.y + flag.height * 0.5f}, 4,
                 flag.height * 0.34f, 0.0f, {239, 194, 55, 255});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.18f, {31, 78, 148, 255});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.13f, {228, 237, 225, 255});
    } else if (tag == "ARG") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {117, 191, 221, 255}, RAYWHITE, {117, 191, 221, 255}}});
        DrawCircleV({flag.x + flag.width * 0.5f,
                     flag.y + flag.height * 0.5f},
                    flag.height * 0.11f, {239, 194, 55, 255});
        for (int ray = 0; ray < 8; ++ray) {
            const float angle = ray * 3.14159265f / 4.0f;
            DrawLineEx({flag.x + flag.width * 0.5f +
                            std::cos(angle) * flag.height * 0.12f,
                        flag.y + flag.height * 0.5f +
                            std::sin(angle) * flag.height * 0.12f},
                       {flag.x + flag.width * 0.5f +
                            std::cos(angle) * flag.height * 0.19f,
                        flag.y + flag.height * 0.5f +
                            std::sin(angle) * flag.height * 0.19f},
                       1.0f, {239, 194, 55, 255});
        }
    } else if (tag == "CHL") {
        DrawRectangleRec(flag, RAYWHITE);
        DrawRectangleRec({flag.x, flag.y + flag.height * 0.5f,
                          flag.width, flag.height * 0.5f}, {207, 45, 52, 255});
        DrawRectangleRec({flag.x, flag.y, flag.width * 0.36f,
                          flag.height * 0.5f}, {37, 89, 145, 255});
        DrawStar({flag.x + flag.width * 0.18f,
                  flag.y + flag.height * 0.25f},
                 flag.height * 0.11f, flag.height * 0.045f, 5, RAYWHITE);
    } else if (tag == "BOL") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {207, 45, 52, 255}, {239, 194, 55, 255},
            {35, 123, 72, 255}}});
    } else if (tag == "PEU") {
        DrawVerticalBands(flag, std::array<Color, 3>{{
            {207, 45, 52, 255}, RAYWHITE, {207, 45, 52, 255}}});
    } else if (tag == "GRA") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {239, 194, 55, 255}, {37, 89, 145, 255},
            {207, 45, 52, 255}}});
    } else if (tag == "VEN") {
        DrawHorizontalBands(flag, std::array<Color, 3>{{
            {239, 194, 55, 255}, {37, 89, 145, 255},
            {207, 45, 52, 255}}});
        for (int star = 0; star < 8; ++star) {
            const float angle = 3.14159265f + star * 3.14159265f / 7.0f;
            DrawStar({flag.x + flag.width * 0.5f +
                           std::cos(angle) * flag.width * 0.17f,
                       flag.y + flag.height * 0.52f +
                           std::sin(angle) * flag.height * 0.17f},
                     flag.height * 0.035f, flag.height * 0.014f, 5,
                     RAYWHITE);
        }
    } else if (tag == "GUI") {
        DrawRectangleRec(flag, {35, 123, 72, 255});
        DrawTriangle({flag.x, flag.y},
                     {flag.x + flag.width * 0.92f, flag.y + flag.height * 0.5f},
                     {flag.x, flag.y + flag.height}, {239, 194, 55, 255});
        DrawTriangle({flag.x, flag.y + flag.height * 0.17f},
                     {flag.x + flag.width * 0.70f, flag.y + flag.height * 0.5f},
                     {flag.x, flag.y + flag.height * 0.83f}, {207, 45, 52, 255});
    } else if (tag == "AST") {
        DrawRectangleRec(flag, {25, 58, 128, 255});
        DrawUnionJack({flag.x, flag.y, flag.width * 0.52f, flag.height * 0.55f});
        DrawStar({flag.x + flag.width * 0.76f,
                  flag.y + flag.height * 0.68f},
                 flag.height * 0.12f, flag.height * 0.045f, 7, RAYWHITE);
        DrawStar({flag.x + flag.width * 0.64f,
                  flag.y + flag.height * 0.82f},
                 flag.height * 0.055f, flag.height * 0.022f, 5, RAYWHITE);
        DrawStar({flag.x + flag.width * 0.82f,
                  flag.y + flag.height * 0.82f},
                 flag.height * 0.055f, flag.height * 0.022f, 5, RAYWHITE);
    } else {
        DrawRectangleRec(flag, TagColor(tag, 0));
        DrawRectangleRec({flag.x, flag.y + flag.height * 0.5f,
                          flag.width, flag.height * 0.5f},
                         TagColor(tag, 17));
    }
    DrawRectangleLinesEx(flag, 1.0f, {25, 35, 35, 255});
}

}  // namespace ui_country
