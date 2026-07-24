#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include "raylib.h"
#include "market_simulation.h"
#include "ui.h"

// ───── 辅助：从一批字符串中提取所有 Unicode 码点（UTF‑8 解码）─────
static std::vector<int> collectCodepoints(const std::vector<std::string>& texts) {
    std::set<int> cps;
    for (const auto& s : texts) {
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            int cp = 0;
            int extra = 0;
            if (c < 0x80) {
                cp = c;
                extra = 0;
            } else if (c < 0xE0) {
                cp = c & 0x1F;
                extra = 1;
            } else if (c < 0xF0) {
                cp = c & 0x0F;
                extra = 2;
            } else {
                cp = c & 0x07;
                extra = 3;
            }
            if (i + extra >= s.size()) break;   // 不完整的 UTF‑8，跳过
            for (int j = 1; j <= extra; ++j) {
                cp = (cp << 6) | (static_cast<unsigned char>(s[i+j]) & 0x3F);
            }
            cps.insert(cp);
            i += extra + 1;
        }
    }
    return std::vector<int>(cps.begin(), cps.end());
}

int main() {
    printf("Starting Yehenala 1.1 Market Simulation...\n");

    const int screenWidth = 1280;
    const int screenHeight = 800;
    InitWindow(screenWidth, screenHeight, "Yehenala 1.1 - Market Sim");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    // ───── 1. 收集所有界面需要使用的中文字符 ─────
    std::vector<std::string> uiStrings;
    // 商品名、建筑名（来自 constants.h）
    for (const auto& name : commodityNames) uiStrings.push_back(name);
    for (const auto& name : buildingTypeNames) uiStrings.push_back(name);
    // 界面中会出现的各种中文提示
    uiStrings.push_back("测试: 中文 谷物 English");   // 屏幕测试文字
    uiStrings.push_back("Build");                    // 按钮
    uiStrings.push_back("Demolish");
    uiStrings.push_back("Urgent Constr Dept");
    uiStrings.push_back("Goods Market");
    uiStrings.push_back("Buildings");
    uiStrings.push_back("Const. Queue");
    uiStrings.push_back("Week:");                    // 状态栏
    uiStrings.push_back("AI Profit Threshold:");
    uiStrings.push_back("Est");
    uiStrings.push_back("weeks");
    // （如有其他中文，继续添加）

    // 提取所需码点
    std::vector<int> codepoints = collectCodepoints(uiStrings);

    // 强制加入全部基本 ASCII 可打印字符（32-126），保证英文、数字、标点绝对可用
    for (int c = 32; c <= 126; ++c) codepoints.push_back(c);

    // 去重
    std::set<int> uniqueCPs(codepoints.begin(), codepoints.end());
    codepoints.assign(uniqueCPs.begin(), uniqueCPs.end());

    printf("Collected %d unique codepoints for UI.\n", (int)codepoints.size());

    // ───── 2. 多路径尝试加载字体 ─────
    Font font = { 0 };
    const char* fontPaths[] = {
        "C:/Windows/Fonts/simhei.ttf",     // 首选系统黑体
        "C:/Windows/Fonts/msyh.ttc",       // 微软雅黑
        "MingChinese.ttf",
        "D:\\Code\\Model\\MingChinese.ttf"
    };

    for (const char* path : fontPaths) {
        if (FileExists(path)) {
            // ★ 关键：最后一个参数是数组元素个数，不再是 0
            font = LoadFontEx(path, 20, codepoints.data(), (int)codepoints.size());
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

    // ───── 3. 模拟与 UI ─────
    MarketSimulation sim;
    UIState uiState;
    InitUIState(&uiState);

    printf("Entering main loop...\n");

    while (!WindowShouldClose()) {
        HandleInput(&uiState, sim);

        if (!uiState.paused) {
            for (int i = 0; i < uiState.simulationSpeed; ++i) {
                sim.step();
                if (sim.stepCount % AI_INTERVAL == 0)
                    sim.aiBuild();
            }
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);
        // 绘制测试文字以验证字体
        DrawTextEx(font, "测试: 中文 谷物 English", { 10, 10 }, 20, 1, BLACK);
        DrawUI(&uiState, sim, font);
        EndDrawing();
    }

    printf("Window closed, exiting.\n");
    UnloadFont(font);
    CloseWindow();
    return 0;
}