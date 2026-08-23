// ==================== ui_internal.h ====================
#pragma once
#include "ui.h"
#include "ui_layout.h"

// Shared button dimensions
constexpr float BTN_W = 30.0f;
constexpr float BTN_H = 20.0f;
constexpr float BTN_GAP = 4.0f;

// Cash formatting
void FormatCash(double cash, char* buf, size_t bufSize);
void FormatCash(const Money& cash, char* buf, size_t bufSize);

// Chart bounds
void ComputeBounds(double minV, double maxV, bool nonNegative,
                   double& outMin, double& outMax);

void DrawScalarCurve(const std::vector<Money>& data,
                     float chartX, float chartY, float chartW, float chartH,
                     Color color, Font font, const char* label);

void DrawScalarCurveDouble(const std::vector<double>& data,
                           float chartX, float chartY, float chartW, float chartH,
                           Color color, Font font, const char* label);
void InitUIState(UIState* state);
void OpenBuildingDetail(UIState* state, int provinceId, int typeIndex);
void HandleInput(UIState* state, World& world);
void HandleProvinceDetailInput(UIState* state, World& world);
void HandleWorldMapInput(UIState* state, World& world);

bool HandleConstructionListInput(UIState* state, World& world);

// Top-level drawing
void DrawUI(UIState* state, World& world, Font font, double elapsedSeconds);
void DrawProvinceDetailUI(const UIState* state, World& world, Font font,
                          double elapsedSeconds);
void DrawWorldMapUI(const UIState* state, World& world, Font font,
                    double elapsedSeconds);
void DrawConstructionListUI(const UIState* state, World& world, Font font);
void HandleCountryOverviewInput(UIState* state, World& world);
void DrawCountryOverviewUI(const UIState* state, World& world, Font font,
                           double elapsedSeconds);
void HandleTransportInput(UIState* state, World& world);
void DrawTransportUI(const UIState* state, World& world, Font font);
