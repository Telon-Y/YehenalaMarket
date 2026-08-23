#include "ui_internal.h"

#include <algorithm>
#include <string>

namespace {

constexpr Color kPanelBackground{244, 247, 244, 252};
constexpr Color kHeader{35, 49, 50, 255};

const char* BuildingLabel(int type) {
    return type >= 0 && type < TYPE_COUNT
        ? buildingTypeNames[static_cast<std::size_t>(type)].c_str()
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

bool ProjectCancellable(int status) {
    return status == static_cast<int>(ConstructionProjectStatus::Queued) ||
           status == static_cast<int>(ConstructionProjectStatus::Active);
}

int ActiveCountryId(const UIState* state, const World& world) {
    if (state != nullptr && state->selectedCountryId >= 0)
        return state->selectedCountryId;
    if (state != nullptr && state->selectedProvinceId >= 0) {
        try {
            return world.getProvinceById(state->selectedProvinceId).getCountryId();
        } catch (...) {
            return -1;
        }
    }
    if (world.getProvinceCount() > 0)
        return world.getCurrentProvince().getCountryId();
    return -1;
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

std::string TargetName(const World& world, int provinceId) {
    if (provinceId < 0) return "Unknown province";
    try {
        return world.getProvinceById(provinceId).getName();
    } catch (...) {
        return "Unknown province";
    }
}

void DrawConstructionButton(const UIState* state, Font font) {
    const UILayout layout = CurrentUILayout();
    const Rectangle button = layout.constructionButton;
    const Color fill = state->constructionPanelOpen
        ? Color{157, 71, 64, 245} : Color{61, 126, 91, 245};
    DrawRectangleRec(button, fill);
    DrawRectangleLinesEx(button, 1.0f,
                         state->constructionPanelOpen
                             ? Color{224, 145, 133, 255}
                             : Color{151, 211, 169, 255});
    const char* label = state->constructionPanelOpen ? "Close" : "Build list";
    const Vector2 size = MeasureTextEx(
        font, label, kUiBodyFontSize, 0.0f);
    DrawTextEx(font, label,
               {button.x + (button.width - size.x) * 0.5f,
                button.y + (button.height - size.y) * 0.5f},
               kUiBodyFontSize, 0.0f, RAYWHITE);
}

}  // namespace

bool HandleConstructionListInput(UIState* state, World& world) {
    if (state == nullptr) return false;
    const UILayout layout = CurrentUILayout();
    const Vector2 mouse = GetMousePosition();
    const bool left = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
    if (left && CheckCollisionPointRec(mouse, layout.constructionButton)) {
        state->constructionPanelOpen = !state->constructionPanelOpen;
        state->constructionListScroll = 0;
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
    const int count = static_cast<int>(country.constructionProjects.size());
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
            const ConstructionProjectSnapshot& project =
                country.constructionProjects[static_cast<std::size_t>(index)];
            const Rectangle rowRect = ConstructionRow(layout, row);
            if (!CheckCollisionPointRec(mouse, rowRect)) continue;
            const Rectangle cancel = {
                rowRect.x + rowRect.width - 66.0f, rowRect.y + 4.0f,
                62.0f, 28.0f};
            if (ProjectCancellable(project.status) &&
                CheckCollisionPointRec(mouse, cancel)) {
                world.cancelNationalConstructionProject(country.countryId,
                                                         project.id);
            }
            break;
        }
    }
    return true;
}

void DrawConstructionListUI(const UIState* state, World& world, Font font) {
    if (state == nullptr) return;
    const UILayout layout = CurrentUILayout();
    if (state->constructionPanelOpen) {
        CountrySnapshot country;
        const bool hasCountry = ReadCountry(state, world, &country);
        BeginScissorMode(static_cast<int>(layout.constructionPanel.x),
                         static_cast<int>(layout.constructionPanel.y),
                         static_cast<int>(layout.constructionPanel.width),
                         static_cast<int>(layout.constructionPanel.height));
        DrawRectangleRec(layout.constructionPanel, kPanelBackground);
        DrawRectangleRec(layout.constructionPanelHeader, kHeader);
        const float x = layout.constructionPanelContent.x;
        if (hasCountry) {
            DrawTextEx(font, "National construction", {x, 16.0f},
                       22.0f, 0.0f, RAYWHITE);
            const std::string& countryCode = country.countryCode.empty()
                ? country.tag : country.countryCode;
            const std::string heading = country.name +
                (countryCode.empty() ? "" : "  [" + countryCode + "]");
            DrawTextEx(font, heading.c_str(), {x, 45.0f}, kUiBodyFontSize, 0.0f,
                       {190, 207, 201, 255});
            DrawTextEx(font, TextFormat("Treasury %.0f",
                                       country.treasury.toDouble()),
                       {x, 72.0f}, kUiBodyFontSize, 0.0f,
                       {226, 233, 229, 255});
            DrawTextEx(font, TextFormat("Reserved %.0f",
                                       country.reservedConstructionBudget.toDouble()),
                       {x, 91.0f}, kUiBodyFontSize, 0.0f,
                       {226, 233, 229, 255});
            DrawTextEx(font, TextFormat("Available %.0f",
                                       country.availableTreasury.toDouble()),
                       {x, 110.0f}, kUiBodyFontSize, 0.0f,
                       {164, 225, 190, 255});
        } else {
            DrawTextEx(font, "National construction", {x, 16.0f},
                       22.0f, 0.0f, RAYWHITE);
            DrawTextEx(font, "No country selected", {x, 48.0f},
                       kUiBodyFontSize, 0.0f, {190, 207, 201, 255});
        }
        EndScissorMode();

        const Rectangle content = layout.constructionPanelContent;
        DrawTextEx(font, "Projects", {content.x, content.y + 1.0f},
                   kUiHeadingFontSize, 0.0f, {33, 55, 53, 255});
        BeginScissorMode(static_cast<int>(layout.constructionPanelList.x),
                         static_cast<int>(layout.constructionPanelList.y),
                         static_cast<int>(layout.constructionPanelList.width),
                         static_cast<int>(layout.constructionPanelList.height));
        if (hasCountry) {
            const int count = static_cast<int>(country.constructionProjects.size());
            const int scroll = std::clamp(
                state->constructionListScroll, 0, MaxScroll(layout, count));
            for (int row = 0; row < layout.constructionPanelVisibleRows; ++row) {
                const int index = scroll + row;
                if (index >= count) break;
                const ConstructionProjectSnapshot& project =
                    country.constructionProjects[static_cast<std::size_t>(index)];
                const Rectangle rowRect = ConstructionRow(layout, row);
                const Color rowFill = row % 2 == 0
                    ? Color{250, 251, 248, 255} : Color{235, 241, 236, 255};
                DrawRectangleRec(rowRect, rowFill);
                const std::string target = TargetName(world, project.targetProvinceId);
                DrawTextEx(font,
                           ("#" + std::to_string(project.id) + "  " + target).c_str(),
                           {rowRect.x + 7.0f, rowRect.y + 4.0f},
                           kUiBodyFontSize, 0.0f, {35, 48, 47, 255});
                DrawTextEx(font,
                           (std::string(BuildingLabel(project.typeIndex)) +
                            " x" + std::to_string(project.quantity)).c_str(),
                            {rowRect.x + 7.0f, rowRect.y + 26.0f},
                           kUiBodyFontSize, 0.0f, DARKGRAY);
                DrawTextEx(font,
                           TextFormat("%.0f%%  paid %.0f/%.0f",
                                      project.progress.toDouble() * 100.0,
                                      project.paidBudget.toDouble(),
                                      project.totalBudget.toDouble()),
                            {rowRect.x + 7.0f, rowRect.y + 48.0f},
                           kUiBodyFontSize, 0.0f, DARKGRAY);
                DrawTextEx(font, ProjectStatus(project.status),
                           {rowRect.x + rowRect.width - 126.0f,
                             rowRect.y + 27.0f},
                           kUiBodyFontSize, 0.0f, ProjectStatusColor(project.status));
                if (ProjectCancellable(project.status)) {
                    const Rectangle cancel = {
                        rowRect.x + rowRect.width - 66.0f, rowRect.y + 4.0f,
                        62.0f, 28.0f};
                    DrawRectangleRec(cancel, {157, 71, 64, 235});
                    DrawTextEx(font, "Cancel", {cancel.x + 7.0f, cancel.y + 5.0f},
                               kUiBodyFontSize, 0.0f, RAYWHITE);
                }
            }
            if (count == 0)
                DrawTextEx(font, "No national projects.",
                           {layout.constructionPanelList.x,
                            layout.constructionPanelList.y + 28.0f},
                           kUiBodyFontSize, 0.0f, GRAY);
        }
        EndScissorMode();
        DrawRectangleLinesEx(layout.constructionPanel, 1.0f,
                             {45, 62, 58, 255});
    }
    DrawConstructionButton(state, font);
}
