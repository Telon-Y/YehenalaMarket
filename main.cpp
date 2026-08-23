// main.cpp
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <chrono>
#include "raylib.h"
#include "world.h"
#include "ui.h"
#include "debug_ui.h"
#include "font_codepoints.h"
#include "ui_text_catalog.h"
#include "world_data.h"
#include "debug_report.h"

#ifdef _WIN32
extern "C" __declspec(dllimport) int __stdcall SetProcessDPIAware(void);
#endif

namespace {

enum class LaunchView {
    WorldMap,
    ProvinceDetail,
    CountryOverview
};

struct LaunchOptions {
    std::string screenshotPath;
    std::string provinceKey;
    float mapScrollX = 0.0f;
    float mapZoom = 1.0f;
    int framesBeforeScreenshot = 3;
    LaunchView view = LaunchView::WorldMap;
    bool viewExplicit = false;
    MapMode mapMode = MapMode::Political;
    int countryTab = 0;
    int transportRouteId = -1;
    WarehouseOrderId transportOrderId = NO_WAREHOUSE_ORDER;
    int warmupSteps = 0;
    int screenWidth = 1920;
    int screenHeight = 1080;
    bool allowInput = false;
    bool constructionPanelOpen = false;
    bool debugFiveMarkets = false;
    int provincePanel = 0;
    int debugPanel = 0;
    int debugMarket = 0;
    int debugGood = 0;
    int debugBuilding = 0;
    std::string dumpDebugStatePath;
};

LaunchOptions ParseLaunchOptions(int argc, char** argv) {
    LaunchOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--screenshot" && i + 1 < argc) {
            options.screenshotPath = argv[++i];
        } else if (argument == "--province" && i + 1 < argc) {
            options.provinceKey = argv[++i];
            if (!options.viewExplicit)
                options.view = LaunchView::ProvinceDetail;
        } else if (argument == "--view" && i + 1 < argc) {
            const std::string view = argv[++i];
            options.viewExplicit = true;
            if (view == "province")
                options.view = LaunchView::ProvinceDetail;
            else if (view == "country")
                options.view = LaunchView::CountryOverview;
            else
                options.view = LaunchView::WorldMap;
        } else if (argument == "--map-mode" && i + 1 < argc) {
            const std::string mode = argv[++i];
            options.mapMode = mode == "logistics"
                ? MapMode::Logistics : MapMode::Political;
        } else if (argument == "--tab" && i + 1 < argc) {
            const std::string tab = argv[++i];
            if (tab == "overview") options.countryTab = 0;
            else if (tab == "local") options.countryTab = 0;
            else if (tab == "national") options.countryTab = 0;
            else if (tab == "transport") {
                options.countryTab = 2;
                options.view = LaunchView::CountryOverview;
                options.viewExplicit = true;
            }
            else if (tab == "construction") options.countryTab = 1;
            else options.countryTab = std::clamp(std::atoi(tab.c_str()), 0, 2);
        } else if (argument == "--route" && i + 1 < argc) {
            options.transportRouteId = std::atoi(argv[++i]);
        } else if (argument == "--order" && i + 1 < argc) {
            options.transportOrderId = static_cast<WarehouseOrderId>(
                std::strtoull(argv[++i], nullptr, 10));
        } else if (argument == "--warmup" && i + 1 < argc) {
            options.warmupSteps = std::max(0, std::atoi(argv[++i]));
        } else if (argument == "--size" && i + 1 < argc) {
            int width = 0;
            int height = 0;
            if (std::sscanf(argv[++i], "%dx%d", &width, &height) == 2 &&
                width >= 1024 && height >= 720) {
                options.screenWidth = width;
                options.screenHeight = height;
            }
        } else if (argument == "--map-scroll" && i + 1 < argc) {
            char* end = nullptr;
            const float parsed = std::strtof(argv[++i], &end);
            if (end != argv[i]) options.mapScrollX = parsed;
        } else if (argument == "--map-zoom" && i + 1 < argc) {
            char* end = nullptr;
            const float parsed = std::strtof(argv[++i], &end);
            if (end != argv[i] && std::isfinite(parsed))
                options.mapZoom = std::clamp(parsed, 0.75f, 3.0f);
        } else if (argument == "--frames" && i + 1 < argc) {
            options.framesBeforeScreenshot =
                std::max(1, std::atoi(argv[++i]));
        } else if (argument == "--allow-input") {
            options.allowInput = true;
        } else if (argument == "--construction-panel") {
            options.constructionPanelOpen = true;
        } else if (argument == "--province-panel" && i + 1 < argc) {
            const std::string panel = argv[++i];
            if (panel == "goods") options.provincePanel = 0;
            else if (panel == "buildings") options.provincePanel = 1;
            else if (panel == "construction") options.provincePanel = 2;
            else if (panel == "macro") options.provincePanel = 3;
            else options.provincePanel =
                std::clamp(std::atoi(panel.c_str()), 0, 3);
        } else if (argument == "--debug-five-markets") {
            options.debugFiveMarkets = true;
        } else if (argument == "--world-gui") {
            options.debugFiveMarkets = false;
        } else if (argument == "--debug-panel" && i + 1 < argc) {
            const std::string panel = argv[++i];
            if (panel == "goods") options.debugPanel = 0;
            else if (panel == "buildings") options.debugPanel = 1;
            else if (panel == "construction") options.debugPanel = 2;
            else if (panel == "macro") options.debugPanel = 3;
            else options.debugPanel =
                std::clamp(std::atoi(panel.c_str()), 0, 3);
        } else if (argument == "--debug-market" && i + 1 < argc) {
            options.debugMarket = std::max(0, std::atoi(argv[++i]));
        } else if (argument == "--debug-good" && i + 1 < argc) {
            options.debugGood =
                std::clamp(std::atoi(argv[++i]), 0, NUM_GOODS - 1);
        } else if (argument == "--debug-building" && i + 1 < argc) {
            options.debugBuilding =
                std::clamp(std::atoi(argv[++i]), 0, TYPE_COUNT - 1);
        } else if (argument == "--dump-debug-state" && i + 1 < argc) {
            options.dumpDebugStatePath = argv[++i];
        }
    }
    return options;
}

