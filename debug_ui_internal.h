#pragma once

#include "debug_ui.h"

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace debug_ui {

inline constexpr Color kBackground{246, 247, 245, 255};
inline constexpr Color kSurface{255, 255, 255, 255};
inline constexpr Color kSidebar{38, 42, 44, 255};
inline constexpr Color kSidebarHover{56, 62, 64, 255};
inline constexpr Color kText{30, 35, 37, 255};
inline constexpr Color kMuted{100, 108, 110, 255};
inline constexpr Color kBorder{210, 215, 212, 255};
inline constexpr Color kGreen{35, 126, 91, 255};
inline constexpr Color kGreenSoft{222, 240, 231, 255};
inline constexpr Color kBlue{47, 104, 166, 255};
inline constexpr Color kBlueSoft{226, 236, 247, 255};
inline constexpr Color kOrange{184, 108, 36, 255};
inline constexpr Color kOrangeSoft{249, 235, 216, 255};
inline constexpr Color kRed{176, 61, 55, 255};
inline constexpr Color kRedSoft{249, 225, 222, 255};
inline constexpr Color kGold{171, 135, 36, 255};

inline constexpr float kDebugMinimumFontSize = 13.0f;
inline constexpr float kDebugCaptionFontSize = 14.0f;
inline constexpr float kDebugTableFontSize = 15.0f;
inline constexpr float kDebugBodyFontSize = 16.0f;
inline constexpr float kDebugSectionFontSize = 20.0f;
inline constexpr float kDebugPageTitleFontSize = 26.0f;

struct DebugLayout {
    DebugUIMode mode = DebugUIMode::FullScreen;
    Rectangle bounds{};
    float width = 0.0f;
    float height = 0.0f;
    float originX = 0.0f;
    float originY = 0.0f;
    float headerHeight = 58.0f;
    float marketHeight = 52.0f;
    float sidebarWidth = 190.0f;
    float contentX = 206.0f;
    float contentY = 124.0f;
    float contentWidth = 0.0f;
    float contentHeight = 0.0f;
    float goodListX = 12.0f;
    float goodListY = 306.0f;
    float goodButtonWidth = 0.0f;
    float goodButtonHeight = 24.0f;
    float goodButtonStride = 25.0f;
    int goodColumns = 1;
    bool compact = false;
    Rectangle backButton{};
    Rectangle pauseButton{};
    Rectangle stepButton{};
    std::array<Rectangle, 3> speedButtons{};
    std::array<Rectangle, 4> panelButtons{};
};

struct EmbeddedBuildingGeometry {
    float rowsY = 0.0f;
    float rowHeight = 52.0f;
    int visibleRows = 1;
    float detailY = 0.0f;
    Rectangle scrollTrack{};
};

DebugLayout MakeLayout();
DebugLayout MakeLayout(DebugUIMode mode, Rectangle bounds);
Rectangle MarketButton(const DebugLayout& layout, int index, int count);
Rectangle GoodButton(const DebugLayout& layout, int good);
Rectangle BuildingRow(const DebugLayout& layout, int type);
EmbeddedBuildingGeometry MakeEmbeddedBuildingGeometry(
    const DebugLayout& layout);
Rectangle EmbeddedBuildingRow(const DebugLayout& layout,
                              const EmbeddedBuildingGeometry& geometry,
                              int visibleRow);
Rectangle BuildingAddButton(const DebugLayout& layout);
Rectangle BuildingRemoveButton(const DebugLayout& layout);
Rectangle ConstructionDepartmentButton(const DebugLayout& layout);

void DrawTextAt(Font font, const std::string& text, float x, float y,
                float size, Color color);
void DrawFittedText(Font font, const std::string& text, Rectangle bounds,
                    float size, Color color, float padding = 0.0f);
bool DrawButton(Font font, Rectangle bounds, const std::string& label,
                bool active, Color accent = kBlue);
std::string NumberText(double value, int precision = 1);
std::string NumberText(Money value, int precision = 1);
std::string PercentText(double value);
std::string MarketCode(int marketId);
const char* OrderKindText(WarehouseOrderKind kind);
const char* OrderStatusText(WarehouseOrderStatus status);
Color OrderStatusColor(WarehouseOrderStatus status);
bool CanEditBuilding(int type);

void DrawSectionTitle(Font font, const std::string& title, float x, float y,
                      float width, const std::string& trailing = {});
void DrawMetric(Font font, Rectangle bounds, const std::string& label,
                const std::string& value, Color accent);
void DrawMoneySeries(Font font, const std::vector<Money>& values,
                     Rectangle bounds, Color color,
                     const std::string& label);
void DrawDoubleSeries(Font font, const std::vector<double>& values,
                      Rectangle bounds, Color color,
                      const std::string& label);

void DrawTradePie(Font font, Rectangle bounds, const std::string& title,
                  const std::vector<std::pair<std::string, Money>>& values);
std::vector<std::pair<std::string, Money>> OverallTradeBreakdown(
    const TransportationSnapshot& transport, bool incoming);

void RefreshAudit(DebugUIState* state, const World& world);
std::vector<int> MarketIndicesForLayout(const DebugUIState* state,
                                        const World& world,
                                        const DebugLayout& layout);
void DrawShell(DebugUIState* state, World& world, Font font,
               const DebugLayout& layout, double elapsedSeconds,
               const TransportationSnapshot& transport);

void DrawGoodsPanel(DebugUIState* state, World& world, Font font,
                    const DebugLayout& layout,
                    const TransportationSnapshot& transport);
void DrawBuildingsPanel(DebugUIState* state, World& world, Font font,
                        const DebugLayout& layout);
bool HandleConstructionPanelInput(DebugUIState* state, World& world,
                                  const DebugLayout& layout);
void DrawConstructionPanel(DebugUIState* state, World& world, Font font,
                           const DebugLayout& layout);
void DrawMacroPanel(DebugUIState* state, World& world, Font font,
                    const DebugLayout& layout,
                    const TransportationSnapshot& transport);

}  // namespace debug_ui
