#include "ui_text_runtime.h"

#include "constants.h"
#include "world_data.h"

std::vector<std::string> BuildRuntimeUiTexts() {
    std::vector<std::string> texts;
    texts.reserve(commodityNames.size() + buildingTypeNames.size() + 256);
    texts.insert(texts.end(), commodityNames.begin(), commodityNames.end());
    texts.insert(texts.end(), buildingTypeNames.begin(),
                 buildingTypeNames.end());
    for (const auto& item : WorldData::continents())
        texts.emplace_back(item.name);
    for (const auto& item : WorldData::regions())
        texts.emplace_back(item.name);
    for (const auto& item : WorldData::countries())
        texts.emplace_back(item.name);
    for (const auto& item : WorldData::provinces())
        texts.emplace_back(item.name);
    return texts;
}
