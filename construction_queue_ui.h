#pragma once

#include "raylib.h"

// Keep drawing and hit testing identical across national, country, debug,
// and embedded construction lists. Directions are geometric, so they remain
// legible with every UI font.
struct ConstructionQueueButtons {
    Vector2 up;
    Vector2 down;
    float radius = 10.0f;
};

inline ConstructionQueueButtons ConstructionQueueRowButtons(Rectangle row) {
    const float centerY = row.y + row.height * 0.5f;
    return {{row.x + row.width - 37.0f, centerY},
            {row.x + row.width - 13.0f, centerY}, 10.0f};
}

inline bool ConstructionQueueShiftHeld() {
    return IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
}

inline bool ConstructionQueueButtonHit(Vector2 mouse,
                                       const ConstructionQueueButtons& buttons,
                                       bool up) {
    return CheckCollisionPointCircle(
        mouse, up ? buttons.up : buttons.down, buttons.radius);
}

inline void DrawConstructionQueueButton(Vector2 center, float radius,
                                       bool up, bool enabled) {
    const bool hovered = enabled && CheckCollisionPointCircle(
        GetMousePosition(), center, radius);
    DrawCircleV(center, radius,
                enabled ? (hovered ? Color{72, 139, 110, 255}
                                   : Color{54, 111, 91, 255})
                        : Color{199, 205, 201, 255});
    const Color arrow = enabled ? RAYWHITE : Color{146, 153, 148, 255};
    const float direction = up ? -1.0f : 1.0f;
    const Vector2 tip{center.x, center.y + direction * 3.0f};
    DrawLineEx({center.x - 4.0f, center.y - direction * 2.0f},
               tip, 1.8f, arrow);
    DrawLineEx(tip, {center.x + 4.0f, center.y - direction * 2.0f},
               1.8f, arrow);
}

inline void DrawConstructionQueueButtons(const ConstructionQueueButtons& buttons,
                                         bool canUp, bool canDown) {
    DrawConstructionQueueButton(buttons.up, buttons.radius, true, canUp);
    DrawConstructionQueueButton(buttons.down, buttons.radius, false, canDown);
}
