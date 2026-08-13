#pragma once

#include <string>
#include <vector>

std::vector<int> collectFontCodepoints(
    const char* fontPath,
    const std::vector<std::string>& requiredTexts = {});
