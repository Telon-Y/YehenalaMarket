// ==================== ui_internal.h ====================
// UI 内部共享声明（非公共接口）
#pragma once
#include "ui.h"

// ===== 按钮尺寸常量 =====
constexpr float BTN_W = 30.0f;
constexpr float BTN_H = 20.0f;
constexpr float BTN_GAP = 4.0f;

// ===== 格式化 =====
void FormatCash(double cash, char* buf, size_t bufSize);
void FormatCash(const Money& cash, char* buf, size_t bufSize);

// ===== 图表边界计算 =====
void ComputeBounds(double minV, double maxV, bool nonNegative,
                   double& outMin, double& outMax);

// ===== 图表绘制辅助 =====
void DrawPriceCurve(const std::vector<std::array<Money, NUM_GOODS>>& hist,
                    int goodIdx, float chartX, float chartY, float chartW, float chartH,
                    Color color, Font font, int startRow = -1);

void DrawScalarCurve(const std::vector<Money>& data,
                     float chartX, float chartY, float chartW, float chartH,
                     Color color, Font font, const char* label);

void DrawScalarCurveDouble(const std::vector<double>& data,
                           float chartX, float chartY, float chartW, float chartH,
                           Color color, Font font, const char* label);

void DrawMultiPriceCurve(const std::vector<std::array<Money, NUM_GOODS>>& hist,
                         const std::vector<int>& goodIndices,
                         const std::vector<Color>& colors,
                         Font font, Rectangle chartRect);

int EstimateWeeksLeft(const LocalMarket& market, size_t orderIndex);

// ===== 劳动力比例饼图（不含受抚养人口） =====
void DrawLaborPieChart(const LocalMarket& market, float chartX, float chartY,
                       float radius, Font font);

// ===== UI 状态初始化与输入 =====
void InitUIState(UIState* state);
void HandleInput(UIState* state, World& world);

// ===== 主绘制 =====
void DrawUI(const UIState* state, World& world, Font font, double elapsedSeconds);