// ==================== ui_helpers.cpp ====================
// 格式化、边界计算、图表绘制辅助
#include "ui_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <string>

// ===== 扩展的格式化函数：支持 k, m, b, t, p =====
void FormatCash(double cash, char* buf, size_t bufSize) {
    if (cash == 0.0) { snprintf(buf, bufSize, "0.00"); return; }
    double absCash = fabs(cash);
    const char* sign = (cash < 0) ? "-" : "";
    if (absCash >= 1e18) snprintf(buf, bufSize, "%s%.2fe", sign, absCash / 1e18);
    else if (absCash >= 1e15) snprintf(buf, bufSize, "%s%.2fp", sign, absCash / 1e15);
    else if (absCash >= 1e12) snprintf(buf, bufSize, "%s%.2ft", sign, absCash / 1e12);
    else if (absCash >= 1e9) snprintf(buf, bufSize, "%s%.2fb", sign, absCash / 1e9);
    else if (absCash >= 1e6) snprintf(buf, bufSize, "%s%.2fm", sign, absCash / 1e6);
    else if (absCash >= 1e3) snprintf(buf, bufSize, "%s%.2fk", sign, absCash / 1e3);
    else snprintf(buf, bufSize, "%.2f", cash);
}

void FormatCash(const Money& cash, char* buf, size_t bufSize) {
    FormatCash(cash.toDouble(), buf, bufSize);
}

// ===== 统一图表边界计算：上下各留 10% 空白 =====
void ComputeBounds(double minV, double maxV, bool nonNegative,
                   double& outMin, double& outMax) {
    constexpr double EPS = 1e-9;

    if (maxV - minV < EPS) {
        double base = std::max(std::fabs(minV) * 0.1, 1.0);
        maxV = minV + base;
    }

    double padding = (maxV - minV) * 0.1;
    outMin = minV - padding;
    outMax = maxV + padding;

    if (nonNegative && outMin < 0.0) {
        outMin = 0.0;
        double lostRange = minV - padding < 0.0 ? -(minV - padding) : 0.0;
        outMax += lostRange;
    }
}

// ===== 单商品价格曲线 =====
void DrawPriceCurve(const std::vector<std::array<Money, NUM_GOODS>>& hist,
                    int goodIdx, float chartX, float chartY, float chartW, float chartH,
                    Color color, Font font, int startRow) {
    if (hist.empty()) return;
    size_t first = (startRow >= 0) ? (size_t)startRow : 0;
    if (first >= hist.size()) return;
    size_t last = hist.size(), count = last - first;
    if (count < 2) return;

    double minP = 1e30, maxP = -1e30;
    for (size_t i = first; i < last; ++i) {
        double v = hist[i][goodIdx].toDouble();
        if (v < minP) minP = v;
        if (v > maxP) maxP = v;
    }

    double minPlot, maxPlot;
    ComputeBounds(minP, maxP, true, minPlot, maxPlot);

    DrawLine(chartX, chartY, chartX, chartY + chartH, BLACK);
    DrawLine(chartX, chartY + chartH, chartX + chartW, chartY + chartH, BLACK);
    char maxBuf[32], minBuf[32];
    FormatCash(maxPlot, maxBuf, sizeof(maxBuf));
    FormatCash(minPlot, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 16 }, 14, 1, BLACK);

    float prevX = chartX;
    float prevY = chartY + chartH - (float)((hist[first][goodIdx].toDouble() - minPlot) / (maxPlot - minPlot) * chartH);
    for (size_t i = first + 1; i < last; ++i) {
        float x = chartX + (i - first) * chartW / (count - 1);
        float y = chartY + chartH - (float)((hist[i][goodIdx].toDouble() - minPlot) / (maxPlot - minPlot) * chartH);
        DrawLine(prevX, prevY, x, y, color);
        prevX = x; prevY = y;
    }
}