bool ExportLogicalScreenshot(const std::string& path) {
    Image screenshot = LoadImageFromScreen();
    if (screenshot.data == nullptr) return false;

    const int logicalWidth = std::min(screenshot.width, GetScreenWidth());
    const int logicalHeight = std::min(screenshot.height, GetScreenHeight());
    const Rectangle content = {
        0.0f,
        static_cast<float>(screenshot.height - logicalHeight),
        static_cast<float>(logicalWidth),
        static_cast<float>(logicalHeight),
    };
    ImageCrop(&screenshot, content);
    const bool exported = ExportImage(screenshot, path.c_str());
    UnloadImage(screenshot);
    return exported;
}

}  // namespace

int main(int argc, char** argv) {
    printf("Starting YehenalaMarket Simulation...\n");
    const LaunchOptions launchOptions = ParseLaunchOptions(argc, argv);
    World& world = launchOptions.debugFiveMarkets
        ? World::DebugFiveMarkets() : World::Instance();
    for (int step = 0; step < launchOptions.warmupSteps; ++step)
        world.stepAll(!launchOptions.debugFiveMarkets);
    if (!launchOptions.dumpDebugStatePath.empty()) {
        bool healthy = false;
        if (launchOptions.dumpDebugStatePath == "-") {
            healthy = WriteDebugStateReport(std::cout, world);
        } else {
            std::ofstream report(launchOptions.dumpDebugStatePath,
                                 std::ios::binary);
            if (!report) {
                std::fprintf(stderr, "Failed to open debug report: %s\n",
                             launchOptions.dumpDebugStatePath.c_str());
                return 1;
            }
            healthy = WriteDebugStateReport(report, world);
        }
        return healthy ? 0 : 2;
    }

    const int screenWidth = launchOptions.screenWidth;
    const int screenHeight = launchOptions.screenHeight;
#ifdef _WIN32
    SetProcessDPIAware();
#endif
    InitWindow(screenWidth, screenHeight,
               launchOptions.debugFiveMarkets
                   ? "YehenalaMarket - 供应链调试"
                   : "YehenalaMarket - 2.0 世界");
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);

    std::vector<std::string> uiTexts = BuildUiTextCatalog();
    uiTexts.insert(uiTexts.end(), commodityNames.begin(), commodityNames.end());
    uiTexts.insert(uiTexts.end(), buildingTypeNames.begin(), buildingTypeNames.end());
    for (const auto& item : WorldData::continents())
        uiTexts.emplace_back(item.name);
    for (const auto& item : WorldData::regions())
        uiTexts.emplace_back(item.name);
    for (const auto& item : WorldData::countries())
        uiTexts.emplace_back(item.name);
    for (const auto& item : WorldData::provinces())
        uiTexts.emplace_back(item.name);
    Font font{};
    const std::string bundledFontPath =
        std::string(GetApplicationDirectory()) + "MingChinese.ttf";
    const std::vector<std::string> fontPaths = {
        bundledFontPath,
        "MingChinese.ttf",
        "C:/Windows/Fonts/simhei.ttf",
        "C:/Windows/Fonts/msyh.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJKsc-Regular.otf",
        "/usr/share/fonts/truetype/arphic/uming.ttc",
        "/usr/share/fonts/truetype/arphic/ukai.ttc",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc"
    };

    for (const std::string& path : fontPaths) {
        if (FileExists(path.c_str())) {
            std::vector<int> codepoints =
                collectFontCodepoints(path.c_str(), uiTexts);
            const std::vector<int> missing =
                collectMissingFontCodepoints(path.c_str(), uiTexts);
            printf("Collected %d mapped codepoints from %s.\n",
                   (int)codepoints.size(), path.c_str());
            printf("Font catalog missing %d codepoints.\n",
                   (int)missing.size());
            font = LoadFontEx(path.c_str(), 36, codepoints.data(),
                               (int)codepoints.size());
            if (font.texture.id != 0 && font.glyphCount > 100) {
                SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
                printf("Font loaded OK: %s (glyphs: %d)\n",
                       path.c_str(), font.glyphCount);
                break;
            } else {
                if (font.texture.id != 0) {
                    UnloadFont(font);
                    font = Font{};
                }
                printf("Font %s has only %d glyphs, trying next...\n",
                       path.c_str(), font.glyphCount);
            }
        } else {
            printf("Font not found: %s\n", path.c_str());
        }
    }
    if (font.texture.id == 0) {
        printf("No Chinese font found, using default (Chinese will be missing).\n");
        font = GetFontDefault();
    }

    UIState uiState;
    InitUIState(&uiState);
    DebugUIState debugUIState;
    InitDebugUIState(&debugUIState);
    uiState.mapScrollX = launchOptions.mapScrollX;
    uiState.mapZoom = launchOptions.mapZoom;
    uiState.mapMode = launchOptions.mapMode;
    uiState.constructionPanelOpen = launchOptions.constructionPanelOpen;
    if (launchOptions.view != LaunchView::WorldMap) {
        const Province* province = launchOptions.provinceKey.empty()
            ? &world.getProvince(0)
            : world.findProvinceByKey(launchOptions.provinceKey);
        if (province != nullptr && world.switchProvinceById(province->getId())) {
            if (launchOptions.view == LaunchView::CountryOverview) {
                NavigateToCountry(&uiState, province->getCountryId(),
                                  province->getId());
                // 2.0 exposes the national overview and construction queue.
                // Older command-line tab names remain accepted as aliases.
                uiState.countryTab =
                    std::clamp(launchOptions.countryTab, 0, 2);
                uiState.view = uiState.countryTab == 2
                    ? UIView::TransportLogistics
                    : UIView::CountryOverview;
            } else {
                uiState.selectedCountryId = province->getCountryId();
                NavigateToProvince(&uiState, province->getId());
            }
            uiState.selectedTransportRouteId = launchOptions.transportRouteId;
            uiState.selectedTransportOrderId = launchOptions.transportOrderId;
        }
    }
    uiState.localMarketUI.currentPanel = launchOptions.provincePanel;
    if (!launchOptions.screenshotPath.empty()) uiState.paused = true;
    debugUIState.currentPanel = launchOptions.debugPanel;
    debugUIState.selectedMarket = std::clamp(
        launchOptions.debugMarket, 0,
        std::max(0, world.getMarketCount() - 1));
    debugUIState.selectedCountryId = world.getMarketCount() > 0
        ? world.getMarket(debugUIState.selectedMarket).getFiscalCountry() != nullptr
            ? world.getMarket(debugUIState.selectedMarket).getFiscalCountry()->getId()
            : -1
        : -1;
    debugUIState.selectedGood = launchOptions.debugGood;
    debugUIState.selectedBuilding = launchOptions.debugBuilding;
    if (launchOptions.debugFiveMarkets)
        world.switchMarket(debugUIState.selectedMarket);
    if (!launchOptions.screenshotPath.empty()) debugUIState.paused = true;
    if (!launchOptions.screenshotPath.empty() && !launchOptions.allowInput)
        uiState.mapInputEnabled = false;

    // Simulation start time
    auto startTime = std::chrono::steady_clock::now();

    int frameTimer = 0;
    const int MAX_STEPS_PER_FRAME_NORMAL = 10;
    const int MAX_STEPS_PER_FRAME_UNLIMITED = 1;
    int renderedFrames = 0;
    int exitCode = 0;

    while (!WindowShouldClose()) {
        if (launchOptions.debugFiveMarkets)
            HandleDebugUIInput(&debugUIState, world);
        else
            HandleInput(&uiState, world);

        const bool singleStep =
            launchOptions.debugFiveMarkets &&
            ConsumeDebugSingleStep(&debugUIState);
        const bool paused = launchOptions.debugFiveMarkets
            ? debugUIState.paused : uiState.paused;
        const int simulationSpeed = launchOptions.debugFiveMarkets
            ? debugUIState.simulationSpeed : uiState.simulationSpeed;
        if (singleStep) {
            world.stepAll();
            frameTimer = 0;
        } else if (!paused) {
            int framesPerStep;
            int maxStepsThisFrame = MAX_STEPS_PER_FRAME_NORMAL;
            // Speed mode controls the number of simulation steps per frame.
            switch (simulationSpeed) {
                case 2:  framesPerStep = 10; break;
                case 5:  framesPerStep = 4;  break;
                case -1: // unlimited
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

        // Measure actual runtime.
        auto now = std::chrono::steady_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(now - startTime).count();

        BeginDrawing();
        ClearBackground(RAYWHITE);
        if (launchOptions.debugFiveMarkets)
            DrawDebugUI(&debugUIState, world, font, elapsedSeconds);
        else
            DrawUI(&uiState, world, font, elapsedSeconds);
        EndDrawing();

        if (!launchOptions.screenshotPath.empty() &&
            ++renderedFrames >= launchOptions.framesBeforeScreenshot) {
            if (!ExportLogicalScreenshot(launchOptions.screenshotPath)) {
                exitCode = 1;
                std::fprintf(stderr, "Failed to export screenshot: %s\n",
                             launchOptions.screenshotPath.c_str());
            }
            if (launchOptions.debugFiveMarkets) {
                std::printf(
                    "Screenshot UI state: view=DebugFiveMarkets market=%d panel=%d\n",
                    debugUIState.selectedMarket,
                    debugUIState.currentPanel);
            } else {
                std::printf(
                    "Screenshot UI state: view=%s provinceId=%d scrollX=%.3f\n",
                    uiState.view == UIView::WorldMap
                        ? "WorldMap"
                        : uiState.view == UIView::CountryOverview
                              ? "CountryOverview"
                              : uiState.view == UIView::NationalMarket
                                    ? "NationalMarket"
                                    : uiState.view ==
                                              UIView::TransportLogistics
                                          ? "TransportLogistics"
                                          : "ProvinceDetail",
                    uiState.selectedProvinceId, uiState.mapScrollX);
            }
            break;
        }
    }

    UnloadFont(font);
    CloseWindow();
    return exitCode;
}
