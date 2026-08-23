#include "debug_ui_internal.h"


#include <algorithm>
#include <array>
#include <string>

namespace debug_ui {
void DrawConstructionPanel(DebugUIState* state, World& world, Font font,
                           const DebugLayout& layout) {
    const LocalMarket& market = world.getMarket(state->selectedMarket);
    const Country* country = market.getFiscalCountry();
    const CountrySnapshot countrySnapshot = country != nullptr
        ? world.getCountrySnapshot(country->getId()) : CountrySnapshot{};
    const auto& queue = countrySnapshot.constructionProjects;
    const float x = layout.contentX;
    const float y = layout.contentY;
    const float width = layout.contentWidth;

    DrawTextAt(font, "建造队列", x, y,
               kDebugPageTitleFontSize, kText);
    DrawTextAt(font, market.getMarketName(), x + 140.0f, y + 7.0f,
               kDebugBodyFontSize, kMuted);
    if (country != nullptr) {
        DrawTextAt(font, "国库 " + NumberText(countrySnapshot.treasury) +
                         "  预留 " + NumberText(countrySnapshot.reservedConstructionBudget) +
                         "  可用 " + NumberText(countrySnapshot.availableTreasury),
                   x + 360.0f, y + 7.0f, kDebugCaptionFontSize, kGold);
    }
    DrawButton(font, ConstructionDepartmentButton(layout),
               "+ 建造部门", false, kGreen);

    Money remaining = Money(0);
    for (const ConstructionProjectSnapshot& project : queue)
        remaining += project.remainingConstruction;

    // The queue belongs to the country, so construction capacity is a
    // national metric. Sum every province instead of showing the selected
    // province's local construction department only.
    Money produced = Money(0);
    Money used = Money(0);
    Money idle = Money(0);
    if (country != nullptr) {
        for (const int provinceId : countrySnapshot.provinceIds) {
            const LocalMarket& provinceMarket =
                world.getProvinceById(provinceId).getLocalMarket();
            const Money provinceProduced =
                std::max(Money(0), provinceMarket.getLastConstrProduced());
            const Money provinceUsed =
                std::max(Money(0), provinceMarket.getLastConstrUsed());
            produced += provinceProduced;
            used += provinceUsed;
        }
    } else {
        produced = std::max(Money(0), market.getLastConstrProduced());
        used = std::max(Money(0), market.getLastConstrUsed());
    }
    idle = std::max(Money(0), produced - used);
    const float metricY = y + 46.0f;
    const float gap = 12.0f;
    const float metricWidth = (width - gap * 3.0f) / 4.0f;
    DrawMetric(font, {x, metricY, metricWidth, 72.0f},
               "全国建造力产出 / 周", NumberText(produced), kGreen);
    DrawMetric(font, {x + metricWidth + gap, metricY,
                       metricWidth, 72.0f},
               "本周使用", NumberText(used), kBlue);
    DrawMetric(font, {x + (metricWidth + gap) * 2.0f, metricY,
                       metricWidth, 72.0f},
               "闲置能力", NumberText(idle),
               idle > Money(1e-7) ? kOrange : kGreen);
    DrawMetric(font, {x + (metricWidth + gap) * 3.0f, metricY,
                       metricWidth, 72.0f},
               "队列剩余", NumberText(remaining), kGold);

    const float sectionY = metricY + 88.0f;
    DrawSectionTitle(font, "项目明细", x, sectionY, width,
                     std::to_string(queue.size()) + " 项");
    const float headerY = sectionY + 35.0f;
    const float headerHeight = 28.0f;
    const std::array<float, 7> fractions = {
        0.00f, 0.10f, 0.31f, 0.58f, 0.72f, 0.86f, 1.00f
    };
    const char* headers[6] = {
        "序号", "建筑", "进度", "剩余", "所有者", "付款方"
    };
    DrawRectangle(static_cast<int>(x), static_cast<int>(headerY),
                   static_cast<int>(width), static_cast<int>(headerHeight),
                   Color{235, 238, 235, 255});
    for (int column = 0; column < 6; ++column) {
        DrawFittedText(font, headers[column],
                       {x + width * fractions[column] + 5.0f, headerY,
                        width * (fractions[column + 1] -
                                  fractions[column]) - 8.0f, headerHeight},
                       kDebugTableFontSize, kMuted);
    }

    const float rowsY = headerY + headerHeight + 1.0f;
    const float rowHeight = 40.0f;
    const int visible = std::max(
        1, static_cast<int>((layout.height - rowsY - 16.0f) / rowHeight));
    const int maxScroll =
        std::max(0, static_cast<int>(queue.size()) - visible);
    state->constructionScroll =
        std::clamp(state->constructionScroll, 0, maxScroll);
    const int end = std::min(
        static_cast<int>(queue.size()),
        state->constructionScroll + visible);
    for (int index = state->constructionScroll; index < end; ++index) {
        const ConstructionProjectSnapshot& project = queue[index];
        const float rowY =
            rowsY + (index - state->constructionScroll) * rowHeight;
        DrawRectangle(static_cast<int>(x), static_cast<int>(rowY),
                      static_cast<int>(width), static_cast<int>(rowHeight),
                      index % 2 == 0 ? kSurface : kBackground);
        const double total = project.totalConstruction.toDouble();
        const double completed =
            std::max(0.0, total - project.remainingConstruction.toDouble());
        const double progress = total > 0.0
            ? std::clamp(completed / total, 0.0, 1.0) : 0.0;
        const std::array<std::string, 6> cells = {
            std::to_string(index + 1),
            project.typeIndex >= 0 && project.typeIndex < TYPE_COUNT
                ? buildingTypeNames[project.typeIndex] : "未知",
            "",
            NumberText(project.remainingConstruction),
            "政府",
            project.payerCountryTag.empty() ? "--" : project.payerCountryTag
        };
        for (int column = 0; column < 6; ++column) {
            if (column == 2) continue;
            DrawFittedText(font, cells[column],
                           {x + width * fractions[column] + 5.0f, rowY,
                            width * (fractions[column + 1] -
                                     fractions[column]) - 8.0f,
                            rowHeight},
                            kDebugBodyFontSize, kText);
        }
        const Rectangle progressBounds = {
            x + width * fractions[2] + 6.0f, rowY + 10.0f,
            width * (fractions[3] - fractions[2]) - 12.0f, 20.0f
        };
        DrawRectangleRec(progressBounds, Color{226, 230, 227, 255});
        DrawRectangle(static_cast<int>(progressBounds.x),
                      static_cast<int>(progressBounds.y),
                      static_cast<int>(progressBounds.width * progress),
                      static_cast<int>(progressBounds.height), kGreen);
        DrawFittedText(font,
                       NumberText(progress * 100.0, 0) + "%",
                       progressBounds, kDebugMinimumFontSize,
                       progress > 0.55 ? WHITE : kText, 4.0f);
        DrawLineEx({x, rowY + rowHeight},
                   {x + width, rowY + rowHeight}, 1.0f, kBorder);
    }
    if (queue.empty()) {
        DrawTextAt(font, "当前市场没有在建项目。",
                   x, rowsY + 20.0f, kDebugBodyFontSize, kMuted);
    }
}


}  // namespace debug_ui
