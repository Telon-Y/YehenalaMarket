#include "ui_internal.h"
#include "construction_ui_text.h"
#include "construction_queue_ui.h"
#include "number_format.h"

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

// 统一的中文紧凑数字文本：大额数值一律使用 万 / 亿 / 万亿 省略表述，
// 不再输出 k、m、b 这类字母后缀，也不输出整串原始数字。
std::string ChineseNumber(double value, int precision = 1) {
    return FormatChineseNumber(value, precision);
}

int ClampScroll(const CountryProvinceListLayout& list, int value) {
    return std::clamp(value, 0, list.maxOffset);
}

Rectangle BackRect() { return CurrentUILayout().countryBackButton; }

const char* BuildingLabel(int type) {
    return type >= 0 && type < TYPE_COUNT ? buildingTypeNames[type].c_str()
                                           : "建筑";
}

Color ProjectStatusColor(int status) {
    switch (static_cast<ConstructionProjectStatus>(status)) {
    case ConstructionProjectStatus::Completed: return {51, 132, 84, 255};
    case ConstructionProjectStatus::Cancelled:
    case ConstructionProjectStatus::Invalidated: return {157, 71, 64, 255};
    case ConstructionProjectStatus::Active: return {50, 111, 151, 255};
    case ConstructionProjectStatus::Paused: return {105, 105, 105, 255};
    default: return {170, 105, 42, 255};
    }
}

int ResolveConstructionTarget(const UIState* state,
                              const CountrySnapshot& country) {
    if (std::find(country.provinceIds.begin(), country.provinceIds.end(),
                  state->countrySelectedProvinceId) !=
        country.provinceIds.end()) {
        return state->countrySelectedProvinceId;
    }
    return country.provinceIds.empty() ? -1 : country.provinceIds.front();
}

ConstructionCommandResult EvaluateConstruction(
    const World& world, int countryId, int provinceId, int type) {
    return world.evaluateNationalConstruction(
        countryId, provinceId, type, 1);
}

bool IsNationalBuildable(const World& world, int countryId,
                         int provinceId, int type) {
    return EvaluateConstruction(
               world, countryId, provinceId, type).error ==
           ConstructionCommandError::None;
}

bool IsNationalTypeSelectable(const World& world, int countryId,
                              int provinceId, int type) {
    const ConstructionCommandError error =
        EvaluateConstruction(world, countryId, provinceId, type).error;
    return error != ConstructionCommandError::InvalidType &&
           error != ConstructionCommandError::FinancialBuilding &&
           error != ConstructionCommandError::UnknownCountry &&
           error != ConstructionCommandError::UnknownProvince &&
           error != ConstructionCommandError::WrongCountry;
}

int NextNationalBuildable(const World& world, int countryId,
                          int provinceId, int current) {
    for (int offset = 1; offset <= TYPE_COUNT; ++offset) {
        const int candidate = (current + offset + TYPE_COUNT) % TYPE_COUNT;
        if (IsNationalTypeSelectable(
                world, countryId, provinceId, candidate)) {
            return candidate;
        }
    }
    return -1;
}

void RecordConstructionResult(UIState* state,
                              const ConstructionCommandResult& result) {
    state->constructionSucceeded = static_cast<bool>(result);
    state->constructionMessage = result
        ? "已加入国家建设项目 #" + std::to_string(result.projectId)
        : ConstructionCommandErrorText(result.error);
}

bool IsProjectCancellable(int status) {
    return ConstructionProjectIsLive(
        static_cast<ConstructionProjectStatus>(status));
}

ConstructionQueueButtons CountryQueueButtons(Rectangle row) {
    // Cancel occupies the top-right corner. Keep ordering beside it, above
    // the state label and progress bar.
    return ConstructionQueueRowButtons(
        {row.x, row.y + 2.0f, row.width - 56.0f, 24.0f});
}

