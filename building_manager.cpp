#include "building_manager.h"
#include <algorithm>
#include <cmath>

BuildingManager::BuildingManager() {
    templates = createBuildingTemplates();
    buildingCounts.fill(50);
    avgProfitRates.fill(0.0);
    smoothedProfitRate.fill(0.0);
    employmentRatio.fill(1.0);
    consecutiveLowEmpWeeks.fill(0);
    for (int t = 0; t < TYPE_COUNT; ++t)
        cashPools[t] = buildingCounts[t] * 500000.0;
    lastDemolishStep.fill(-9999);
    currentSupplyRatio.fill(1.0);
}

void BuildingManager::placeOrder(int typeIdx) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT) return;
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    if (pending >= 50) return;
    constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], false});
}

void BuildingManager::placePlayerOrder(int typeIdx, int count, bool top) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT) return;
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    int maxQueue = 50 - pending;
    if (maxQueue <= 0) return;
    if (count > maxQueue) count = maxQueue;
    for (int i = 0; i < count; ++i) {
        ConstructionOrder order{typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], true}; // 玩家建造忽略现金
        if (top) constructionQueue.insert(constructionQueue.begin(), order);
        else     constructionQueue.push_back(order);
    }
}

void BuildingManager::demolishBuildings(int typeIdx, int count, int stepCount) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || buildingCounts[typeIdx] <= 0) return;
    if (typeIdx == CONST_DEPT) return;
    if (stepCount - lastDemolishStep[typeIdx] < demolishCooldownPeriod) return;
    int actual = std::min(count, buildingCounts[typeIdx]);
    buildingCounts[typeIdx] -= actual;
    lastDemolishStep[typeIdx] = stepCount;
}

std::array<int, TYPE_COUNT> BuildingManager::getInQueueCounts() const {
    std::array<int, TYPE_COUNT> counts{};
    for (const auto& ord : constructionQueue)
        counts[ord.typeIndex]++;
    return counts;
}

void BuildingManager::aiBuild(double aiProfitThreshold,
                              const std::array<double, NUM_GOODS>& prices,
                              double averageWage) {
    auto inQueueCount = getInQueueCounts();
    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                     + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    bool farmCap = (totalFarms >= maxTotalFarms);
    bool coalCap = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
    bool ironCap = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
    bool constrCap = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);

    double constrCapacity = 0.0;
    if (buildingCounts[CONST_DEPT] > 0) {
        const auto& bt = templates[CONST_DEPT];
        constrCapacity = buildingCounts[CONST_DEPT] *
            bt.getProfitFactor(prices, averageWage) *
            employmentRatio[CONST_DEPT] * bt.outputRate;
    }
    double totalRemaining = 0.0;
    for (const auto& ord : constructionQueue) totalRemaining += ord.remainingCost;
    if (totalRemaining / (constrCapacity + 0.001) > 52.0 && inQueueCount[CONST_DEPT] == 0 && !constrCap) {
        constructionQueue.insert(constructionQueue.begin(),
            { CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT], false });
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (inQueueCount[t] >= 50) continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0) continue;
        if (t == COAL_MINE && coalCap) continue;
        if (t == IRON_MINE && ironCap) continue;
        if (t == CONST_DEPT && constrCap) continue;

        double smoothed = smoothedProfitRate[t];
        if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
            const auto& bt = templates[t];
            double estCost = bt.getUnitCost(prices, averageWage);
            double estProfit = (estCost > 1e-6) ? (prices[bt.outputGood] - estCost) / estCost : 0.0;
            if (estProfit > aiProfitThreshold) {
                if (t == FARM_GRAIN || t == COTTON) {
                    if (farmSlotsRemaining > 0) { placeOrder(t); farmSlotsRemaining--; }
                } else {
                    placeOrder(t);
                }
            }
            continue;
        }
        if (smoothed > aiProfitThreshold && buildingCounts[t] > 0) {
            int N_wanted = (int)ceil((smoothed - aiProfitThreshold) / 0.05);
            N_wanted = std::max(0, N_wanted);
            int N_remaining = N_wanted - inQueueCount[t];
            if (N_remaining <= 0) continue;
            if (t == FARM_GRAIN || t == COTTON) N_remaining = std::min(N_remaining, farmSlotsRemaining);
            else if (t == COAL_MINE) N_remaining = std::min(N_remaining, maxCoalMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == IRON_MINE) N_remaining = std::min(N_remaining, maxIronMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == CONST_DEPT) N_remaining = std::min(N_remaining, maxConstDept - (buildingCounts[t] + inQueueCount[t]));

            int maxQueue = 50 - inQueueCount[t];
            if (N_remaining > maxQueue) N_remaining = maxQueue;
            int maxExpand = std::max(1, (int)ceil(buildingCounts[t] * 0.1));
            int N_final = std::min(N_remaining, maxExpand);

            for (int j = 0; j < N_final; ++j) placeOrder(t);
            if (t == FARM_GRAIN || t == COTTON) farmSlotsRemaining -= N_final;
        }
    }
}

