#pragma once

#include <string>
#include <vector>

// Strings the UI can display that are NOT compile-time literals, and therefore
// cannot be discovered by the build-time catalog scanner: the commodity and
// building names from constants.h, and every continent, region, country, and
// province name held by WorldData.
//
// main.cpp appends these to BuildUiTextCatalog() before loading the font, and
// tests/ui_text_catalog_tests.cpp asserts the same union is covered by the
// bundled font. Keeping one implementation means the checked set and the
// rendered set cannot drift apart.
std::vector<std::string> BuildRuntimeUiTexts();
