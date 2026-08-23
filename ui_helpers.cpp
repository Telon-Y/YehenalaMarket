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
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 16, 0, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 18 }, 16, 0, BLACK);
    if (label) DrawTextEx(font, label, { chartX + 2, chartY - 24 }, 18, 0, DARKGRAY);

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
    DrawTextEx(font, maxBuf, { chartX + 2, chartY }, 16, 0, BLACK);
    DrawTextEx(font, minBuf, { chartX + 2, chartY + chartH - 18 }, 16, 0, BLACK);
    if (label) DrawTextEx(font, label, { chartX + 2, chartY - 24 }, 18, 0, DARKGRAY);

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
