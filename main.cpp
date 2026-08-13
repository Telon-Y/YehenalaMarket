// main.cpp
#include <cstdio>
#include <vector>
#include <string>
#include <chrono>
#include "raylib.h"
#include "world.h"
#include "ui.h"
#include "font_codepoints.h"

int main() {
    printf("Starting YehenalaMarket Simulation...\n");

    const int screenWidth = 1920;
    const int screenHeight = 1080;
    InitWindow(screenWidth, screenHeight, "YehenalaMarket - 市场模拟");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    std::vector<std::string> uiTexts = commodityNames;
    uiTexts.insert(uiTexts.end(), buildingTypeNames.begin(), buildingTypeNames.end());
    uiTexts.push_back(
        "叶赫那拉市场模拟暂停继续无限市场建筑建造队列其他商品当前价格"
        "相对初始生产消费期望近期变化全部历史周度数量扩建中雇佣率现金池"
        "利润率原料不足手动扩建拆除页剩余时间紧急调控总额人口金融数据"
        "货币供给储蓄银行玩家账户建造划转累计劳工工程师资本家工商银行"
        "可贷投资池贷款贷款总额到期逾期周数劳动力比例不含受抚养人口"
        "自给农场平均工资就业空闲模拟周期运行时间速度阈值政府初始私有"
        "金融区所有权开工受限贵金属中央银行高档服装加工食品棉花种植园"
        "煤矿铁矿炼钢厂工具厂住房建造部门金矿左右上下选择取消显示无");
    Font font{};
    // 跨平台字体路径：Windows 优先，Linux 备选
    const char* fontPaths[] = {
        // Windows 常见中文字体
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        // 程序工作目录（跨平台通用）
        "MingChinese.ttf",
        // Linux 常见中文字体（WenQuanYi 系列，多数发行版可通过包管理器安装）
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        // Linux 常见中文字体（Noto CJK，多数发行版默认或可通过包管理器安装）
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc",
        // Linux 常见中文字体（文鼎字体）
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/arphic/ukai.ttc",
        // Debian/Ubuntu 字体包中的其他备选
        "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        // macOS 中文字体
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc"
    };
    for (const char* path : fontPaths) {
        if (FileExists(path)) {
            std::vector<int> codepoints = collectFontCodepoints(path, uiTexts);
            printf("Collected %d mapped codepoints from %s.\n",
                   (int)codepoints.size(), path);
            font = LoadFontEx(path, 28, codepoints.data(), (int)codepoints.size());
            if (font.texture.id != 0 && font.glyphCount > 100) {
                printf("Font loaded OK: %s (glyphs: %d)\n", path, font.glyphCount);
                break;
            } else {
                if (font.texture.id != 0) {
                    UnloadFont(font);
                    font = Font{};
                }
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

    // 记录模拟开始时间
    auto startTime = std::chrono::steady_clock::now();

    int frameTimer = 0;
    const int MAX_STEPS_PER_FRAME_NORMAL = 10;    // 普通模式限制
    const int MAX_STEPS_PER_FRAME_UNLIMITED = 200; // 无限模式限制，可根据硬件调整

    while (!WindowShouldClose()) {
        HandleInput(&uiState, world);

        if (!uiState.paused) {
            int framesPerStep;
            int maxStepsThisFrame = MAX_STEPS_PER_FRAME_NORMAL;

            // 根据速度模式设定帧间隔
            switch (uiState.simulationSpeed) {
                case 2:  framesPerStep = 10; break;
                case 5:  framesPerStep = 4;  break;
                case -1: // 无限速
                    framesPerStep = 1;
                    maxStepsThisFrame = MAX_STEPS_PER_FRAME_UNLIMITED;
                    frameTimer += maxStepsThisFrame - 1;
                    break;
                default: framesPerStep = 20; break; // 1x
            }

            frameTimer++;
            int stepsThisFrame = 0;
            while (frameTimer >= framesPerStep && stepsThisFrame < maxStepsThisFrame) {
                world.stepAll();
                frameTimer -= framesPerStep;
                stepsThisFrame++;
            }
            if (frameTimer >= framesPerStep)
                frameTimer = 0;
        } else {
            frameTimer = 0;
        }

        // 计算实际运行时间
        auto now = std::chrono::steady_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();

        BeginDrawing();
        ClearBackground(RAYWHITE);
        DrawUI(&uiState, world, font, elapsedSeconds);
        EndDrawing();
    }

    UnloadFont(font);
    CloseWindow();
    return 0;
}
