#include <iostream>
#include "raylib.h"
#include "market_simulation.h"
#include "ui.h"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    #ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    #endif

    const int screenWidth = 1280;
    const int screenHeight = 800;
    InitWindow(screenWidth, screenHeight, "叶赫那拉 1.1 - 市场模拟");
    SetTargetFPS(60);

    // 加载中文字体
    Font font = LoadFontFromSystem();

    MarketSimulation sim;
    UIState uiState;
    InitUIState(&uiState, sim);

    while (!WindowShouldClose()) {
        HandleInput(&uiState, sim);

        // 模拟步进
        if (!uiState.paused) {
            for (int i = 0; i < uiState.simulationSpeed; ++i) {
                sim.step();
                if (sim.stepCount % AI_INTERVAL == 0)
                    sim.aiBuild();
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawUI(&uiState, sim, font);
        EndDrawing();
    }

    if (font.texture.id != 0) UnloadFont(font);
    CloseWindow();
    return 0;
}