// ===== 标量曲线（Money 版本） =====
void DrawScalarCurve(const std::vector<Money>& data,
                     float chartX, float chartY, float chartW, float chartH,
                     Color color, Font font, const char* label) {
    if (data.size() < 2) return;
    double minV = 1e30, maxV = -1e30;
    for (const auto& v : data) {
        double dv = v.toDouble();
        if (dv < minV) minV = dv;
        if (dv > maxV) maxV = dv;
    }

    double minPlot, maxPlot;
    ComputeBounds(minV, maxV, true, minPlot, maxPlot);

    DrawLine(chartX, chartY, chartX, chartY + chartH, BLACK);
    DrawLine(chartX, chartY + chartH, chartX + chartW, chartY + chartH, BLACK);
    char maxBuf[32], minBuf[32];
    FormatCash(maxPlot, maxBuf, sizeof(maxBuf));
    FormatCash(minPlot, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 16 }, 14, 1, BLACK);
    if (label) DrawTextEx(font, label, { chartX + 2, chartY - 22 }, 16, 1, DARKGRAY);

    size_t count = data.size();
    float prevX = chartX;
    float prevY = chartY + chartH - (float)((data[0].toDouble() - minPlot) / (maxPlot - minPlot) * chartH);
    for (size_t i = 1; i < count; ++i) {
        float x = chartX + (float)i / (count - 1) * chartW;
        float y = chartY + chartH - (float)((data[i].toDouble() - minPlot) / (maxPlot - minPlot) * chartH);
        DrawLine(prevX, prevY, x, y, color);
        prevX = x; prevY = y;
    }
}

// ===== 标量曲线（double 版本） =====
void DrawScalarCurveDouble(const std::vector<double>& data,
                           float chartX, float chartY, float chartW, float chartH,
                           Color color, Font font, const char* label) {
    if (data.size() < 2) return;
    double minV = 1e30, maxV = -1e30;
    for (const auto& v : data) {
        if (v < minV) minV = v;
        if (v > maxV) maxV = v;
    }

    double minPlot, maxPlot;
    ComputeBounds(minV, maxV, true, minPlot, maxPlot);

    DrawLine(chartX, chartY, chartX, chartY + chartH, BLACK);
    DrawLine(chartX, chartY + chartH, chartX + chartW, chartY + chartH, BLACK);
    char maxBuf[32], minBuf[32];
    FormatCash(maxPlot, maxBuf, sizeof(maxBuf));
    FormatCash(minPlot, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 16 }, 14, 1, BLACK);
    if (label) DrawTextEx(font, label, { chartX + 2, chartY - 22 }, 16, 1, DARKGRAY);

    size_t count = data.size();
    float prevX = chartX;
    float prevY = chartY + chartH - (float)((data[0] - minPlot) / (maxPlot - minPlot) * chartH);
    for (size_t i = 1; i < count; ++i) {
        float x = chartX + (float)i / (count - 1) * chartW;
        float y = chartY + chartH - (float)((data[i] - minPlot) / (maxPlot - minPlot) * chartH);
        DrawLine(prevX, prevY, x, y, color);
        prevX = x; prevY = y;
    }
}

