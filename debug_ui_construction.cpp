#include "debug_ui_internal.h"

#include "construction_ui_text.h"
#include "construction_queue.h"
#include "construction_queue_ui.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace debug_ui {
namespace {

ConstructionProjectSnapshot SnapshotOf(
    const ConstructionProject& project, bool historical) {
    ConstructionProjectSnapshot value;
    value.id = project.id;
    value.clientRequestId = project.clientRequestId;
    value.sequence = project.sequence;
    value.payerCountryId = project.payerCountryId;
    value.payerCountryTag = project.payerCountryTag;
    value.targetProvinceId = project.targetProvinceId;
    value.typeIndex = project.typeIndex;
    value.quantity = project.quantity;
    value.completedUnits = project.completedUnits;
    value.totalBudget = project.totalBudget;
    value.unitPrice = project.unitPrice;
    value.maximumUnitPrice = project.maximumUnitPrice;
    value.reservedBudget = project.reservedBudget;
    value.paidBudget = project.paidBudget;
    value.startupCapitalPerUnit = project.startupCapitalPerUnit;
    value.reservedStartupCapital = project.reservedStartupCapital;
    value.paidStartupCapital = project.paidStartupCapital;
    value.totalConstruction = project.totalConstruction;
    value.remainingConstruction = project.remainingConstruction;
    value.currentUnitProgress = project.currentUnitProgress;
    value.expectedProfitPriority = project.expectedProfitPriority;
    value.progress = project.totalConstruction > Money(0)
        ? (project.totalConstruction - project.remainingConstruction) /
              project.totalConstruction
        : Money(0);
    value.priority = project.priority;
    value.fundingKind = static_cast<int>(project.funding.kind);
    value.ownerType = static_cast<int>(project.owner.type);
    value.createdStep = project.createdStep;
    value.lastSettledStep = project.lastSettledStep;
    value.finishedStep = project.finishedStep;
    value.status = static_cast<int>(project.status);
    value.blockReason = static_cast<int>(project.blockReason);
    value.historical = historical;
    return value;
}

std::vector<ConstructionProjectSnapshot> Projects(
    const DebugUIState* state, World& world, const LocalMarket& market) {
    const Country* country = market.getFiscalCountry();
    if (country != nullptr) {
        const CountrySnapshot snapshot =
            world.getCountrySnapshot(country->getId());
        return state->constructionHistoryVisible
            ? snapshot.constructionHistory : snapshot.constructionProjects;
    }
    const auto& source = state->constructionHistoryVisible
        ? market.getConstructionHistory() : market.getConstructionQueue();
    std::vector<ConstructionProjectSnapshot> result;
    result.reserve(source.size());
    for (const ConstructionProject& project : source)
        result.push_back(SnapshotOf(
            project, state->constructionHistoryVisible));
    if (state->constructionHistoryVisible) {
        std::stable_sort(
            result.begin(), result.end(),
            [](const ConstructionProjectSnapshot& left,
               const ConstructionProjectSnapshot& right) {
                if (left.finishedStep != right.finishedStep)
                    return left.finishedStep > right.finishedStep;
                return left.id > right.id;
            });
    } else {
        std::stable_sort(
            result.begin(), result.end(),
            ConstructionQueueOrder{});
    }
    return result;
}

const ConstructionProjectSnapshot* Selected(
    const DebugUIState* state,
    const std::vector<ConstructionProjectSnapshot>& projects) {
    const auto found = std::find_if(
        projects.begin(), projects.end(),
        [state](const ConstructionProjectSnapshot& project) {
            return project.id == state->selectedConstructionProjectId;
        });
    return found == projects.end() ? nullptr : &*found;
}

void RecordConstructionResult(
    DebugUIState* state, const ConstructionCommandResult& result) {
    state->constructionSucceeded = static_cast<bool>(result);
    state->constructionMessage = result
        ? "建设 project queued: #" +
              std::to_string(result.projectId)
        : ConstructionCommandErrorText(result.error);
}

struct PanelGeometry {
    bool compact = false;
    float sectionY = 0.0f;
    float headerY = 0.0f;
    float rowsY = 0.0f;
    float rowHeight = 40.0f;
    int visibleRows = 1;
    Rectangle liveTab{};
    Rectangle historyTab{};
    Rectangle pause{};
    Rectangle budget{};
    Rectangle cancel{};
};

PanelGeometry Geometry(const DebugLayout& layout) {
    PanelGeometry result;
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;
    result.compact = width < 850.0f;
    if (result.compact) result.rowHeight = 76.0f;
    result.sectionY = y + (result.compact ? 134.0f : 154.0f);
    result.headerY = result.sectionY + 35.0f;
    result.rowsY = result.headerY + 29.0f;
    result.visibleRows = std::max(
        1, static_cast<int>(
            (layout.originY + layout.height - result.rowsY - 16.0f) /
            result.rowHeight));
    result.liveTab = {x + width - 138.0f, result.sectionY, 64.0f, 25.0f};
    result.historyTab = {
        x + width - 70.0f, result.sectionY, 70.0f, 25.0f};
    const float controlY = y + 39.0f;
    result.pause = {x + width - 378.0f, controlY, 66.0f, 24.0f};
    result.budget = {x + width - 192.0f, controlY, 70.0f, 24.0f};
    result.cancel = {x + width - 118.0f, controlY, 62.0f, 24.0f};
    if (result.compact) {
        result.pause = {x, controlY, 66.0f, 24.0f};
        result.budget = {x + 70.0f, controlY, 70.0f, 24.0f};
        result.cancel = {x + 144.0f, controlY, 62.0f, 24.0f};
    }
    return result;
}

bool ApplyProjectAction(DebugUIState* state, World& world,
                        LocalMarket& market,
                        const ConstructionProjectSnapshot& project,
                        Rectangle target, Vector2 mouse, int action) {
    if (!CheckCollisionPointRec(mouse, target)) return false;
    const Country* country = market.getFiscalCountry();
    bool success = false;
    if (country != nullptr) {
        switch (action) {
        case 0:
            success = project.status ==
                    static_cast<int>(ConstructionProjectStatus::Paused)
                ? world.resumeConstructionProject(country->getId(), project.id)
                : world.pauseConstructionProject(country->getId(), project.id);
            break;
        case 3:
            success = world.addConstructionProjectBudget(
                country->getId(), project.id,
                std::max(Money(100), project.totalBudget / Money(10)));
            break;
        case 4:
            success = world.cancelNationalConstructionProject(
                country->getId(), project.id);
            break;
        }
    } else {
        switch (action) {
        case 0:
            success = project.status ==
                    static_cast<int>(ConstructionProjectStatus::Paused)
                ? market.resumeConstructionProject(project.id)
                : market.pauseConstructionProject(project.id);
            break;
        case 3:
            success = market.addConstructionProjectBudget(
                project.id,
                std::max(Money(100), project.totalBudget / Money(10)));
            break;
        case 4:
            success = market.cancelConstructionProject(project.id);
            break;
        }
    }
    state->constructionSucceeded = success;
    state->constructionMessage =
        success ? "项目已更新" : "项目操作失败";
    if (success && action == 4)
        state->selectedConstructionProjectId = 0;
    return true;
}

}  // namespace

