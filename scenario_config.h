#pragma once

namespace scenario {

// The map combines post-1867 Habsburg borders with later colonial relations
// and an alternate Prussian central-African subject. Keep that choice explicit
// instead of implying that all relationships belong to one real-world year.
inline constexpr int kScenarioYear = 1880;
inline constexpr bool kAlternateHistory = true;
inline constexpr const char* kScenarioKey =
    "1880_alternate_colonial_relations";

}  // namespace scenario
