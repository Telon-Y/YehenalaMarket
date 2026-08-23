#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

template <typename Value, std::size_t Window>
class RecentAverageFilter {
    static_assert(Window > 0, "RecentAverageFilter needs a non-zero window");

public:
    Value update(Value sample, double alpha,
                 bool includeEmptyWindow = false) {
        using std::isfinite;
        if (!isfinite(sample) || sample < Value(0)) sample = Value(0);

        if (sampleCount < Window) {
            samples[cursor] = sample;
            runningSum += sample;
            ++sampleCount;
        } else {
            runningSum -= samples[cursor];
            samples[cursor] = sample;
            runningSum += sample;
        }
        cursor = (cursor + 1) % Window;

        const std::size_t divisor = includeEmptyWindow
            ? Window : std::max<std::size_t>(1, sampleCount);
        recentAverage = runningSum / Value(static_cast<int>(divisor));
        const double gain = std::clamp(alpha, 0.0, 1.0);
        if (!initialized) {
            filteredValue = recentAverage;
            initialized = true;
        } else {
            filteredValue +=
                (recentAverage - filteredValue) * Value(gain);
        }
        if (!isfinite(filteredValue) || filteredValue < Value(0))
            filteredValue = Value(0);
        return filteredValue;
    }

    void seed(Value value) {
        using std::isfinite;
        if (!isfinite(value) || value < Value(0)) value = Value(0);
        samples.fill(value);
        runningSum = value * Value(static_cast<int>(Window));
        recentAverage = value;
        filteredValue = value;
        cursor = 0;
        sampleCount = Window;
        initialized = true;
    }

    Value average() const { return recentAverage; }
    Value value() const { return filteredValue; }
    std::size_t count() const { return sampleCount; }

private:
    std::array<Value, Window> samples{};
    Value runningSum = Value(0);
    Value recentAverage = Value(0);
    Value filteredValue = Value(0);
    std::size_t cursor = 0;
    std::size_t sampleCount = 0;
    bool initialized = false;
};
