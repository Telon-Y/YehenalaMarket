#pragma once

// Shared compact number formatting for all user-facing UI.  Values are
// rendered with Chinese large-number units so that debug and gameplay views
// never expose the implementation-oriented k/m/b/t/p/e suffixes.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>

inline std::string FormatChineseNumber(double value, int precision = 1,
                                       const char* invalid = "无数据") {
    if (!std::isfinite(value)) return invalid ? std::string(invalid) : std::string();

    // Keep enough precision for prices and percentages while avoiding
    // accidental very long labels from an out-of-range caller.
    precision = std::max(0, std::min(precision, 6));
    const double magnitude = std::fabs(value);
    struct Unit { double scale; const char* suffix; };
    // Chinese units used by the game economy.  万亿 corresponds to 10^12;
    // 京/垓 cover very large debug values without falling back to e notation.
    constexpr Unit units[] = {
        {1e20, "垓"}, {1e16, "京"}, {1e12, "万亿"},
        {1e8, "亿"}, {1e4, "万"}, {1.0, ""}
    };
    const Unit* selected = &units[sizeof(units) / sizeof(units[0]) - 1];
    for (const Unit& unit : units) {
        if (magnitude >= unit.scale) { selected = &unit; break; }
    }

    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%.*f%s", precision,
                  value / selected->scale, selected->suffix);
    std::string result(buffer);

    // Compact labels read better without an insignificant decimal tail
    // (for example, 12.0万 becomes 12万).  Preserve a meaningful fractional
    // part when one exists.
    const std::size_t suffixPos = result.find_first_of("万亿京垓");
    const std::size_t end = suffixPos == std::string::npos ? result.size() : suffixPos;
    const std::size_t dot = result.find('.', 0);
    if (dot != std::string::npos && dot < end) {
        std::size_t trim = end;
        while (trim > dot + 1 && result[trim - 1] == '0') --trim;
        if (trim == dot + 1) --trim; // remove the decimal point as well
        if (trim < end) result.erase(trim, end - trim);
    }
    return result;
}
