#include "ui_internal.h"

#include "construction_ui_text.h"
#include "construction_queue_ui.h"
#include "building_icons.h"
#include "number_format.h"

#include <algorithm>
#include <string>

namespace {

constexpr Color kPanelBackground{244, 247, 244, 252};
constexpr Color kHeader{35, 49, 50, 255};
constexpr Color kGreen{61, 126, 91, 255};
constexpr Color kRed{157, 71, 64, 255};
constexpr Color kBlue{50, 111, 151, 255};
constexpr Color kDisabled{145, 150, 146, 255};

const char* BuildingLabel(int type) {
    return type >= 0 && type < TYPE_COUNT
        ? buildingTypeNames[static_cast<std::size_t>(type)].c_str()
        : "建筑";
}

ConstructionProjectStatus StatusOf(
    const ConstructionProjectSnapshot& project) {
    return static_cast<ConstructionProjectStatus>(project.status);
}

ConstructionBlockReason BlockReasonOf(
    const ConstructionProjectSnapshot& project) {
    return static_cast<ConstructionBlockReason>(project.blockReason);
}

Color ProjectStatusColor(ConstructionProjectStatus status) {
    switch (status) {
    case ConstructionProjectStatus::Completed: return {51, 132, 84, 255};
    case ConstructionProjectStatus::Cancelled:
    case ConstructionProjectStatus::Invalidated: return kRed;
    case ConstructionProjectStatus::Active: return kBlue;
    case ConstructionProjectStatus::Paused: return {105, 105, 105, 255};
    case ConstructionProjectStatus::Queued: return {170, 105, 42, 255};
    }
    return DARKGRAY;
}

bool ProjectControllable(const ConstructionProjectSnapshot& project) {
    return ConstructionProjectIsLive(StatusOf(project));
}

int ActiveCountryId(const UIState* state, const World& world) {
    (void)world;
    return state != nullptr ? state->playerCountryId : -1;
}

bool ReadCountry(const UIState* state, const World& world,
                 CountrySnapshot* snapshot) {
    if (snapshot == nullptr) return false;
    const int countryId = ActiveCountryId(state, world);
    if (countryId < 0) return false;
    try {
        *snapshot = world.getCountrySnapshot(countryId);
        return true;
    } catch (...) {
        return false;
    }
}

const std::vector<ConstructionProjectSnapshot>& VisibleProjects(
    const UIState* state, const CountrySnapshot& country) {
    return state->constructionHistoryVisible
        ? country.constructionHistory : country.constructionProjects;
}

const ConstructionProjectSnapshot* SelectedProject(
    const UIState* state, const CountrySnapshot& country) {
    if (state->selectedConstructionProjectId == 0) return nullptr;
    const auto& projects = VisibleProjects(state, country);
    const auto found = std::find_if(
        projects.begin(), projects.end(),
        [state](const ConstructionProjectSnapshot& project) {
            return project.id == state->selectedConstructionProjectId;
        });
    return found == projects.end() ? nullptr : &*found;
}

int MaxScroll(const UILayout& layout, int count) {
    return std::max(0, count - layout.constructionPanelVisibleRows);
}

Rectangle ConstructionRow(const UILayout& layout, int row) {
    return {layout.constructionPanelList.x,
            layout.constructionPanelList.y +
                row * layout.constructionPanelRowHeight,
            layout.constructionPanelList.width,
            layout.constructionPanelRowHeight - 4.0f};
}

Rectangle LiveTab(const UILayout& layout) {
    return {layout.constructionPanelContent.x +
                layout.constructionPanelContent.width - 126.0f,
            layout.constructionPanelContent.y, 60.0f, 23.0f};
}

Rectangle HistoryTab(const UILayout& layout) {
    return {layout.constructionPanelContent.x +
                layout.constructionPanelContent.width - 62.0f,
            layout.constructionPanelContent.y, 62.0f, 23.0f};
}

struct ProjectControls {
    Rectangle pause;
    Rectangle budget;
    Rectangle cancel;
};

ProjectControls Controls(const UILayout& layout) {
    const float x = layout.constructionPanelContent.x;
    const float y = layout.constructionPanelContent.y + 29.0f;
    return {
        {x, y, 61.0f, 24.0f},
        {x + 169.0f, y, 66.0f, 24.0f},
        {x + 239.0f, y, 58.0f, 24.0f}
    };
}

std::string TargetName(const World& world, int provinceId) {
    if (provinceId < 0) return "未知省份";
    try {
        return world.getProvinceById(provinceId).getName();
    } catch (...) {
        return "未知省份";
    }
}

int ProjectOutputGood(const World& world,
                      const ConstructionProjectSnapshot& project) {
    if (project.typeIndex < 0 || project.typeIndex >= TYPE_COUNT ||
        project.targetProvinceId < 0) return -1;
    try {
        const auto& templates = world.getProvinceById(
            project.targetProvinceId).getLocalMarket().getBuildingTemplates();
        return templates[static_cast<std::size_t>(project.typeIndex)].outputGood;
    } catch (...) {
        return -1;
    }
}

int QuoteTarget(const UIState* state, const CountrySnapshot& country) {
    if (state->selectedProvinceId >= 0 &&
        std::find(country.provinceIds.begin(), country.provinceIds.end(),
                  state->selectedProvinceId) != country.provinceIds.end()) {
        return state->selectedProvinceId;
    }
    if (state->countrySelectedProvinceId >= 0 &&
        std::find(country.provinceIds.begin(), country.provinceIds.end(),
                  state->countrySelectedProvinceId) !=
            country.provinceIds.end()) {
        return state->countrySelectedProvinceId;
    }
    return country.provinceIds.empty() ? -1 : country.provinceIds.front();
}

ConstructionQuote CurrentQuote(const UIState* state, const World& world,
                               const CountrySnapshot& country) {
    ConstructionRequest request;
    request.countryId = country.countryId;
    request.targetProvinceId = QuoteTarget(state, country);
    request.typeIndex = state->selectedBuilding;
    request.quantity = 1;
    request.funding = {ConstructionFundingKind::CountryTreasury,
                       country.countryId, -1};
    request.owner = {OWNER_GOVERNMENT, country.countryId,
                     request.targetProvinceId};
    return world.quoteConstruction(request);
}

void DrawFittedLine(Font font, const std::string& text, Rectangle bounds,
                    float preferredSize, Color color) {
    const Vector2 measured =
        MeasureTextEx(font, text.c_str(), preferredSize, 0.0f);
    float size = preferredSize;
    if (measured.x > bounds.width && measured.x > 0.0f)
        size = std::max(9.0f, preferredSize * bounds.width / measured.x);
    const Vector2 finalSize = MeasureTextEx(font, text.c_str(), size, 0.0f);
    DrawTextEx(font, text.c_str(),
               {bounds.x, bounds.y + (bounds.height - finalSize.y) * 0.5f},
               size, 0.0f, color);
}

void DrawButton(Font font, Rectangle bounds, const char* label,
                bool enabled, Color activeColor) {
    DrawRectangleRec(bounds, enabled ? activeColor : kDisabled);
    DrawRectangleLinesEx(bounds, 1.0f, {90, 101, 96, 255});
    DrawFittedLine(font, label,
                   {bounds.x + 5.0f, bounds.y,
                    std::max(1.0f, bounds.width - 10.0f), bounds.height},
                   kUiBodyFontSize, RAYWHITE);
}

void DrawConstructionButton(const UIState* state, Font font) {
    const UILayout layout = CurrentUILayout();
    const Rectangle button = layout.constructionButton;
    const Color fill = state->constructionPanelOpen ? kRed : kGreen;
    DrawRectangleRec(button, fill);
    DrawRectangleLinesEx(button, 1.0f,
                         state->constructionPanelOpen
                             ? Color{224, 145, 133, 255}
                             : Color{151, 211, 169, 255});
    DrawFittedLine(font,
                   state->constructionPanelOpen ? "关闭" : "建设列表",
                   button, kUiBodyFontSize, RAYWHITE);
}

void SetActionResult(UIState* state, bool success,
                     const char* successText) {
    state->constructionSucceeded = success;
    state->constructionMessage =
        success ? successText : "建设项目操作失败";
}

}  // namespace