// ===== 多商品价格总表 =====
void DrawMultiPriceCurve(const std::vector<std::array<Money, NUM_GOODS>>& hist,
                         const std::vector<int>& goodIndices,
                         const std::vector<Color>& colors,
                         Font font, Rectangle chartRect) {
    if (hist.empty() || goodIndices.empty()) return;
    size_t count = hist.size();
    if (count < 2) return;

    double minV = 1e30, maxV = -1e30;
    for (int g : goodIndices) {
        for (const auto& row : hist) {
            double v = row[g].toDouble();
            if (v < minV) minV = v;
            if (v > maxV) maxV = v;
        }
    }

    double minPlot, maxPlot;
    ComputeBounds(minV, maxV, true, minPlot, maxPlot);

    const float legendWidth = 150;
    const float legendPadding = 5;
    Rectangle plotArea = { chartRect.x, chartRect.y,
                           chartRect.width - legendWidth - legendPadding, chartRect.height };
    Rectangle legendArea = { chartRect.x + chartRect.width - legendWidth, chartRect.y,
                             legendWidth, chartRect.height };
    DrawRectangleRec(legendArea, Fade(RAYWHITE, 0.8f));
    DrawRectangleLinesEx(legendArea, 1, GRAY);

    float legendY = legendArea.y + 3;
    for (size_t i = 0; i < goodIndices.size(); ++i) {
        int g = goodIndices[i];
        DrawRectangle(legendArea.x + 5, legendY, 12, 12, colors[i]);
        DrawRectangleLines(legendArea.x + 5, legendY, 12, 12, BLACK);
        DrawTextEx(font, commodityNames[g].c_str(), { legendArea.x + 20, legendY }, 12, 1, BLACK);
        legendY += 14;
    }

    DrawLine(plotArea.x, plotArea.y, plotArea.x, plotArea.y + plotArea.height, BLACK);
    DrawLine(plotArea.x, plotArea.y + plotArea.height,
             plotArea.x + plotArea.width, plotArea.y + plotArea.height, BLACK);

    char maxBuf[32], minBuf[32];
    FormatCash(maxPlot, maxBuf, sizeof(maxBuf));
    FormatCash(minPlot, minBuf, sizeof(minBuf));
    DrawTextEx(font, maxBuf, { plotArea.x + 2, plotArea.y }, 14, 1, BLACK);
    DrawTextEx(font, minBuf, { plotArea.x + 2, plotArea.y + plotArea.height - 16 }, 14, 1, BLACK);
    DrawTextEx(font, TextFormat("%d", (int)count),
               { plotArea.x + plotArea.width - 60, plotArea.y + plotArea.height - 16 }, 14, 1, BLACK);

    for (size_t idx = 0; idx < goodIndices.size(); ++idx) {
        int g = goodIndices[idx];
        Color col = colors[idx];
        double v0 = hist[0][g].toDouble();
        float prevX = plotArea.x;
        float prevY = plotArea.y + plotArea.height - (float)((v0 - minPlot) / (maxPlot - minPlot) * plotArea.height);
        for (size_t i = 1; i < count; ++i) {
            float x = plotArea.x + (float)i / (count - 1) * plotArea.width;
            double v = hist[i][g].toDouble();
            float y = plotArea.y + plotArea.height - (float)((v - minPlot) / (maxPlot - minPlot) * plotArea.height);
            DrawLine(prevX, prevY, x, y, col);
            prevX = x; prevY = y;
        }
    }
}

// ===== 预估订单剩余周数 =====
int EstimateWeeksLeft(const LocalMarket& market, size_t orderIndex) {
    if (market.getBuildingCounts()[CONST_DEPT] == 0) return -1;
    const auto& constrBt = market.getBuildingTemplates()[CONST_DEPT];
    double capacity = market.getBuildingCounts()[CONST_DEPT] * constrBt.outputRate * market.getEmploymentRatio()[CONST_DEPT];
    if (capacity <= 0.0) return -1;
    std::vector<Money> rem(market.getConstructionQueue().size());
    for (size_t i = 0; i < rem.size(); ++i)
        rem[i] = market.getConstructionQueue()[i].remainingCost;
    int weeks = 0;
    while (orderIndex < rem.size() && rem[orderIndex] > Money(0)) {
        double avail = capacity;
        for (size_t i = 0; i < rem.size() && avail > 0; ++i) {
            if (rem[i] <= Money(0)) continue;
            double invest = std::min({ avail, 30.0, rem[i].toDouble() });
            rem[i] -= Money(invest);
            avail -= invest;
        }
        weeks++;
        if (weeks > 10000) return -1;
    }
    return weeks;
}

