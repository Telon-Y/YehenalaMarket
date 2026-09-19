#pragma once

#include "raylib.h"

#include <algorithm>
#include <array>

// Text and card dimensions are shared by drawing and input so the visible
// contract cannot drift from the hit targets when the font is enlarged.
inline constexpr float kUiBodyFontSize = 16.0f;
inline constexpr float kUiHeadingFontSize = 20.0f;
inline constexpr float kUiTabFontSize = 16.0f;
inline constexpr float kProvinceBuildingCardGap = 8.0f;

inline int ProvinceBuildingColumns(float contentWidth) {
    return contentWidth < 440.0f ? 2 : 3;
}

inline float ProvinceBuildingCardSize(float contentWidth) {
    const int columns = ProvinceBuildingColumns(contentWidth);
    return std::max(1.0f,
        (contentWidth - kProvinceBuildingCardGap * (columns - 1)) /
            static_cast<float>(columns));
}

// All top-level UI geometry is derived from the current window size. Drawing
// and input both consume this value so a resize cannot move a control away
// from its hit target.
struct ProvinceBuildingGridLayout {
    int columns = 1;
    float gap = kProvinceBuildingCardGap;
    float cardSize = 1.0f;
    float listTopOffset = 52.0f;
    float listBottomOffset = 44.0f;
    int totalRows = 0;
    int visibleRows = 1;
    int maxScroll = 0;
};

inline ProvinceBuildingGridLayout ComputeProvinceBuildingGridLayout(
    float contentWidth, float contentHeight, int buildingCount) {
    ProvinceBuildingGridLayout layout;
    layout.columns = ProvinceBuildingColumns(contentWidth);
    layout.cardSize = ProvinceBuildingCardSize(contentWidth);
    layout.totalRows = std::max(0, (buildingCount + layout.columns - 1) /
                                    layout.columns);
    const float viewportHeight = std::max(
        1.0f, contentHeight - layout.listTopOffset - layout.listBottomOffset);
    layout.visibleRows = std::max(1, static_cast<int>(
        (viewportHeight + layout.gap) / (layout.cardSize + layout.gap)));
    layout.maxScroll = std::max(0, layout.totalRows - layout.visibleRows);
    return layout;
}
struct CountryProvinceListLayout {
    float firstY = 0.0f;
    float rowHeight = 0.0f;
    float rowStride = 0.0f;
    float bottom = 0.0f;
    int visibleRows = 1;
    int maxOffset = 0;
};
struct UILayout {
    int width = 0;
    int height = 0;

    Rectangle globalBar{};
    Rectangle countryHeader{};
    Rectangle countryBackButton{};
    Rectangle localBackButton{};
    Rectangle countryPanel{};
    Rectangle provincePanel{};
    Rectangle constructionPanel{};
    Rectangle constructionButton{};
    Rectangle constructionPanelHeader{};
    Rectangle constructionPanelContent{};
    Rectangle constructionPanelList{};
    float constructionPanelRowHeight = 0.0f;
    int constructionPanelVisibleRows = 1;
    Rectangle countryFlag{};
    Rectangle countryPageContent{};
    std::array<Rectangle, 3> countryPageTabs{};
    std::array<Rectangle, 3> provincePageTabs{};
    Rectangle provincePageContent{};
    Rectangle mapModeToggle{};
    Rectangle goodsButton{};
    std::array<Rectangle, 5> speedButtons{};
    float speedStatusX = 0.0f;
};

CountryProvinceListLayout ComputeCountryProvinceListLayout(int width, int height,
                                                   int tab, int count);

UILayout ComputeUILayout(int width, int height);
UILayout CurrentUILayout();
