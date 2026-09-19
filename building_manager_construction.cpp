// ==================== building_manager_construction.cpp ====================
// Completed-building installation owned by the unified construction service.
#include "building_manager.h"

void BuildingManager::addCompletedBuildings(int typeIdx, int count,
                                             OwnerType owner) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || count <= 0 ||
        owner < 0 || owner >= OWNER_COUNT || templates[typeIdx].isFinancial)
        return;

    buildingCounts[typeIdx] += count;
    ownedBuildings[typeIdx][owner] += count;
    if (onBuildingCompleted)
        onBuildingCompleted(typeIdx, count, owner);
}