std::array<double, TYPE_COUNT> BuildingManager::calculateBaseOutputRates(double maxLabor) const {
    std::array<double, TYPE_COUNT> rates;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) { rates[t] = 0.0; continue; }
        const auto& bt = templates[t];
        rates[t] = bt.outputRate * employmentRatio[t];
    }
    double idealLabor = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t)
        idealLabor += buildingCounts[t] * templates[t].laborPerUnit * rates[t];
    double scale = (idealLabor > 1e-9) ? std::min(1.0, maxLabor / idealLabor) : 1.0;
    if (scale < 1.0) {
        for (int t = 0; t < TYPE_COUNT; ++t)
            rates[t] *= scale;
    }
    return rates;
}

double BuildingManager::processConstruction(double availableConstr, double constrPrice) {
    double soldConstr = 0.0;
    for (auto& ord : constructionQueue) {
        if (availableConstr <= 0) break;
        double invest = std::min({ availableConstr, 30.0, ord.remainingCost });
        if (invest <= 0) continue;
        if (!ord.ignoreCash)
            cashPools[ord.typeIndex] -= invest * constrPrice;
        soldConstr += invest;
        ord.remainingCost -= invest;
        availableConstr -= invest;
    }
    constructionQueue.erase(
        std::remove_if(constructionQueue.begin(), constructionQueue.end(),
            [&](ConstructionOrder& o) {
                if (o.remainingCost <= 0) {
                    buildingCounts[o.typeIndex]++;
                    return true;
                }
                return false;
            }),
        constructionQueue.end());
    return soldConstr;
}

void BuildingManager::updateProfitRates(const std::array<double, NUM_GOODS>& prices, double averageWage) {
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) {
            avgProfitRates[t] = 0.0;
            smoothedProfitRate[t] = 0.0;   // 重置平滑值
            continue;
        }
        const auto& bt = templates[t];
        double unitCost = bt.laborPerUnit * averageWage;
        for (int g = 0; g < NUM_GOODS; ++g) unitCost += prices[g] * bt.inputs[g];
        avgProfitRates[t] = (unitCost > 1e-6) ? (prices[bt.outputGood] - unitCost) / unitCost : 0.0;
    }
    if (!profitInitialized) {
        smoothedProfitRate = avgProfitRates;
        profitInitialized = true;
    } else {
        double alpha = 0.2;
        for (int t = 0; t < TYPE_COUNT; ++t)
            smoothedProfitRate[t] = smoothedProfitRate[t] * (1.0 - alpha) + avgProfitRates[t] * alpha;
    }
}

void BuildingManager::adjustEmployment() {
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        if (smoothedProfitRate[t] < 0.0) {
            employmentRatio[t] *= 0.80;
        } else {
            employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.30;
        }
        employmentRatio[t] = std::clamp(employmentRatio[t], 0.0, 1.0);
    }
}

void BuildingManager::checkDecay(int stepCount) {
    if (stepCount <= 52) {
        resetDecayCounters();
        return;
    }
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == CONST_DEPT) {
            consecutiveLowEmpWeeks[t] = 0;
            continue;
        }
        if (buildingCounts[t] == 0) {
            consecutiveLowEmpWeeks[t] = 0;
            continue;
        }
        if (employmentRatio[t] <= 0.75) {
            consecutiveLowEmpWeeks[t]++;
            if (consecutiveLowEmpWeeks[t] >= 156) {
                int reduce = std::max(1, (int)ceil(buildingCounts[t] * 0.05));
                buildingCounts[t] = std::max(0, buildingCounts[t] - reduce); // 允许拆至0
            }
        } else {
            consecutiveLowEmpWeeks[t] = 0;
        }
    }
}

void BuildingManager::resetDecayCounters() {
    consecutiveLowEmpWeeks.fill(0);
}