bool HandleConstructionPanelInput(DebugUIState* state, World& world,
                                  const DebugLayout& layout) {
    if (state == nullptr || state->selectedMarket < 0 ||
        state->selectedMarket >= world.getMarketCount() ||
        !IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        return false;
    }
    LocalMarket& market = world.getMarket(state->selectedMarket);
    const Vector2 mouse = GetMousePosition();
    const PanelGeometry geometry = Geometry(layout);
    if (CheckCollisionPointRec(
            mouse, ConstructionDepartmentButton(layout))) {
        RecordConstructionResult(
            state, market.playerBuildCommand(CONST_DEPT, 1));
        return true;
    }
    if (CheckCollisionPointRec(mouse, geometry.liveTab)) {
        state->constructionHistoryVisible = false;
        state->selectedConstructionProjectId = 0;
        state->constructionScroll = 0;
        return true;
    }
    if (CheckCollisionPointRec(mouse, geometry.historyTab)) {
        state->constructionHistoryVisible = true;
        state->selectedConstructionProjectId = 0;
        state->constructionScroll = 0;
        return true;
    }

    const auto projects = Projects(state, world, market);
    const ConstructionProjectSnapshot* selected = Selected(state, projects);
    if (!state->constructionHistoryVisible && selected != nullptr &&
        ConstructionProjectIsLive(
            static_cast<ConstructionProjectStatus>(selected->status))) {
        if (ApplyProjectAction(
                state, world, market, *selected,
                geometry.pause, mouse, 0) ||
            ApplyProjectAction(
                state, world, market, *selected,
                geometry.budget, mouse, 3) ||
            ApplyProjectAction(
                state, world, market, *selected,
                geometry.cancel, mouse, 4)) {
            return true;
        }
    }

    const int maxScroll = std::max(
        0, static_cast<int>(projects.size()) - geometry.visibleRows);
    state->constructionScroll =
        std::clamp(state->constructionScroll, 0, maxScroll);
    const int end = std::min(
        static_cast<int>(projects.size()),
        state->constructionScroll + geometry.visibleRows);
    for (int index = state->constructionScroll; index < end; ++index) {
        const Rectangle row = {
            layout.contentX,
            geometry.rowsY +
                (index - state->constructionScroll) * geometry.rowHeight,
            layout.contentWidth, geometry.rowHeight};
        if (CheckCollisionPointRec(mouse, row)) {
            const ConstructionQueueButtons buttons =
                ConstructionQueueRowButtons(row);
            for (const bool up : {true, false}) {
                if (!ConstructionQueueButtonHit(mouse, buttons, up)) continue;
                if (!state->constructionHistoryVisible &&
                    ConstructionProjectIsLive(static_cast<
                        ConstructionProjectStatus>(projects[index].status)) &&
                    (up ? index > 0 : index + 1 <
                        static_cast<int>(projects.size()))) {
                    state->constructionSucceeded =
                        market.moveConstructionProject(
                            projects[index].id, up,
                            ConstructionQueueShiftHeld());
                    state->constructionMessage = state->constructionSucceeded
                        ? "建设队列顺序已更新"
                        : "项目操作失败";
                }
                return true;
            }
            state->selectedConstructionProjectId = projects[index].id;
            return true;
        }
    }
    return false;
}