// ===== 劳动力比例饼图（不含受抚养人口） =====
void DrawLaborPieChart(const LocalMarket& market, float chartX, float chartY,
                       float radius, Font font) {
    double laborForce = market.getLaborForcePopulation();
    if (laborForce <= 0.0) {
        DrawTextEx(font, "无劳动力", { chartX, chartY }, 16, 1, GRAY);
        return;
    }

    // 按建筑模板汇总实际就业构成；金融建筑使用 500/250/250。
    double employedLaborers = 0.0;
    double engineers = 0.0;
    double capitalists = 0.0;
    const auto& actualEmp = market.getActualEmployment();
    const auto& templates = market.getBuildingTemplates();
    for (int t = 0; t < TYPE_COUNT; ++t) {
        employedLaborers += actualEmp[t] * templates[t].workforceShares[LABORER];
        engineers += actualEmp[t] * templates[t].workforceShares[ENGINEER];
        capitalists += actualEmp[t] * templates[t].workforceShares[CAPITALIST];
    }
    double employedTotal = employedLaborers + engineers + capitalists;

    double subsistence = market.getSubsistencePop();   // 自给农
    double unemployedLaborers = laborForce - employedTotal - subsistence;
    if (unemployedLaborers < 0.0) unemployedLaborers = 0.0;

    Color darkBrown = { 92, 64, 51, 255 };   // 深棕色
    Color navy      = { 0, 0, 128, 255 };    // 海军蓝
    Color lightBlue = { 135, 206, 235, 255 }; // 淡蓝色

    struct Slice {
        const char* label;
        double value;
        Color color;
    };
    Slice slices[] = {
        { "自给农",   subsistence,        lightBlue },
        { "劳工",     employedLaborers,   BROWN },
        { "失业劳工", unemployedLaborers, darkBrown },
        { "工程师",   engineers,          ORANGE },
        { "资本家",   capitalists,        navy }
    };

    Vector2 center = { chartX + radius, chartY + radius };
    float startRad = -PI * 0.5f;   // 从正上方（12点钟）开始

    const int MAX_SEGMENTS = 72;
    for (const auto& s : slices) {
        if (s.value <= 0.0) continue;
        float angleCoverage = (float)(s.value / laborForce) * 2.0f * PI;
        if (angleCoverage <= 0.0f) continue;

        int segs = (int)std::ceil(angleCoverage / (2.0f * PI / MAX_SEGMENTS));
        if (segs < 1) segs = 1;

        float step = angleCoverage / segs;
        for (int i = 0; i < segs; ++i) {
            float a1 = startRad + step * i;
            float a2 = startRad + step * (i + 1);
            Vector2 p1 = { center.x + cosf(a1) * radius, center.y + sinf(a1) * radius };
            Vector2 p2 = { center.x + cosf(a2) * radius, center.y + sinf(a2) * radius };
            // 三角形绕序：center-p2-p1，确保2D下正常填充
            DrawTriangle(center, p2, p1, s.color);
        }
        startRad += angleCoverage;
    }

    // 外轮廓
    DrawCircleLines((int)center.x, (int)center.y, radius, BLACK);

    // 图例
    float legendX = chartX + radius * 2.0f + 20.0f;
    float legendY = chartY;
    char buf[128];

    for (const auto& s : slices) {
        DrawRectangle(legendX, legendY + 2, 14, 14, s.color);
        DrawRectangleLines(legendX, legendY + 2, 14, 14, BLACK);

        double pct = (laborForce > 0.0) ? (s.value / laborForce * 100.0) : 0.0;
        if (s.value >= 10000.0)
            snprintf(buf, sizeof(buf), "%s: %.1f万 (%.1f%%)", s.label, s.value / 10000.0, pct);
        else
            snprintf(buf, sizeof(buf), "%s: %.0f (%.1f%%)", s.label, s.value, pct);

        DrawTextEx(font, buf, { legendX + 20.0f, legendY }, 18, 1, BLACK);
        legendY += 26.0f;
    }
}
