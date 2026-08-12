#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include "raylib.h"
#include "world.h"
#include "ui.h"

// 收集字体所需的码点
// 不再需要手动维护 UI 字符串列表，只需自动注册：
//   - ASCII 可见字符
//   - CJK 基本区全部汉字（简体+繁体）
//   - CJK 标点符号（，。！？等）
//   - 全角 ASCII 变体（ＡＢＣ等）
//   - 常用特殊符号（← → · —— “” ‘’ 等）
static std::vector<int> collectCodepoints(const std::vector<std::string>& extraTexts = {}) {
    std::set<int> cps;

    // 1. 从显式传入的文本中收集（通常用于特殊符号）
    for (const auto& s : extraTexts) {
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            int cp = 0;
            int extra = 0;
            if (c < 0x80) { cp = c; extra = 0; }
            else if (c < 0xE0) { cp = c & 0x1F; extra = 1; }
            else if (c < 0xF0) { cp = c & 0x0F; extra = 2; }
            else { cp = c & 0x07; extra = 3; }
            if (i + extra >= s.size()) break;
            for (int j = 1; j <= extra; ++j)
                cp = (cp << 6) | (static_cast<unsigned char>(s[i+j]) & 0x3F);
            cps.insert(cp);
            i += extra + 1;
        }
    }

    // 2. ASCII 可见字符（32~126）
    for (int c = 32; c <= 126; ++c) cps.insert(c);

    // 3. CJK 基本区全部汉字（0x4E00 - 0x9FFF）
    //    这一步覆盖所有 UI 中可能出现的中文，无需手动添加
    for (int cp = 0x4E00; cp <= 0x9FFF; ++cp) cps.insert(cp);

    // 4. CJK 标点符号（0x3000 - 0x303F）
    for (int cp = 0x3000; cp <= 0x303F; ++cp) cps.insert(cp);

    // 5. 全角 ASCII 变体（0xFF00 - 0xFFEF）
    for (int cp = 0xFF00; cp <= 0xFFEF; ++cp) cps.insert(cp);

    // 6. 常用特殊符号
    cps.insert(0x00B7);   // · 中间点
    cps.insert(0x2190);   // ←
    cps.insert(0x2191);   // ↑
    cps.insert(0x2192);   // →
    cps.insert(0x2193);   // ↓
    cps.insert(0x2014);   // — 破折号
    cps.insert(0x2018);   // ‘
    cps.insert(0x2019);   // ’
    cps.insert(0x201C);   // “
    cps.insert(0x201D);   // ”

    return std::vector<int>(cps.begin(), cps.end());
}

int main() {
    printf("Starting YehenalaMarket Simulation...\n");

    const int screenWidth = 1920;
    const int screenHeight = 1080;
    InitWindow(screenWidth, screenHeight, "YehenalaMarket - 市场模拟");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    // 不再需要手动维护 uiStrings，直接生成全部需要的码点
    std::vector<int> codepoints = collectCodepoints();
    printf("Collected %d unique codepoints for UI.\n", (int)codepoints.size());

    Font font = { 0 };
    const char* fontPaths[] = {
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "MingChinese.ttf",
        "D:\\Code\\Model\\MingChinese.ttf"
    };
    for (const char* path : fontPaths) {
        if (FileExists(path)) {
            font = LoadFontEx(path, 28, codepoints.data(), (int)codepoints.size());
            if (font.texture.id != 0 && font.glyphCount > 100) {
                printf("Font loaded OK: %s (glyphs: %d)\n", path, font.glyphCount);
                break;
            } else {
                if (font.texture.id != 0) UnloadFont(font);
                printf("Font %s has only %d glyphs, trying next...\n", path, font.glyphCount);
            }
        } else {
            printf("Font not found: %s\n", path);
        }
    }
    if (font.texture.id == 0) {
        printf("No Chinese font found, using default (Chinese will be missing).\n");
        font = GetFontDefault();
    }

    World& world = World::Instance();
    UIState uiState;
    InitUIState(&uiState);

    int frameTimer = 0;
    const int MAX_STEPS_PER_FRAME = 10;

    while (!WindowShouldClose()) {
        HandleInput(&uiState, world);

        if (!uiState.paused) {
            int framesPerStep;
            switch (uiState.simulationSpeed) {
                case 2:  framesPerStep = 10; break;
                case 5:  framesPerStep = 4;  break;
                default: framesPerStep = 20; break;
            }

            frameTimer++;
            int stepsThisFrame = 0;
            while (frameTimer >= framesPerStep && stepsThisFrame < MAX_STEPS_PER_FRAME) {
                LocalMarket& market = world.getCurrentMarket();
                market.step();
                if (market.getStepCount() % AI_INTERVAL == 0)
                    market.aiBuild();
                frameTimer -= framesPerStep;
                stepsThisFrame++;
            }
            if (frameTimer >= framesPerStep)
                frameTimer = 0;
        } else {
            frameTimer = 0;
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawUI(&uiState, world, font);
        EndDrawing();
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}