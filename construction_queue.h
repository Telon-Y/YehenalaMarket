#pragma once

#include "construction.h"

// The queue shown to the player and the scheduler must use the same order.
struct ConstructionQueueOrder {
    template <typename Project>
    bool operator()(const Project& left, const Project& right) const {
        if (left.priority != right.priority)
            return left.priority > right.priority;
        if (left.sequence != right.sequence)
            return left.sequence < right.sequence;
        return left.id < right.id;
    }
};

// Moves among live projects, including paused work. A boundary or a missing /
// terminal project is a no-op and returns false. No funding or work changes.
bool MoveConstructionQueueProject(CountryConstructionState& state,
                                  ConstructionProjectId projectId,
                                  bool up, bool toEdge = false);