bool HandleConstructionListInput(UIState* state, World& world) {
    if (state == nullptr) return false;
    if (state->playerCountryId < 0) return false;
    if (state->view == UIView::ProvinceDetail ||
        state->view == UIView::CommodityMarket) return false;
    const UILayout layout = CurrentUILayout();
    const Vector2 mouse = GetMousePosition();
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (left && CheckCollisionPointRec(mouse, layout.constructionButton)) {
        state->constructionPanelOpen = !state->constructionPanelOpen;
        state->constructionListScroll = 0;
        state->selectedConstructionProjectId = 0;
        state->panelConsumesInput = state->constructionPanelOpen ||
            state->view != UIView::WorldMap;
        return true;
    }
    if (!state->constructionPanelOpen) return false;
    if (IsKeyPressed(KEY_ESCAPE)) {
        state->constructionPanelOpen = false;
        state->panelConsumesInput = state->constructionPanelOpen ||
            state->view != UIView::WorldMap;
        return true;
    }
    if (!CheckCollisionPointRec(mouse, layout.constructionPanel)) return false;

    state->panelConsumesInput = true;
    CountrySnapshot country;
    if (!ReadCountry(state, world, &country)) return true;

    if (left && CheckCollisionPointRec(mouse, LiveTab(layout))) {
        state->constructionHistoryVisible = false;
        state->constructionListScroll = 0;
        state->selectedConstructionProjectId = 0;
        return true;
    }
    if (left && CheckCollisionPointRec(mouse, HistoryTab(layout))) {
        state->constructionHistoryVisible = true;
        state->constructionListScroll = 0;
        state->selectedConstructionProjectId = 0;
        return true;
    }

    const ConstructionProjectSnapshot* selected =
        SelectedProject(state, country);
    if (!state->constructionHistoryVisible && selected != nullptr &&
        ProjectControllable(*selected) && left) {
        const ProjectControls controls = Controls(layout);
        bool result = false;
        if (CheckCollisionPointRec(mouse, controls.pause)) {
            if (StatusOf(*selected) == ConstructionProjectStatus::Paused) {
                result = world.resumeConstructionProject(
                    country.countryId, selected->id);
                SetActionResult(state, result, "建设项目已恢复");
            } else {
                result = world.pauseConstructionProject(
                    country.countryId, selected->id);
                SetActionResult(state, result, "建设项目已暂停");
            }
            return true;
        }
        if (CheckCollisionPointRec(mouse, controls.budget)) {
            const Money amount =
                std::max(Money(100), selected->totalBudget / Money(10));
            result = world.addConstructionProjectBudget(
                country.countryId, selected->id, amount);
            SetActionResult(state, result, "建设资金已预留");
            return true;
        }
        if (CheckCollisionPointRec(mouse, controls.cancel)) {
            result = world.cancelNationalConstructionProject(
                country.countryId, selected->id);
            SetActionResult(state, result, "建设 project cancelled");
            if (result) state->selectedConstructionProjectId = 0;
            return true;
        }
    }

    const auto& projects = VisibleProjects(state, country);
    const int count = static_cast<int>(projects.size());
    const int maxScroll = MaxScroll(layout, count);
    const float wheel = GetMouseWheelMove();
    if (CheckCollisionPointRec(mouse, layout.constructionPanelList) &&
        wheel != 0.0f)
        state->constructionListScroll += wheel > 0.0f ? -1 : 1;
    if (IsKeyPressed(KEY_UP)) --state->constructionListScroll;
    if (IsKeyPressed(KEY_DOWN)) ++state->constructionListScroll;
    state->constructionListScroll = std::clamp(
        state->constructionListScroll, 0, maxScroll);

    if (left && CheckCollisionPointRec(mouse, layout.constructionPanelList)) {
        const int scroll = state->constructionListScroll;
        for (int row = 0; row < layout.constructionPanelVisibleRows; ++row) {
            const int index = scroll + row;
            if (index >= count) break;
            if (CheckCollisionPointRec(mouse, ConstructionRow(layout, row))) {
                const auto& project = projects[static_cast<std::size_t>(index)];
                const ConstructionQueueButtons buttons =
                    ConstructionQueueRowButtons(ConstructionRow(layout, row));
                for (const bool up : {true, false}) {
                    if (!ConstructionQueueButtonHit(mouse, buttons, up))
                        continue;
                    if (!state->constructionHistoryVisible &&
                        ProjectControllable(project) &&
                        (up ? index > 0 : index + 1 < count)) {
                        SetActionResult(state, world.moveConstructionProject(
                            country.countryId, project.id, up,
                            ConstructionQueueShiftHeld()),
                            "建设队列顺序已更新");
                    }
                    return true;
                }
                state->selectedConstructionProjectId = project.id;
                break;
            }
        }
    }
    return true;
}

