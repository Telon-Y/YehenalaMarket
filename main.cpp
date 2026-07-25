#include <cstdio>
#include <vector>
#include <set>
#include <string>
#include "raylib.h"
#include "market_simulation.h"
#include "ui.h"

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
            if (i + extra >= s.size()) break;
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
    InitWindow(screenWidth, screenHeight, "叶赫那拉 1.1 - 市场模拟");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    std::vector<std::string> uiStrings;
    for (const auto& name : commodityNames)   uiStrings.push_back(name);
    for (const auto& name : buildingTypeNames) uiStrings.push_back(name);
    uiStrings.push_back("暂停");
    uiStrings.push_back("1倍");
    uiStrings.push_back("2倍");
    uiStrings.push_back("5倍");
    uiStrings.push_back("周期:");
    uiStrings.push_back("AI 利润阈值:");
    uiStrings.push_back("(上/下键)");
    uiStrings.push_back("商品市场");
    uiStrings.push_back("建筑");
    uiStrings.push_back("建造队列");
    uiStrings.push_back("商品:");
    uiStrings.push_back("当前价格:");
    uiStrings.push_back("(相对初始:");
    uiStrings.push_back("市场产量:");
    uiStrings.push_back("全市场消费:");          // v1.2 替换原“市场消费”
    uiStrings.push_back("短缺");                // 原料短缺标记
    uiStrings.push_back("近期价格变化 (最近200周)");
    uiStrings.push_back("总价格变化 (全部周期)");
    uiStrings.push_back("价格总表 (全部商品 · 百分比变化)");
    uiStrings.push_back("未选择任何商品");
    // 建筑页面表头 & 自给农场
    uiStrings.push_back("建筑名称");
    uiStrings.push_back("现有(在建)");
    uiStrings.push_back("雇佣率%");
    uiStrings.push_back("利润率%");
    uiStrings.push_back("现金池");
    uiStrings.push_back("自给农场");
    // 建造/拆除按钮文字
    uiStrings.push_back("建1");
    uiStrings.push_back("建5");
    uiStrings.push_back("建10");
    uiStrings.push_back("拆1");
    uiStrings.push_back("拆5");
    uiStrings.push_back("拆10");
    // 建造队列翻页文字
    uiStrings.push_back("第");
    uiStrings.push_back("页");
    uiStrings.push_back("←");
    uiStrings.push_back("→");
    uiStrings.push_back("翻页");
    // 其他可能出现的字符
    uiStrings.push_back("紧急建造部门");
    uiStrings.push_back("预计");
    uiStrings.push_back("周");

    std::vector<int> codepoints = collectCodepoints(uiStrings);
    for (int c = 32; c <= 126; ++c) codepoints.push_back(c); // ASCII
    std::set<int> uniqueCPs(codepoints.begin(), codepoints.end());
    codepoints.assign(uniqueCPs.begin(), uniqueCPs.end());
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

    MarketSimulation sim;
    UIState uiState;
    InitUIState(&uiState);

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
        DrawUI(&uiState, sim, font);
        EndDrawing();
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}