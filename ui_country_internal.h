#pragma once

#include "ui_internal.h"

#include <string>

namespace ui_country {

const std::string& CountryCode(const CountrySnapshot& country);
void DrawFlag(const CountrySnapshot& country, Rectangle flag);

}  // namespace ui_country