void DrawConstructionPanel(DebugUIState* state, World& world, Font font,
                           const DebugLayout& layout) {
    const LocalMarket& market = world.getMarket(state->selectedMarket);
    const Country* country = market.getFiscalCountry();
    const CountrySnapshot countrySnapshot = country != nullptr
        ? world.getCountrySnapshot(country->getId()) : CountrySnapshot{};
    const auto projects = Projects(state, world, market);
    const ConstructionProjectSnapshot* selected = Selected(state, projects);
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;
    const PanelGeometry geometry = Geometry(layout);

    DrawFittedText(font, "建设",
                   {x, y, geometry.compact ? width - 162.0f : 140.0f, 34.0f},
                   kDebugPageTitleFontSize, kText);
    if (!geometry.compact)
        DrawTextAt(font, market.getMarketName(), x + 140.0f, y + 7.0f,
                   kDebugBodyFontSize, kMuted);
    if (!geometry.compact && country != nullptr) {
        DrawTextAt(font, "国库 " + NumberText(countrySnapshot.treasury) +
                         "  已预留 " +
                         NumberText(countrySnapshot.reservedConstructionBudget) +
                         "  可用 " +
                         NumberText(countrySnapshot.availableTreasury),
                   x + 360.0f, y + 7.0f,
                   kDebugCaptionFontSize, kGold);
    } else if (!geometry.compact) {
        DrawTextAt(font, "沙盒 " + NumberText(market.getPlayerCash()) +
                         "  已预留 " +
                         NumberText(
                             market.getReservedSandboxConstructionBudget()),
                   x + 360.0f, y + 7.0f,
                   kDebugCaptionFontSize, kGold);
    }
    DrawButton(font, ConstructionDepartmentButton(layout),
               "+ 建设部门", false, kGreen);

    const bool controllable =
        selected != nullptr && !state->constructionHistoryVisible &&
        ConstructionProjectIsLive(
            static_cast<ConstructionProjectStatus>(selected->status));
    const bool paused = controllable &&
        selected->status ==
            static_cast<int>(ConstructionProjectStatus::Paused);
    DrawButton(font, geometry.pause, paused ? "恢复" : "暂停",
               controllable, kBlue);
    if (!geometry.compact)
        DrawFittedText(font, "Shift：首项/末项",
                       {geometry.pause.x + 70.0f, geometry.pause.y,
                        110.0f, geometry.pause.height},
                       kDebugCaptionFontSize, kMuted);
    DrawButton(font, geometry.budget, "增加资金",
               controllable, kOrange);
    DrawButton(font, geometry.cancel, "取消",
               controllable, kRed);

    Money remaining = Money(0);
    for (const ConstructionProjectSnapshot& project : projects)
        remaining += project.remainingConstruction;

    Money totalCapacity = Money(0);
    Money used = Money(0);
    Money available = Money(0);
    if (country != nullptr) {
        totalCapacity = countrySnapshot.totalConstructionCapacity;
        used = countrySnapshot.totalConstructionUsed;
        available = countrySnapshot.totalConstructionAvailable;
    } else {
        totalCapacity = std::max(
            Money(0), market.getLastConstrProduced());
        used = std::max(Money(0), market.getLastConstrUsed());
        available = std::max(Money(0), totalCapacity - used);
    }
    if (!geometry.compact && !state->constructionMessage.empty()) {
        DrawFittedText(
            font, state->constructionMessage,
            {x, y + 42.0f, std::max(1.0f, width - 390.0f), 20.0f},
            kDebugCaptionFontSize,
            state->constructionSucceeded ? kGreen : kRed);
    }
    const float metricY = y + 66.0f;
    const float gap = 12.0f;
    const float metricWidth = (width - gap * 3.0f) / 4.0f;
    if (geometry.compact) {
        DrawFittedText(font,
            country != nullptr
                ? "国库 " + NumberText(countrySnapshot.treasury) +
                  " / 已预留 " + NumberText(countrySnapshot.reservedConstructionBudget)
                : "沙盒 " + NumberText(market.getPlayerCash()) +
                  " / 已预留 " + NumberText(market.getReservedSandboxConstructionBudget()),
            {x, metricY, width, 21.0f}, kDebugCaptionFontSize, kGold);
        DrawFittedText(font,
            "建设能力 " + NumberText(totalCapacity) +
            " / 已用 " + NumberText(used) + " / 剩余 " + NumberText(remaining),
            {x, metricY + 21.0f, width, 21.0f}, kDebugCaptionFontSize, kMuted);
        DrawFittedText(font,
            state->constructionMessage.empty()
                ? "队列箭头：移动一项；Shift：首项/末项"
                : state->constructionMessage,
            {x, metricY + 42.0f, width, 23.0f}, kDebugCaptionFontSize,
            state->constructionMessage.empty() ? kMuted :
                (state->constructionSucceeded ? kGreen : kRed));
    } else {
        DrawMetric(font, {x, metricY, metricWidth, 72.0f},
                   "全国建造力 / 周", NumberText(totalCapacity), kGreen);
        DrawMetric(font, {x + metricWidth + gap, metricY,
                           metricWidth, 72.0f},
                   "本周已用", NumberText(used), kBlue);
        DrawMetric(font, {x + (metricWidth + gap) * 2.0f, metricY,
                           metricWidth, 72.0f},
                   "可用能力", NumberText(available),
                   available > Money(1e-7) ? kOrange : kGreen);
        DrawMetric(font, {x + (metricWidth + gap) * 3.0f, metricY,
                           metricWidth, 72.0f},
                   "剩余工作量", NumberText(remaining), kGold);
    }

    DrawSectionTitle(
        font,
        geometry.compact ? "项目" :
            (state->constructionHistoryVisible ? "建设历史" : "进行中项目"),
        x, geometry.sectionY, width,
        geometry.compact ? "" : std::to_string(projects.size()) + " 个项目");
    DrawButton(font, geometry.liveTab, "进行中",
               !state->constructionHistoryVisible, kBlue);
    DrawButton(font, geometry.historyTab, "历史",
               state->constructionHistoryVisible, kBlue);

    const float tableWidth = std::max(1.0f, width - 52.0f);
    const std::array<float, 8> fractions = {
        0.00f, 0.07f, 0.24f, 0.34f, 0.50f, 0.66f, 0.83f, 1.00f
    };
    const char* headers[7] = {
        "编号", "建筑", "数量", "进度",
        "资金 / 所有", "状态", "已付 / 预算"
    };
    DrawRectangle(
        static_cast<int>(x), static_cast<int>(geometry.headerY),
        static_cast<int>(width), 28, Color{235, 238, 235, 255});
    if (geometry.compact) {
        DrawFittedText(font, "项目 / 进度",
                       {x + 5.0f, geometry.headerY, tableWidth, 28.0f},
                       kDebugTableFontSize, kMuted);
    }
    for (int column = 0; !geometry.compact && column < 7; ++column) {
        DrawFittedText(
            font, headers[column],
            {x + tableWidth * fractions[column] + 5.0f, geometry.headerY,
             tableWidth * (fractions[column + 1] - fractions[column]) - 8.0f,
             28.0f},
            kDebugTableFontSize, kMuted);
    }

    const int maxScroll = std::max(
        0, static_cast<int>(projects.size()) - geometry.visibleRows);
    state->constructionScroll =
        std::clamp(state->constructionScroll, 0, maxScroll);
    const int end = std::min(
        static_cast<int>(projects.size()),
        state->constructionScroll + geometry.visibleRows);
    for (int index = state->constructionScroll; index < end; ++index) {
        const ConstructionProjectSnapshot& project = projects[index];
        const float rowY = geometry.rowsY +
            (index - state->constructionScroll) * geometry.rowHeight;
        Color fill = index % 2 == 0 ? kSurface : kBackground;
        if (project.id == state->selectedConstructionProjectId)
            fill = Color{220, 232, 224, 255};
        DrawRectangle(static_cast<int>(x), static_cast<int>(rowY),
                      static_cast<int>(width),
                      static_cast<int>(geometry.rowHeight), fill);
        const bool movable = !state->constructionHistoryVisible &&
            ConstructionProjectIsLive(static_cast<
                ConstructionProjectStatus>(project.status));
        DrawConstructionQueueButtons(
            ConstructionQueueRowButtons({x, rowY, width, geometry.rowHeight}),
            movable && index > 0,
            movable && index + 1 < static_cast<int>(projects.size()));

        const double progress =
            std::clamp(project.progress.toDouble(), 0.0, 1.0);
        const auto status =
            static_cast<ConstructionProjectStatus>(project.status);
        const auto block =
            static_cast<ConstructionBlockReason>(project.blockReason);
        std::string stateText = ConstructionProjectStatusText(status);
        if (block != ConstructionBlockReason::None)
            stateText += " / " +
                std::string(ConstructionBlockReasonText(block));
        const std::array<std::string, 7> cells = {
            std::to_string(project.id),
            project.typeIndex >= 0 && project.typeIndex < TYPE_COUNT
                ? buildingTypeNames[project.typeIndex] : "未知",
            std::to_string(project.completedUnits) + "/" +
                std::to_string(project.quantity),
            NumberText(progress * 100.0, 0) + "%",
            std::string(ConstructionFundingText(
                static_cast<ConstructionFundingKind>(
                    project.fundingKind))) +
                " / " +
                ConstructionOwnerText(
                    static_cast<OwnerType>(project.ownerType)),
            stateText,
            NumberText(project.paidBudget) + " / " +
                NumberText(project.totalBudget)
        };
        if (geometry.compact) {
            const Color textColor = block == ConstructionBlockReason::None
                ? kText : kRed;
            DrawFittedText(font, "#" + cells[0] + "  " + cells[1] + "  " + cells[2],
                {x + 5.0f, rowY + 1.0f, tableWidth - 8.0f, 21.0f},
                kDebugBodyFontSize, textColor);
            DrawFittedText(font, cells[4] + " / " + cells[5],
                {x + 5.0f, rowY + 22.0f, tableWidth - 8.0f, 21.0f},
                kDebugCaptionFontSize, textColor);
            DrawFittedText(font, cells[3] + "  已付 " + cells[6],
                {x + 5.0f, rowY + 43.0f, tableWidth - 8.0f, 21.0f},
                kDebugCaptionFontSize, textColor);
            const Rectangle bar{x + 5.0f, rowY + 67.0f, width - 10.0f, 5.0f};
            DrawRectangleRec(bar, Color{226, 230, 227, 255});
            DrawRectangleRec({bar.x, bar.y, bar.width * static_cast<float>(progress),
                              bar.height}, kGreen);
            continue;
        }
        for (int column = 0; column < 7; ++column) {
            if (column == 3) continue;
            DrawFittedText(
                font, cells[column],
                {x + tableWidth * fractions[column] + 5.0f, rowY,
                 tableWidth * (fractions[column + 1] - fractions[column]) - 8.0f,
                 geometry.rowHeight},
                kDebugBodyFontSize,
                block == ConstructionBlockReason::None ? kText : kRed);
        }
        const Rectangle progressBounds = {
            x + tableWidth * fractions[3] + 6.0f, rowY + 10.0f,
            std::max(
                1.0f,
                tableWidth * (fractions[4] - fractions[3]) - 12.0f),
            20.0f};
        DrawRectangleRec(progressBounds, Color{226, 230, 227, 255});
        DrawRectangle(
            static_cast<int>(progressBounds.x),
            static_cast<int>(progressBounds.y),
            static_cast<int>(progressBounds.width * progress),
            static_cast<int>(progressBounds.height), kGreen);
        DrawFittedText(
            font, cells[3], progressBounds, kDebugMinimumFontSize,
            progress > 0.55 ? WHITE : kText, 4.0f);
        DrawLineEx(
            {x, rowY + geometry.rowHeight},
            {x + width, rowY + geometry.rowHeight}, 1.0f, kBorder);
    }
    if (projects.empty()) {
        DrawTextAt(
            font,
            state->constructionHistoryVisible
                ? "暂无建设历史。" : "暂无进行中的项目。",
            x, geometry.rowsY + 20.0f,
            kDebugBodyFontSize, kMuted);
    }
}

}  // namespace debug_ui
