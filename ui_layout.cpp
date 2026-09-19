#include "ui_layout.h"
#include "constants.h"

#include <algorithm>

namespace {

// Country and construction overlays stay compact. Province management needs
// a wider reading surface for tables while retaining map context on desktop.
constexpr float kOverlayPanelWidthRatio = 2.0f / 5.0f;
constexpr float kProvincePanelWidthRatio = 0.62f;
constexpr float kProvincePanelPreferredWidth = 900.0f;
constexpr float kProvincePanelMaximumWidth = 1120.0f;


}  // namespace

CountryProvinceListLayout ComputeCountryProvinceListLayout(int width, int height,
                                                           int tab, int count) {
    (void)width;
    const float windowHeight = static_cast<float>(std::max(1, height));
    CountryProvinceListLayout layout;
    // Both national pages share one scroll viewport. The tab argument is
    // retained for callers compiled against the earlier layout API.
    (void)tab;
    layout.firstY = 322.0f;
    layout.rowHeight = 50.0f;
    layout.rowStride = 56.0f;
    layout.bottom = windowHeight - 18.0f;
    layout.visibleRows = std::max(
        1, static_cast<int>((layout.bottom - layout.firstY -
                             layout.rowHeight) / layout.rowStride) + 1);
    layout.maxOffset = std::max(0, count - layout.visibleRows);
    return layout;
}

UILayout ComputeUILayout(int width, int height) {
    UILayout layout;
    layout.width = std::max(1, width);
    layout.height = std::max(1, height);

    const float w = static_cast<float>(layout.width);
    const float h = static_cast<float>(layout.height);
    const float panelWidth = std::max(1.0f, w * kOverlayPanelWidthRatio);
    const float provincePanelWidth = std::min(
        w, std::clamp(w * kProvincePanelWidthRatio,
                      kProvincePanelPreferredWidth,
                      kProvincePanelMaximumWidth));
    const float panelInset = std::min(16.0f, std::max(10.0f, panelWidth * 0.06f));
    const float contentWidth = std::max(1.0f, panelWidth - panelInset * 2.0f);

    layout.globalBar = {0.0f, 0.0f, w, 56.0f};
    layout.countryPanel = {0.0f, 0.0f, panelWidth, h};
    layout.provincePanel = {0.0f, 0.0f, provincePanelWidth, h};
    layout.constructionPanel = {w - panelWidth, 0.0f, panelWidth, h};
    layout.constructionPanelHeader = {w - panelWidth, 0.0f, panelWidth, 138.0f};
    layout.constructionPanelContent = {
        w - panelWidth + panelInset, 150.0f, contentWidth,
        std::max(1.0f, h - 162.0f)};
    layout.constructionPanelList = {
        layout.constructionPanelContent.x,
        layout.constructionPanelContent.y + 112.0f,
        layout.constructionPanelContent.width,
        std::max(1.0f, h - 278.0f)};
    layout.constructionPanelRowHeight = 74.0f;
    layout.constructionPanelVisibleRows = std::max(
        1, static_cast<int>((layout.constructionPanelList.height - 4.0f) /
                            layout.constructionPanelRowHeight));
    const float constructionButtonWidth = std::min(
        156.0f, std::max(112.0f, panelWidth - panelInset * 2.0f));
    layout.constructionButton = {
        w - panelInset - constructionButtonWidth, 8.0f,
        constructionButtonWidth, 40.0f};
    layout.countryHeader = {0.0f, 0.0f, panelWidth, 150.0f};
    layout.countryFlag = {panelInset, 14.0f, 48.0f, 32.0f};
    layout.countryBackButton = {
        std::max(panelInset, panelWidth - panelInset - 78.0f), 14.0f,
        std::min(78.0f, contentWidth), 30.0f};
    const float provinceInset = std::min(
        16.0f, std::max(10.0f, provincePanelWidth * 0.06f));
    layout.localBackButton = {
        std::max(provinceInset,
                 provincePanelWidth - provinceInset - 78.0f),
        14.0f,
        std::min(78.0f,
                 std::max(1.0f, provincePanelWidth - provinceInset * 2.0f)),
        30.0f};

    constexpr float speedGap = 6.0f;
    const float speedStart = panelWidth + 10.0f;
    const float speedEnd = layout.constructionButton.x - 10.0f;
    const float speedWidth = std::max(
        1.0f, std::min(72.0f,
            (speedEnd - speedStart - speedGap * 4.0f) / 5.0f));
    float speedX = speedStart;
    for (Rectangle& button : layout.speedButtons) {
        button = {speedX, 7.0f, speedWidth, 40.0f};
        speedX += speedWidth + speedGap;
    }
    layout.speedStatusX = layout.speedButtons.back().x +
                          layout.speedButtons.back().width + 16.0f;
    layout.mapModeToggle = {
        std::max(16.0f, w - 232.0f), std::max(120.0f, h - 52.0f),
        std::min(216.0f, std::max(170.0f, w - 32.0f)), 36.0f};
    layout.goodsButton = {18.0f, 124.0f, 54.0f, 54.0f};

    const float tabGap = 6.0f;
    const float countryTabWidth = std::max(
        1.0f, (contentWidth - tabGap * 2.0f) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        layout.countryPageTabs[static_cast<std::size_t>(i)] = {
            panelInset + i * (countryTabWidth + tabGap), 158.0f,
            countryTabWidth, 30.0f};
    }
    const float provinceTabWidth = std::max(
        1.0f, (contentWidth - tabGap * 2.0f) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        layout.provincePageTabs[static_cast<std::size_t>(i)] = {
            panelInset + i * (provinceTabWidth + tabGap), 158.0f,
            provinceTabWidth, 30.0f};
    }
    layout.countryPageContent = {
        panelInset, 202.0f, contentWidth, std::max(1.0f, h - 214.0f)};
    layout.provincePageContent = layout.countryPageContent;

    return layout;
}
UILayout CurrentUILayout() {
    return ComputeUILayout(GetScreenWidth(), GetScreenHeight());
}