void DrawProjectLine(Font font, const std::string& text, Rectangle bounds,
                     Color color) {
    const Vector2 measured = MeasureTextEx(font, text.c_str(), kUiBodyFontSize, 0);
    const float size = measured.x > bounds.width && measured.x > 0.0f
        ? std::max(9.0f, kUiBodyFontSize * bounds.width / measured.x)
        : kUiBodyFontSize;
    DrawTextEx(font, text.c_str(), {bounds.x, bounds.y}, size, 0, color);
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
    DrawTextEx(font, "地图", {back.x + 17.0f, back.y + 8.0f}, kUiBodyFontSize, 0, RAYWHITE);

    const float split = layout.countryHeader.x + layout.countryHeader.width * 0.51f;
    const std::string populationLine =
        "人口 " + ChineseNumber(country.population);
    const std::string gdpLine = "国内生产总值 " + ChineseNumber(country.gdp.toDouble());
    const std::string treasuryLine =
        "国库 " + ChineseNumber(country.treasury.toDouble());
    const std::string reservedLine =
        "已预留 " + ChineseNumber(country.reservedConstructionBudget.toDouble());
    const std::string availableLine =
        "可用 " + ChineseNumber(country.availableTreasury.toDouble());
    DrawTextEx(font, populationLine.c_str(),
               {layout.countryHeader.x + 10.0f, 62.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, gdpLine.c_str(),
               {split, 62.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, treasuryLine.c_str(),
               {layout.countryHeader.x + 10.0f, 82.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, reservedLine.c_str(),
               {split, 82.0f}, kUiBodyFontSize, 0, {226, 233, 229, 255});
    DrawTextEx(font, availableLine.c_str(),
               {layout.countryHeader.x + 10.0f, 104.0f}, kUiBodyFontSize, 0,
               {164, 225, 190, 255});
    DrawTextEx(font, TextFormat("周期 %d", country.cycle),
               {split, 104.0f}, kUiBodyFontSize, 0, {185, 204, 197, 255});
}

void DrawTabs(const UIState* state, const UILayout& layout, Font font) {
    const char* labels[] = {"总览", "建设", "运输"};
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
    DrawTextEx(font, "国家总览", {content.x, content.y + 1.0f},
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
        DrawTextEx(font, ("已选择：" + snapshot.name).c_str(),
                   {content.x + 8.0f, content.y + 34.0f}, kUiBodyFontSize, 0,
                   {34, 70, 59, 255});
        const std::string provinceLine =
            "国内生产总值 " + ChineseNumber(snapshot.gdp.toDouble()) +
            "  人口 " + ChineseNumber(snapshot.population);
        DrawTextEx(font, provinceLine.c_str(),
                   {content.x + 8.0f, content.y + 54.0f}, kUiBodyFontSize, 0, DARKGRAY);
    }
    DrawTextEx(font, TextFormat("省份数：%d",
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
        const std::string provinceGdp = "国内生产总值 " + ChineseNumber(province.gdp.toDouble());
        DrawTextEx(font, provinceGdp.c_str(),
                   {content.x + 7.0f, y + 20.0f}, kUiBodyFontSize, 0, DARKGRAY);
        const std::string provincePopulation = "人口 " + ChineseNumber(province.population);
        DrawTextEx(font, provincePopulation.c_str(),
                   {content.x + content.width * 0.54f, y + 20.0f}, kUiBodyFontSize, 0, DARKGRAY);
    }
}

void DrawConstructionPage(const UIState* state, const CountrySnapshot& country,
                          const World& world, const UILayout& layout, Font font) {
    const Rectangle content = layout.countryPageContent;
    const int target = ResolveConstructionTarget(state, country);
    DrawTextEx(font, "国家建设", {content.x, content.y + 1.0f},
               16, 0, {33, 55, 53, 255});
    DrawTextEx(font, target >= 0 ?
        ("目标：" + world.getProvinceById(target).getName()).c_str() :
        "目标：无", {content.x, content.y + 27.0f}, kUiBodyFontSize, 0, DARKGRAY);
    DrawTextEx(font, "类型", {content.x, content.y + 50.0f}, kUiBodyFontSize, 0, GRAY);
    const Rectangle typeButton = {content.x + 33.0f, content.y + 45.0f,
                                  std::max(56.0f, content.width - 117.0f), 24.0f};
    const bool canBuild = IsNationalBuildable(
        world, country.countryId, target, state->selectedBuilding);
    DrawRectangleRec(typeButton, canBuild
        ? Color{231, 237, 233, 255} : Color{231, 224, 220, 255});
    DrawTextEx(font, BuildingLabel(state->selectedBuilding),
               {typeButton.x + 6.0f, typeButton.y + 6.0f}, kUiBodyFontSize, 0,
               canBuild ? Color{39, 54, 51, 255} : Color{126, 74, 67, 255});
    const Rectangle issue = {content.x + content.width - 78.0f,
                             content.y + 45.0f, 78.0f, 24.0f};
    DrawRectangleRec(issue, canBuild
        ? Color{61, 126, 91, 255} : Color{145, 150, 146, 255});
    DrawTextEx(font, canBuild ? "建设 1 座" : "不可用",
               {issue.x + (canBuild ? 13.0f : 5.0f), issue.y + 6.0f},
               kUiBodyFontSize, 0, RAYWHITE);
    if (!state->constructionMessage.empty()) {
        DrawTextEx(
            font, state->constructionMessage.c_str(),
            {content.x, content.y + 74.0f}, kUiBodyFontSize, 0,
            state->constructionSucceeded
                ? Color{51, 132, 84, 255}
                : Color{157, 71, 64, 255});
    } else {
        DrawProjectLine(font, "队列箭头：移动项目；Shift：首项/末项",
                        {content.x, content.y + 74.0f, content.width, 20.0f},
                        GRAY);
    }

    const std::string capacityLine =
        "建设能力 " + ChineseNumber(country.totalConstructionCapacity.toDouble()) +
        "  已用 " + ChineseNumber(country.totalConstructionUsed.toDouble()) +
        "  可用 " + ChineseNumber(country.totalConstructionAvailable.toDouble()) +
        "  项目 " + std::to_string(
            static_cast<int>(country.constructionProjects.size()));
    DrawTextEx(font, capacityLine.c_str(),
               {content.x, content.y + 98.0f}, kUiBodyFontSize, 0, DARKGRAY);

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
                      project.id == state->selectedConstructionProjectId
                          ? Color{218, 232, 222, 255}
                          : Color{250, 251, 248, 255});
        std::string provinceName = "#" + std::to_string(project.targetProvinceId);
        if (project.targetProvinceId >= 0)
            provinceName = world.getProvinceById(project.targetProvinceId).getName();
        DrawProjectLine(font, "#" + std::to_string(project.id) + " " + provinceName,
                   {content.x + 7.0f, y + 1.0f, content.width - 119.0f, 20.0f},
                   {35, 48, 47, 255});
        const double progressPercent = project.progress.toDouble() * 100.0;
        const std::string projectLine =
            std::string(BuildingLabel(project.typeIndex)) + " " +
            ChineseNumber(static_cast<double>(project.quantity), 2) + " 座  " +
            ChineseNumber(progressPercent, 1) + "%";
        DrawProjectLine(font, projectLine.c_str(),
                   {content.x + 7.0f, y + 20.0f, content.width - 143.0f, 20.0f},
                   DARKGRAY);
        DrawConstructionQueueButtons(
            CountryQueueButtons({content.x, y - 4.0f, content.width,
                                 list.rowHeight}),
            IsProjectCancellable(project.status) && index > 0,
            IsProjectCancellable(project.status) && index + 1 < count);
        if (IsProjectCancellable(project.status)) {
            const Rectangle cancel = {content.x + content.width - 50.0f,
                                      y - 2.0f, 48.0f, 24.0f};
            DrawRectangleRec(cancel, {157, 71, 64, 235});
            DrawTextEx(font, "取消", {cancel.x + 5.0f, cancel.y + 4.0f},
                       kUiBodyFontSize, 0, RAYWHITE);
        }
        DrawTextEx(font, ConstructionProjectStatusText(
                       static_cast<ConstructionProjectStatus>(project.status)),
                   {content.x + content.width - 130.0f, y + 20.0f}, kUiBodyFontSize, 0,
                   ProjectStatusColor(project.status));
        const double progress = std::clamp(
            project.progress.toDouble(), 0.0, 1.0);
        const Rectangle progressBounds = {
            content.x + 7.0f, y + 38.0f,
            std::max(1.0f, content.width - 14.0f), 6.0f};
        DrawRectangleRec(progressBounds, Color{215, 222, 217, 255});
        DrawRectangle(
            static_cast<int>(progressBounds.x),
            static_cast<int>(progressBounds.y),
            static_cast<int>(progressBounds.width * progress),
            static_cast<int>(progressBounds.height),
            Color{63, 137, 87, 255});
    }
    if (count == 0)
        DrawTextEx(font, "暂无国家建设项目。", {content.x, list.firstY}, kUiBodyFontSize, 0, GRAY);
}

}  // namespace

void HandleCountryOverviewInput(UIState* state, World& world) {
    if (state->playerCountryId < 0) {
        state->view = UIView::WorldMap;
        state->panelConsumesInput = false;
        return;
    }
    state->selectedCountryId = state->playerCountryId;
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

    const float wheel = GetMouseWheelMove();
    const bool up = IsKeyPressed(KEY_UP);
    const bool down = IsKeyPressed(KEY_DOWN);
    if (!left && wheel == 0.0f && !up && !down) return;

    const int countryId = state->playerCountryId;
    const CountrySnapshot country = world.getCountrySnapshot(countryId);
    const int constructionTarget =
        ResolveConstructionTarget(state, country);
    if (constructionTarget >= 0) {
        state->countrySelectedProvinceId = constructionTarget;
    }
    const int count = state->countryTab == 0
        ? static_cast<int>(country.provinceIds.size())
        : static_cast<int>(country.constructionProjects.size());
    const CountryProvinceListLayout list =
        ComputeCountryProvinceListLayout(layout.width, layout.height,
                                         state->countryTab, count);
    if (CheckCollisionPointRec(mouse, layout.countryPageContent) && wheel != 0.0f)
        state->countryProvinceScroll += wheel > 0.0f ? -1 : 1;
    if (up) --state->countryProvinceScroll;
    if (down) ++state->countryProvinceScroll;
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
            world, countryId, constructionTarget,
            state->selectedBuilding);
        if (next >= 0) state->selectedBuilding = next;
        return;
    }
    const Rectangle issue = {content.x + content.width - 78.0f,
                             content.y + 45.0f, 78.0f, 24.0f};
    if (CheckCollisionPointRec(mouse, issue)) {
        RecordConstructionResult(
            state, world.queueNationalConstructionCommand(
                countryId, constructionTarget,
                state->selectedBuilding, 1));
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
        const ConstructionQueueButtons buttons = CountryQueueButtons(rowRect);
        for (const bool moveUp : {true, false}) {
            if (!ConstructionQueueButtonHit(mouse, buttons, moveUp)) continue;
            if (IsProjectCancellable(project.status) &&
                (moveUp ? index > 0 : index + 1 < count)) {
                state->constructionSucceeded = world.moveConstructionProject(
                    countryId, project.id, moveUp,
                    ConstructionQueueShiftHeld());
                state->constructionMessage = state->constructionSucceeded
                    ? "建设队列顺序已更新"
                    : "项目操作失败";
            }
            return;
        }
        const Rectangle cancel = {content.x + content.width - 50.0f,
                                  list.firstY + row * list.rowStride - 2.0f,
                                  48.0f, 24.0f};
        if (IsProjectCancellable(project.status) &&
            CheckCollisionPointRec(mouse, cancel)) {
            world.cancelNationalConstructionProject(
                countryId, project.id);
        } else {
            state->selectedConstructionProjectId = project.id;
        }
        return;
    }
}

void DrawCountryOverviewUI(const UIState* state, World& world, Font font,
                           double elapsedSeconds) {
    (void)elapsedSeconds;
    if (state->playerCountryId < 0) return;
    const UILayout layout = CurrentUILayout();
    const CountrySnapshot country =
        world.getCountrySnapshot(state->playerCountryId);
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
