// ==================== building_manager_construction.cpp ====================
// 建造队列：下订单、每周结算
#include "building_manager.h"
#include <algorithm>
#include <cmath>
#include <numeric>

void BuildingManager::placeOrder(int typeIdx, OwnerType owner) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT) return;
    if (templates[typeIdx].isFinancial) return;
    if (typeIdx == CONST_DEPT) owner = OWNER_GOVERNMENT;
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    if (pending >= 50) return;
    auto queued = getInQueueCounts();
    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON] +
                     queued[FARM_GRAIN] + queued[COTTON];
    if ((typeIdx == FARM_GRAIN || typeIdx == COTTON) && totalFarms >= maxTotalFarms) return;
    if (typeIdx == COAL_MINE && buildingCounts[typeIdx] + queued[typeIdx] >= maxCoalMines) return;
    if (typeIdx == IRON_MINE && buildingCounts[typeIdx] + queued[typeIdx] >= maxIronMines) return;
    if (typeIdx == GOLD_MINE && buildingCounts[typeIdx] + queued[typeIdx] >= maxGoldMines) return;
    if (typeIdx == CONST_DEPT && buildingCounts[typeIdx] + queued[typeIdx] >= maxConstDept) return;
    constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], false, owner});
}

void BuildingManager::placePlayerOrder(int typeIdx, int count, bool top) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT) return;
    if (templates[typeIdx].isFinancial) return;

    // ===== 资源上限检查（与 AI 一致） =====
    auto inQueue = getInQueueCounts();
    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON] +
                     inQueue[FARM_GRAIN] + inQueue[COTTON];
    bool farmCap = (totalFarms >= maxTotalFarms);
    bool coalCap = (buildingCounts[COAL_MINE] + inQueue[COAL_MINE] >= maxCoalMines);
    bool ironCap = (buildingCounts[IRON_MINE] + inQueue[IRON_MINE] >= maxIronMines);
    bool goldCap = (buildingCounts[GOLD_MINE] + inQueue[GOLD_MINE] >= maxGoldMines);
    bool constrCap = (buildingCounts[CONST_DEPT] + inQueue[CONST_DEPT] >= maxConstDept);

    switch (typeIdx) {
        case FARM_GRAIN: case COTTON: if (farmCap) return; break;
        case COAL_MINE: if (coalCap) return; break;
        case IRON_MINE: if (ironCap) return; break;
        case GOLD_MINE: if (goldCap) return; break;
        case CONST_DEPT: if (constrCap) return; break;
        default: break;
    }

    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    if (pending >= 50) return;

    int capacity = 50 - pending;
    switch (typeIdx) {
        case FARM_GRAIN: case COTTON: capacity = std::min(capacity, maxTotalFarms - totalFarms); break;
        case COAL_MINE: capacity = std::min(capacity, maxCoalMines - buildingCounts[typeIdx] - inQueue[typeIdx]); break;
        case IRON_MINE: capacity = std::min(capacity, maxIronMines - buildingCounts[typeIdx] - inQueue[typeIdx]); break;
        case GOLD_MINE: capacity = std::min(capacity, maxGoldMines - buildingCounts[typeIdx] - inQueue[typeIdx]); break;
        case CONST_DEPT: capacity = std::min(capacity, maxConstDept - buildingCounts[typeIdx] - inQueue[typeIdx]); break;
        default: break;
    }
    count = std::clamp(count, 0, capacity);
    for (int i = 0; i < count; ++i) {
        ConstructionOrder order{
            typeIdx, buildingCost[typeIdx], buildingCost[typeIdx],
            true,               // ignoreCash = true (保留，但支付已统一)
            OWNER_GOVERNMENT
        };
        if (top) constructionQueue.insert(constructionQueue.begin(), order);
        else     constructionQueue.push_back(order);
    }
}

std::array<int, TYPE_COUNT> BuildingManager::getInQueueCounts() const {
    std::array<int, TYPE_COUNT> counts{};
    for (const auto& ord : constructionQueue)
        counts[ord.typeIndex]++;
    return counts;
}

Money BuildingManager::getWeeklyPrivateConstructionDemand(Money availableConstr) const {
    Money privateDemand = Money(0);
    for (const auto& ord : constructionQueue) {
        if (availableConstr <= Money(0)) break;
        Money invest = std::min({availableConstr, Money(30), ord.remainingCost});
        availableConstr -= invest;
        if (ord.owner != OWNER_GOVERNMENT) privateDemand += invest;
    }
    return privateDemand;
}

ConstructionSettlement BuildingManager::processConstruction(
    Money availableConstr, Money constrPrice,
    Money& investmentPool, Money& governmentCash) {
    ConstructionSettlement settlement;

    for (auto& ord : constructionQueue) {
        if (availableConstr <= Money(0)) break;

        Money invest = std::min({availableConstr, Money(30), ord.remainingCost});
        if (ord.owner != OWNER_GOVERNMENT) {
            Money spendableCash = std::max(Money(0), investmentPool);
            if (constrPrice <= Money(0) || spendableCash <= Money(0)) continue;
            invest = std::min(invest, spendableCash / constrPrice);
            if (invest <= Money(0)) continue;

            Money payment = invest * constrPrice;
            investmentPool -= payment;
            governmentCash += payment;
            settlement.privatePayment += payment;
        }

        ord.remainingCost -= invest;
        availableConstr -= invest;
        settlement.totalUsed += invest;
    }

    // 移除已完成订单
    constructionQueue.erase(
        std::remove_if(constructionQueue.begin(), constructionQueue.end(),
            [&](ConstructionOrder& o) {
                if (o.remainingCost <= Money(0)) {
                    buildingCounts[o.typeIndex]++;
                    ownedBuildings[o.typeIndex][o.owner]++;
                    if (onBuildingCompleted)
                        onBuildingCompleted(o.typeIndex, 1, o.owner);
                    syncFinanceCount();
                    return true;
                }
                return false;
            }),
        constructionQueue.end());

    return settlement;
}
