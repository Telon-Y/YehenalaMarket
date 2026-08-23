#include "ui_internal.h"

#include "ui_country_internal.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

using namespace ui_country;

namespace {
constexpr Color kPanelBackground{244, 247, 244, 252};
constexpr Color kHeader{35, 49, 50, 255};
constexpr Color kAccent{218, 178, 80, 255};
constexpr Color kLine{117, 133, 126, 255};

int ClampScroll(const CountryProvinceListLayout& list, int value) {
    return std::clamp(value, 0, list.maxOffset);
}

Rectangle BackRect() { return CurrentUILayout().countryBackButton; }

const char* BuildingLabel(int type) {
    return type >= 0 && type < TYPE_COUNT ? buildingTypeNames[type].c_str()
                                           : "Building";
}

const char* ProjectStatus(int status) {
    switch (static_cast<ConstructionProjectStatus>(status)) {
    case ConstructionProjectStatus::Queued: return "Queued";
    case ConstructionProjectStatus::Active: return "Active";
    case ConstructionProjectStatus::Completed: return "Done";
    case ConstructionProjectStatus::Cancelled: return "Cancelled";
    case ConstructionProjectStatus::Blocked: return "Blocked";
    }
    return "Unknown";
}

Color ProjectStatusColor(int status) {
    switch (static_cast<ConstructionProjectStatus>(status)) {
    case ConstructionProjectStatus::Completed: return {51, 132, 84, 255};
    case ConstructionProjectStatus::Cancelled:
    case ConstructionProjectStatus::Blocked: return {157, 71, 64, 255};
    case ConstructionProjectStatus::Active: return {50, 111, 151, 255};
    default: return {170, 105, 42, 255};
    }
}

bool IsNationalBuildable(const World& world, int provinceId, int type) {
    (void)world;
    (void)provinceId;
    if (type < 0 || type >= TYPE_COUNT) return false;
    return type != BANK && type != FINANCE &&
           type != INDUSTRIAL_BANK && type != SAVINGS_BANK;
}

int NextNationalBuildable(const World& world, int provinceId, int current) {
    for (int offset = 1; offset <= TYPE_COUNT; ++offset) {
        const int candidate = (current + offset + TYPE_COUNT) % TYPE_COUNT;
        if (IsNationalBuildable(world, provinceId, candidate)) return candidate;
    }
    return -1;
}

bool IsProjectCancellable(int status) {
    return status == static_cast<int>(ConstructionProjectStatus::Queued) ||
           status == static_cast<int>(ConstructionProjectStatus::Active);
}

void DrawHeader(const CountrySnapshot& country, const UILayout& layout, Font font) {
    DrawRectangleRec(layout.countryPanel, kPanelBackground);
    DrawRectangleRec(layout.countryHeader, kHeader);
    DrawFlag(country, layout.countryFlag);
    DrawTextEx(font, country.name.c_str(),
               {layout.countryFlag.x + layout.countryFlag.width + 10.0f, 14.0f},
               17, 0, RAYWHITE);
    const std::string& countryCode = CountryCode(country);
    DrawTextEx(font, countryCode.empty() ? "---" : countryCode.c_str(),
               {layout.countryFlag.x + layout.countryFlag.width + 10.0f, 37.0f},
               kUiBodyFontSize, 0, {185, 204, 197, 255});
    const Rectangle back = CurrentUILayout().countryBackButton;
    DrawRectangleRec(back, {69, 91, 89, 255});
    DrawTextEx(font, "Map", {back.x + 17.0f, back.y + 8.0f}, kUiBodyFontSize, 0, RAYWHITE);

    const float split = layout.countryHeader.x + layout.countryHeader.width * 0.51f;
    DrawTextEx(font, TextFormat("Pop %.0f", country.population),
               {layout.countryHeader.x + 10.0f, 62.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, TextFormat("GDP %.0f", country.gdp.toDouble()),
               {split, 62.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, TextFormat("Treasury %.0f", country.treasury.toDouble()),
               {layout.countryHeader.x + 10.0f, 82.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, TextFormat("Reserved %.0f",
                                country.reservedConstructionBudget.toDouble()),
               {split, 82.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, TextFormat("Available %.0f", country.availableTreasury.toDouble()),
               {layout.countryHeader.x + 10.0f, 104.0f}, kUiBodyFontSize, 0,
               {164, 225, 190, 255});
    DrawTextEx(font, TextFormat("Cycle %d", country.cycle),
               {split, 104.0f}, kUiBodyFontSize, 0, {185, 204, 197, 255});
}

void DrawTabs(const UIState* state, const UILayout& layout, Font font) {
    const char* labels[] = {"Overview", "Construction", "Transport"};
    for (int index = 0; index < 3; ++index) {
        const Rectangle tab = layout.countryPageTabs[static_cast<std::size_t>(index)];
        const bool active = state->countryTab == index;
        DrawRectangleRec(tab, active ? kAccent : Color{61, 78, 78, 255});
        DrawTextEx(font, labels[index], {tab.x + 9.0f, tab.y + 6.0f}, kUiTabFontSize, 0,
                   active ? Color{32, 42, 42, 255} : RAYWHITE);
    }
}

void DrawOverviewPage(const UIState* state, const CountrySnapshot& country,
                      const World& world, const UILayout& layout, Font font) {
    const Rectangle content = layout.countryPageContent;
    DrawTextEx(font, "Country overview", {content.x, content.y + 1.0f},
               16, 0, {33, 55, 53, 255});
    int selected = state->countrySelectedProvinceId;
    if (!country.provinceIds.empty() &&
        std::find(country.provinceIds.begin(), country.provinceIds.end(), selected) ==
            country.provinceIds.end())
        selected = country.provinceIds.front();
    if (selected >= 0) {
        const ProvinceSnapshot snapshot = world.getProvinceSnapshot(selected);
        DrawRectangle(content.x, content.y + 27.0f, content.width, 48.0f,
                      {225, 235, 225, 255});
        DrawTextEx(font, ("Selected: " + snapshot.name).c_str(),
                   {content.x + 8.0f, content.y + 34.0f}, kUiBodyFontSize, 0,
                   {34, 70, 59, 255});
        DrawTextEx(font, TextFormat("GDP %.0f  Pop %.0f",
                                    snapshot.gdp.toDouble(), snapshot.population),
                   {content.x + 8.0f, content.y + 54.0f}, kUiBodyFontSize, 0, DARKGRAY);
    }
    DrawTextEx(font, TextFormat("Provinces %d",
                                static_cast<int>(country.provinceIds.size())),
               {content.x, content.y + 88.0f}, kUiBodyFontSize, 0, DARKGRAY);

    const int count = static_cast<int>(country.provinceIds.size());
    const CountryProvinceListLayout list =
        ComputeCountryProvinceListLayout(layout.width, layout.height, 0, count);
    const int scroll = ClampScroll(list, state->countryProvinceScroll);
    for (int row = 0; row < list.visibleRows; ++row) {
        const int index = scroll + row;
        if (index >= count) break;
        const int provinceId = country.provinceIds[static_cast<std::size_t>(index)];
        const ProvinceSnapshot province = world.getProvinceSnapshot(provinceId);
        const float y = list.firstY + row * list.rowStride;
        DrawRectangle(content.x, y - 4.0f, content.width, list.rowHeight,
                      provinceId == state->countrySelectedProvinceId
                          ? Color{225, 235, 225, 255}
                          : Color{250, 251, 248, 255});
        DrawTextEx(font, province.name.c_str(), {content.x + 7.0f, y + 2.0f},
                   kUiBodyFontSize, 0, {35, 48, 47, 255});
        DrawTextEx(font, TextFormat("GDP %.0f", province.gdp.toDouble()),
                   {content.x + 7.0f, y + 20.0f}, kUiBodyFontSize, 0, DARKGRAY);
        DrawTextEx(font, TextFormat("Pop %.0f", province.population),
                   {content.x + content.width * 0.54f, y + 20.0f}, kUiBodyFontSize, 0, DARKGRAY);
    }
}

void DrawConstructionPage(const UIState* state, const CountrySnapshot& country,
                          const World& world, const UILayout& layout, Font font) {
    const Rectangle content = layout.countryPageContent;
    int target = state->countrySelectedProvinceId;
    if (!country.provinceIds.empty() &&
        std::find(country.provinceIds.begin(), country.provinceIds.end(), target) ==
            country.provinceIds.end())
        target = country.provinceIds.front();
    DrawTextEx(font, "National construction", {content.x, content.y + 1.0f},
               16, 0, {33, 55, 53, 255});
    DrawTextEx(font, target >= 0 ?
        ("Target: " + world.getProvinceById(target).getName()).c_str() :
        "Target: none", {content.x, content.y + 27.0f}, kUiBodyFontSize, 0, DARKGRAY);
    DrawTextEx(font, "Type", {content.x, content.y + 50.0f}, kUiBodyFontSize, 0, GRAY);
    const Rectangle typeButton = {content.x + 33.0f, content.y + 45.0f,
                                  std::max(56.0f, content.width - 117.0f), 24.0f};
    const bool canBuild = IsNationalBuildable(world, target, state->selectedBuilding);
    DrawRectangleRec(typeButton, canBuild
        ? Color{231, 237, 233, 255} : Color{231, 224, 220, 255});
    DrawTextEx(font, BuildingLabel(state->selectedBuilding),
               {typeButton.x + 6.0f, typeButton.y + 6.0f}, kUiBodyFontSize, 0,
               canBuild ? Color{39, 54, 51, 255} : Color{126, 74, 67, 255});
    const Rectangle issue = {content.x + content.width - 78.0f,
                             content.y + 45.0f, 78.0f, 24.0f};
    DrawRectangleRec(issue, canBuild
        ? Color{61, 126, 91, 255} : Color{145, 150, 146, 255});
    DrawTextEx(font, canBuild ? "Build 1" : "Unavailable",
               {issue.x + (canBuild ? 13.0f : 5.0f), issue.y + 6.0f},
               kUiBodyFontSize, 0, RAYWHITE);
    DrawTextEx(font, TextFormat("Projects %d",
                                static_cast<int>(country.constructionProjects.size())),
               {content.x, content.y + 84.0f}, kUiBodyFontSize, 0, DARKGRAY);

    const int count = static_cast<int>(country.constructionProjects.size());
    const CountryProvinceListLayout list =
        ComputeCountryProvinceListLayout(layout.width, layout.height, 1, count);
    const int scroll = ClampScroll(list, state->countryProvinceScroll);
    for (int row = 0; row < list.visibleRows; ++row) {
        const int index = scroll + row;
        if (index >= count) break;
        const ConstructionProjectSnapshot& project =
            country.constructionProjects[static_cast<std::size_t>(index)];
        const float y = list.firstY + row * list.rowStride;
        DrawRectangle(content.x, y - 4.0f, content.width, list.rowHeight,
                      {250, 251, 248, 255});
        std::string provinceName = "#" + std::to_string(project.targetProvinceId);
        if (project.targetProvinceId >= 0)
            provinceName = world.getProvinceById(project.targetProvinceId).getName();
        DrawTextEx(font, ("#" + std::to_string(project.id) + " " + provinceName).c_str(),
                   {content.x + 7.0f, y + 1.0f}, kUiBodyFontSize, 0, {35, 48, 47, 255});
        DrawTextEx(font, TextFormat("%s x%d %.0f%%",
                                    BuildingLabel(project.typeIndex), project.quantity,
                                    project.progress.toDouble() * 100.0),
                   {content.x + 7.0f, y + 20.0f}, kUiBodyFontSize, 0, DARKGRAY);
        if (IsProjectCancellable(project.status)) {
            const Rectangle cancel = {content.x + content.width - 50.0f,
                                      y - 2.0f, 48.0f, 24.0f};
            DrawRectangleRec(cancel, {157, 71, 64, 235});
            DrawTextEx(font, "Cancel", {cancel.x + 5.0f, cancel.y + 4.0f},
                       kUiBodyFontSize, 0, RAYWHITE);
        }
        DrawTextEx(font, ProjectStatus(project.status),
                   {content.x + content.width - 130.0f, y + 20.0f}, kUiBodyFontSize, 0,
                   ProjectStatusColor(project.status));
    }
    if (count == 0)
        DrawTextEx(font, "No national projects.", {content.x, list.firstY}, kUiBodyFontSize, 0, GRAY);
}

}  // namespace

void HandleCountryOverviewInput(UIState* state, World& world) {
    if (state->selectedCountryId < 0) {
        state->view = UIView::WorldMap;
        state->panelConsumesInput = false;
        return;
    }
    state->panelConsumesInput = true;
    const UILayout layout = CurrentUILayout();
    const Vector2 mouse = GetMousePosition();
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (IsKeyPressed(KEY_ESCAPE) || (left && CheckCollisionPointRec(mouse, BackRect()))) {
        NavigateBack(state);
        return;
    }
    for (int tab = 0; tab < 3; ++tab) {
        if (left && CheckCollisionPointRec(mouse,
                    layout.countryPageTabs[static_cast<std::size_t>(tab)])) {
            if (state->countryTab != tab) state->countryProvinceScroll = 0;
            state->countryTab = tab;
            state->view = tab == 2
                ? UIView::TransportLogistics
                : UIView::CountryOverview;
            return;
        }
    }

    const CountrySnapshot country = world.getCountrySnapshot(state->selectedCountryId);
    const int count = state->countryTab == 0
        ? static_cast<int>(country.provinceIds.size())
        : static_cast<int>(country.constructionProjects.size());
    const CountryProvinceListLayout list =
        ComputeCountryProvinceListLayout(layout.width, layout.height,
                                         state->countryTab, count);
    const float wheel = GetMouseWheelMove();
    if (CheckCollisionPointRec(mouse, layout.countryPageContent) && wheel != 0.0f)
        state->countryProvinceScroll += wheel > 0.0f ? -1 : 1;
    if (IsKeyPressed(KEY_UP)) --state->countryProvinceScroll;
    if (IsKeyPressed(KEY_DOWN)) ++state->countryProvinceScroll;
    state->countryProvinceScroll = ClampScroll(list, state->countryProvinceScroll);

    if (!left || !CheckCollisionPointRec(mouse, layout.countryPageContent)) return;
    if (state->countryTab == 0) {
        const int scroll = state->countryProvinceScroll;
        for (int row = 0; row < list.visibleRows; ++row) {
            const int index = scroll + row;
            if (index >= count) break;
            const Rectangle rowRect = {layout.countryPageContent.x,
                list.firstY + row * list.rowStride - 4.0f,
                layout.countryPageContent.width, list.rowHeight};
            if (!CheckCollisionPointRec(mouse, rowRect)) continue;
            const int provinceId = country.provinceIds[static_cast<std::size_t>(index)];
            if (world.switchProvinceById(provinceId)) {
                state->countrySelectedProvinceId = provinceId;
                NavigateToProvince(state, provinceId);
            }
            return;
        }
        return;
    }

    const Rectangle content = layout.countryPageContent;
    const Rectangle typeButton = {content.x + 33.0f, content.y + 45.0f,
                                  std::max(56.0f, content.width - 117.0f), 24.0f};
    if (CheckCollisionPointRec(mouse, typeButton)) {
        const int next = NextNationalBuildable(
            world, state->countrySelectedProvinceId, state->selectedBuilding);
        if (next >= 0) state->selectedBuilding = next;
        return;
    }
    const Rectangle issue = {content.x + content.width - 78.0f,
                             content.y + 45.0f, 78.0f, 24.0f};
    if (CheckCollisionPointRec(mouse, issue) &&
        IsNationalBuildable(world, state->countrySelectedProvinceId,
                            state->selectedBuilding)) {
        world.queueNationalConstruction(state->selectedCountryId,
                                        state->countrySelectedProvinceId,
                                        state->selectedBuilding, 1);
        return;
    }

    const int scroll = state->countryProvinceScroll;
    for (int row = 0; row < list.visibleRows; ++row) {
        const int index = scroll + row;
        if (index >= count) break;
        const Rectangle rowRect = {content.x,
            list.firstY + row * list.rowStride - 4.0f,
            content.width, list.rowHeight};
        if (!CheckCollisionPointRec(mouse, rowRect)) continue;
        const ConstructionProjectSnapshot& project =
            country.constructionProjects[static_cast<std::size_t>(index)];
        const Rectangle cancel = {content.x + content.width - 50.0f,
                                  list.firstY + row * list.rowStride - 2.0f,
                                  48.0f, 24.0f};
        if (IsProjectCancellable(project.status) &&
            CheckCollisionPointRec(mouse, cancel)) {
            world.cancelNationalConstructionProject(
                state->selectedCountryId, project.id);
        }
        return;
    }
}

void DrawCountryOverviewUI(const UIState* state, World& world, Font font,
                           double elapsedSeconds) {
    (void)elapsedSeconds;
    if (state->selectedCountryId < 0) return;
    const UILayout layout = CurrentUILayout();
    const CountrySnapshot country = world.getCountrySnapshot(state->selectedCountryId);
    BeginScissorMode(static_cast<int>(layout.countryPanel.x),
                     static_cast<int>(layout.countryPanel.y),
                     static_cast<int>(layout.countryPanel.width),
                     static_cast<int>(layout.countryPanel.height));
    DrawHeader(country, layout, font);
    DrawTabs(state, layout, font);
    EndScissorMode();
    const Rectangle content = layout.countryPageContent;
    BeginScissorMode(static_cast<int>(content.x), static_cast<int>(content.y),
                     static_cast<int>(content.width), static_cast<int>(content.height));
    if (state->countryTab == 1)
        DrawConstructionPage(state, country, world, layout, font);
    else
        DrawOverviewPage(state, country, world, layout, font);
    EndScissorMode();
    DrawRectangleLinesEx(layout.countryPanel, 1.0f, kLine);
}