void DrawConstructionListUI(const UIState* state, World& world, Font font) {
    if (state == nullptr) return;
    if (state->playerCountryId < 0) return;
    if (state->view == UIView::ProvinceDetail ||
        state->view == UIView::CommodityMarket) return;
    const UILayout layout = CurrentUILayout();
    if (state->constructionPanelOpen) {
        CountrySnapshot country;
        const bool hasCountry = ReadCountry(state, world, &country);
        DrawRectangleRec(layout.constructionPanel, kPanelBackground);
        DrawRectangleRec(layout.constructionPanelHeader, kHeader);
        const float x = layout.constructionPanelContent.x;
        DrawTextEx(font, "国家建设", {x, 16.0f},
                   22.0f, 0.0f, RAYWHITE);
        if (hasCountry) {
            const std::string& code = country.countryCode.empty()
                ? country.tag : country.countryCode;
            const std::string heading = country.name +
                (code.empty() ? "" : "  [" + code + "]");
            DrawFittedLine(
                font, heading,
                {x, 39.0f, layout.constructionPanelContent.width, 25.0f},
                kUiBodyFontSize, {190, 207, 201, 255});
            const std::string treasuryLine =
                "国库 " + FormatChineseNumber(country.treasury.toDouble());
            const std::string reservedLine =
                "已预留 " +
                FormatChineseNumber(country.reservedConstructionBudget.toDouble());
            const std::string availableLine =
                "可用 " + FormatChineseNumber(country.availableTreasury.toDouble());
            DrawTextEx(font, treasuryLine.c_str(),
                       {x, 72.0f}, kUiBodyFontSize, 0.0f,
                       {226, 233, 229, 255});
            DrawTextEx(font, reservedLine.c_str(),
                       {x, 91.0f}, kUiBodyFontSize, 0.0f,
                       {226, 233, 229, 255});
            DrawTextEx(font, availableLine.c_str(),
                       {x, 110.0f}, kUiBodyFontSize, 0.0f,
                       {164, 225, 190, 255});
        } else {
            DrawTextEx(font, "未选择国家", {x, 48.0f},
                       kUiBodyFontSize, 0.0f, {190, 207, 201, 255});
        }

        const Rectangle content = layout.constructionPanelContent;
        DrawTextEx(font,
                   state->constructionHistoryVisible ? "历史" : "项目",
                   {content.x, content.y + 2.0f},
                   kUiHeadingFontSize, 0.0f, {33, 55, 53, 255});
        DrawButton(font, LiveTab(layout), "进行中",
                   !state->constructionHistoryVisible, kBlue);
        DrawButton(font, HistoryTab(layout), "历史",
                   state->constructionHistoryVisible, kBlue);

        if (hasCountry) {
            const ConstructionProjectSnapshot* selected =
                SelectedProject(state, country);
            const ProjectControls controls = Controls(layout);
            const bool controllable =
                selected != nullptr && !state->constructionHistoryVisible &&
                ProjectControllable(*selected);
            const bool paused = controllable &&
                StatusOf(*selected) == ConstructionProjectStatus::Paused;
            DrawButton(font, controls.pause, paused ? "恢复" : "暂停",
                       controllable, kBlue);
            DrawFittedLine(font, "Shift：首项/末项",
                           {content.x + 66.0f, controls.pause.y,
                            98.0f, controls.pause.height},
                           kUiBodyFontSize, GRAY);
            DrawButton(font, controls.budget, "增加资金",
                       controllable, {170, 105, 42, 255});
            DrawButton(font, controls.cancel, "取消",
                       controllable, kRed);

            if (selected != nullptr) {
                const ConstructionBlockReason reason =
                    BlockReasonOf(*selected);
                std::string selectedText =
                    "#" + std::to_string(selected->id) + "  " +
                    ConstructionProjectStatusText(StatusOf(*selected));
                if (reason != ConstructionBlockReason::None)
                    selectedText += "  " + std::string(
                        ConstructionBlockReasonText(reason));
                DrawFittedLine(
                    font, selectedText,
                    {content.x, content.y + 57.0f, content.width, 20.0f},
                    kUiBodyFontSize,
                    reason == ConstructionBlockReason::None
                        ? DARKGRAY : kRed);
            } else {
                DrawTextEx(font, "请选择要管理的项目。",
                           {content.x, content.y + 60.0f},
                           kUiBodyFontSize, 0.0f, GRAY);
            }

            const ConstructionQuote quote =
                CurrentQuote(state, world, country);
            if (quote) {
                const std::string quoteLine =
                    "可用名额 " + FormatChineseNumber(quote.availableSlots, 2) +
                    "  预算 " + FormatChineseNumber(quote.totalBudget.toDouble()) +
                    "  预计 " + FormatChineseNumber(quote.estimatedCycles, 2) + " 周";
                DrawFittedLine(
                    font, quoteLine,
                    {content.x, content.y + 80.0f, content.width, 19.0f},
                    kUiBodyFontSize, DARKGRAY);
                const std::string priceLine =
                    "现价 " +
                    FormatChineseNumber(quote.observedUnitPrice.toDouble()) +
                    "  上限 " +
                    FormatChineseNumber(quote.maximumUnitPrice.toDouble());
                DrawFittedLine(
                    font, priceLine,
                    {content.x, content.y + 97.0f, content.width, 17.0f},
                    kUiBodyFontSize, GRAY);
            } else {
                DrawFittedLine(
                    font,
                    std::string("报价：") +
                        ConstructionCommandErrorText(quote.error),
                    {content.x, content.y + 82.0f, content.width, 20.0f},
                    kUiBodyFontSize, kRed);
            }

            const auto& projects = VisibleProjects(state, country);
            const int count = static_cast<int>(projects.size());
            const int scroll = std::clamp(
                state->constructionListScroll, 0, MaxScroll(layout, count));
            BeginScissorMode(
                static_cast<int>(layout.constructionPanelList.x),
                static_cast<int>(layout.constructionPanelList.y),
                static_cast<int>(layout.constructionPanelList.width),
                static_cast<int>(layout.constructionPanelList.height));
            for (int row = 0;
                 row < layout.constructionPanelVisibleRows; ++row) {
                const int index = scroll + row;
                if (index >= count) break;
                const ConstructionProjectSnapshot& project =
                    projects[static_cast<std::size_t>(index)];
                const Rectangle rowRect = ConstructionRow(layout, row);
                Color rowFill = row % 2 == 0
                    ? Color{250, 251, 248, 255}
                    : Color{235, 241, 236, 255};
                if (project.id == state->selectedConstructionProjectId)
                    rowFill = {218, 232, 222, 255};
                DrawRectangleRec(rowRect, rowFill);
                const Rectangle icon = {
                    rowRect.x + 7.0f, rowRect.y + 6.0f, 40.0f, 40.0f};
                building_icons::DrawBuildingProductionIcon(
                    icon, project.typeIndex,
                    ProjectOutputGood(world, project),
                    project.completedUnits > 0);
                const bool movable = !state->constructionHistoryVisible &&
                    ProjectControllable(project);
                DrawConstructionQueueButtons(
                    ConstructionQueueRowButtons(rowRect),
                    movable && index > 0, movable && index + 1 < count);

                const std::string firstLine =
                    "#" + std::to_string(project.id) + "  " +
                    TargetName(world, project.targetProvinceId);
                DrawFittedLine(
                    font, firstLine,
                    {rowRect.x + 54.0f, rowRect.y + 1.0f,
                     rowRect.width - 152.0f, 20.0f},
                    kUiBodyFontSize, {35, 48, 47, 255});
                DrawFittedLine(
                    font,
                    std::string(BuildingLabel(project.typeIndex)) + "  " +
                        FormatChineseNumber(project.completedUnits, 2) + "/" +
                        FormatChineseNumber(project.quantity, 2) +
                        "  队列第 " + FormatChineseNumber(index + 1, 2) + " 位",
                    {rowRect.x + 54.0f, rowRect.y + 22.0f,
                     rowRect.width - 109.0f, 20.0f},
                    kUiBodyFontSize, DARKGRAY);
                const std::string funding =
                    std::string(ConstructionFundingText(
                        static_cast<ConstructionFundingKind>(
                            project.fundingKind))) +
                    " / " +
                    ConstructionOwnerText(
                        static_cast<OwnerType>(project.ownerType));
                const std::string thirdLine =
                    FormatChineseNumber(project.progress.toDouble() * 100.0, 1) +
                    "%  " + FormatChineseNumber(project.paidBudget.toDouble()) +
                    "/" + FormatChineseNumber(project.totalBudget.toDouble()) +
                    "  " + funding;
                DrawFittedLine(
                    font, thirdLine,
                    {rowRect.x + 54.0f, rowRect.y + 43.0f,
                     rowRect.width - 109.0f, 18.0f},
                    kUiBodyFontSize, DARKGRAY);
                const double progress = std::clamp(
                    project.progress.toDouble(), 0.0, 1.0);
                const Rectangle progressBounds = {
                    rowRect.x + 7.0f, rowRect.y + rowRect.height - 7.0f,
                    std::max(1.0f, rowRect.width - 14.0f), 5.0f};
                DrawRectangleRec(
                    progressBounds, Color{215, 222, 217, 255});
                DrawRectangle(
                    static_cast<int>(progressBounds.x),
                    static_cast<int>(progressBounds.y),
                    static_cast<int>(progressBounds.width * progress),
                    static_cast<int>(progressBounds.height),
                    Color{63, 137, 87, 255});
                const ConstructionProjectStatus status = StatusOf(project);
                DrawFittedLine(
                    font, ConstructionProjectStatusText(status),
                    {rowRect.x + rowRect.width - 95.0f,
                     rowRect.y + 1.0f, 88.0f, 20.0f},
                    kUiBodyFontSize, ProjectStatusColor(status));
            }
            if (count == 0)
                DrawTextEx(font,
                           state->constructionHistoryVisible
                               ? "暂无建设历史。"
                               : "暂无国家建设项目。",
                           {layout.constructionPanelList.x,
                            layout.constructionPanelList.y + 28.0f},
                           kUiBodyFontSize, 0.0f, GRAY);
            EndScissorMode();
        }
        DrawRectangleLinesEx(layout.constructionPanel, 1.0f,
                             {45, 62, 58, 255});
    }
    DrawConstructionButton(state, font);
}
