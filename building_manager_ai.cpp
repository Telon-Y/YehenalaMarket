// ==================== building_manager_ai.cpp ====================
// AI 扩建逻辑与基础产出率计算
#include "building_manager.h"
#include <algorithm>
#include <cmath>
#include <numeric>

void BuildingManager::aiBuild(double aiProfitThreshold,
                              const std::array<Money, NUM_GOODS>& prices,
                              const std::array<Money, TYPE_COUNT>& wages,
                              double availableLabor,
                              const std::array<double, TYPE_COUNT>& actualEmploymentRate,
                              OwnerType automaticOwner,
                              Money* investmentPool) {
    auto inQueueCount = getInQueueCounts();

    double totalLaborDemand = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        totalLaborDemand += (buildingCounts[t] + inQueueCount[t]) * templates[t].laborPerUnit;
    }
    double remainingLabor = std::max(0.0, availableLabor - totalLaborDemand);

    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                     + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    bool constrCap = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);

    Money constrCapacity = Money(0);
    if (buildingCounts[CONST_DEPT] > 0) {
        const auto& bt = templates[CONST_DEPT];
        constrCapacity = Money(buildingCounts[CONST_DEPT]) *
            bt.getProfitFactor(prices, wages[CONST_DEPT]) *
            Money(employmentRatio[CONST_DEPT] * bt.outputRate);
    }
    Money totalRemaining = Money(0);
    for (const auto& ord : constructionQueue) totalRemaining += ord.remainingCost;

    constexpr size_t maxQueueSize = 200;
    if (constructionQueue.size() >= maxQueueSize) return;
    int queueSlots = static_cast<int>(maxQueueSize - constructionQueue.size());

    // ===== 自动扩建建造部门（紧急情况） =====
    if (queueSlots > 0 && allowAutoConstExpansion &&
        totalRemaining / (constrCapacity + Money(0.001)) > Money(52.0) &&
        inQueueCount[CONST_DEPT] == 0 && !constrCap) {
        constructionQueue.insert(constructionQueue.begin(),
            { CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT],
              OWNER_GOVERNMENT, {}, Money(0) });
        ++inQueueCount[CONST_DEPT];
        --queueSlots;
    }

    // Existing productive buildings first form a potential expansion queue.
    // The queue is ordered by realized profit per building, then submitted
    // one request at a time so each request can pay its startup capital.
    std::vector<std::pair<int, int>> expansions;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (templates[t].isDevelopment() || templates[t].isFinancial ||
            buildingCounts[t] <= 0 || inQueueCount[t] >= 50 ||
            !std::isfinite(actualEmploymentRate[t]) ||
            actualEmploymentRate[t] <= 0.90 ||
            !std::isfinite(avgProfitRates[t]) || avgProfitRates[t] <= 0.0)
            continue;

        const auto& bt = templates[t];
        if (bt.outputGood < 0) continue;
        const Money unitCost = bt.getUnitCost(prices, wages[t]);
        if (!isfinite(unitCost) || unitCost <= Money(1e-6)) continue;
        const double expectedRate =
            ((prices[bt.outputGood] - unitCost) / unitCost).toDouble();
        if (!std::isfinite(expectedRate) || expectedRate <= 0.0) continue;

        const int maxExpand = std::max(
            1, static_cast<int>(std::ceil(buildingCounts[t] * 0.1)));
        const double signal = std::max(0.0, smoothedProfitRate[t]);
        int wanted = std::max(1, static_cast<int>(std::ceil(signal / 0.05)));
        wanted = std::min(wanted, maxExpand) - inQueueCount[t];
        if (wanted <= 0) continue;
        if (t == FARM_GRAIN || t == COTTON)
            wanted = std::min(wanted, farmSlotsRemaining);
        else if (t == COAL_MINE)
            wanted = std::min(wanted, maxCoalMines - buildingCounts[t] - inQueueCount[t]);
        else if (t == IRON_MINE)
            wanted = std::min(wanted, maxIronMines - buildingCounts[t] - inQueueCount[t]);
        else if (t == GOLD_MINE)
            wanted = std::min(wanted, maxGoldMines - buildingCounts[t] - inQueueCount[t]);
        if (wanted > 0) expansions.push_back({t, wanted});
    }
    std::sort(expansions.begin(), expansions.end(),
              [this](const auto& left, const auto& right) {
        const Money leftProfit = actualUnitProfits[left.first];
        const Money rightProfit = actualUnitProfits[right.first];
        if (leftProfit != rightProfit) return leftProfit > rightProfit;
        return left.first < right.first;
    });

    for (const auto& expansion : expansions) {
        const int t = expansion.first;
        const Money startup = expansionStartupCapital(t);
        for (int count = 0; count < expansion.second && queueSlots > 0; ++count) {
            if (investmentPool != nullptr &&
                (!isfinite(*investmentPool) || *investmentPool < startup))
                break;
            const int before = inQueueCount[t];
            placeOrder(t, automaticOwner);
            inQueueCount = getInQueueCounts();
            if (inQueueCount[t] <= before) break;
            if (investmentPool != nullptr && startup > Money(0)) {
                *investmentPool -= startup;
                *investmentPool = std::max(Money(0), *investmentPool);
                syncBankLevels(*investmentPool);
            }
            --queueSlots;
            if (t == FARM_GRAIN || t == COTTON) --farmSlotsRemaining;
        }
    }

    // New production buildings retain the existing estimated-profit path.
    for (int t = 0; t < TYPE_COUNT && queueSlots > 0; ++t) {
        if (templates[t].isDevelopment() || templates[t].isFinancial ||
            buildingCounts[t] != 0 || inQueueCount[t] != 0)
            continue;
        const auto& bt = templates[t];
        if (bt.outputGood < 0) continue;
        const Money estCost = bt.getUnitCost(prices, wages[t]);
        if (!isfinite(estCost) || estCost <= Money(1e-6)) continue;
        const Money estProfit = (prices[bt.outputGood] - estCost) / estCost;
        if (estProfit <= Money(aiProfitThreshold) ||
            bt.laborPerUnit > remainingLabor + 1e-9)
            continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0)
            continue;
        if (t == GOLD_MINE && buildingCounts[t] + inQueueCount[t] >= maxGoldMines)
            continue;
        placeOrder(t, automaticOwner);
        inQueueCount = getInQueueCounts();
        if (inQueueCount[t] <= 0) continue;
        remainingLabor -= bt.laborPerUnit;
        if (t == FARM_GRAIN || t == COTTON) --farmSlotsRemaining;
        --queueSlots;
    }
}

std::array<double, TYPE_COUNT> BuildingManager::calculateBaseOutputRates(double maxLabor) const {
    std::array<double, TYPE_COUNT> rates;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) {
            rates[t] = 0.0;
            continue;
        }
        const auto& bt = templates[t];
        if (bt.isFinancial) {
            rates[t] = 1.0;
        } else {
            rates[t] = bt.outputRate * employmentRatio[t];
        }
    }

    double idealLabor = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        const auto& bt = templates[t];
        if (bt.isDevelopment()) {
            idealLabor += buildingCounts[t] * bt.laborPerUnit * 1.0;
        } else {
            idealLabor += buildingCounts[t] * bt.laborPerUnit * employmentRatio[t];
        }
    }

    double developmentLabor = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] > 0 && templates[t].isDevelopment())
            developmentLabor += buildingCounts[t] * templates[t].laborPerUnit;
    }
    double ordinaryLabor = std::max(0.0, idealLabor - developmentLabor);
    double availableOrdinaryLabor = std::max(0.0, maxLabor - developmentLabor);
    double scale = (ordinaryLabor > 1e-9)
        ? std::min(1.0, availableOrdinaryLabor / ordinaryLabor) : 1.0;
    if (scale < 1.0) {
        for (int t = 0; t < TYPE_COUNT; ++t)
            if (!templates[t].isDevelopment()) rates[t] *= scale;
    }
    return rates;
}


std::vector<AIExpansionCandidate>
BuildingManager::collectAIExpansionCandidates(
    double aiProfitThreshold,
    const std::array<Money, NUM_GOODS>& prices,
    const std::array<Money, TYPE_COUNT>& wages,
    double availableLabor,
    const std::array<double, TYPE_COUNT>& actualEmploymentRate) const {
    std::vector<AIExpansionCandidate> result;
    const auto inQueueCount = getInQueueCounts();

    double totalLaborDemand = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        totalLaborDemand +=
            (buildingCounts[t] + inQueueCount[t]) * templates[t].laborPerUnit;
    }
    double remainingLabor = std::max(0.0, availableLabor - totalLaborDemand);

    const int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON] +
                           inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    const bool coalCap =
        buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines;
    const bool ironCap =
        buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines;
    const bool goldCap =
        buildingCounts[GOLD_MINE] + inQueueCount[GOLD_MINE] >= maxGoldMines;
    const bool constrCap =
        buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept;

    Money constructionCapacity = Money(0);
    if (buildingCounts[CONST_DEPT] > 0) {
        const auto& construction = templates[CONST_DEPT];
        constructionCapacity =
            Money(buildingCounts[CONST_DEPT]) *
            construction.getProfitFactor(prices, wages[CONST_DEPT]) *
            Money(employmentRatio[CONST_DEPT] * construction.outputRate);
    }

    Money totalRemaining = Money(0);
    for (const auto& order : constructionQueue)
        totalRemaining += order.remainingCost;

    const std::size_t maxQueueSize = 200;
    if (constructionQueue.size() >= maxQueueSize) return result;
    int queueSlots =
        static_cast<int>(maxQueueSize - constructionQueue.size());

    auto addCandidate = [&result](int typeIndex, int maxUnits,
                                   double priority) {
        if (typeIndex < 0 || maxUnits <= 0 || !std::isfinite(priority))
            return;
        result.push_back({typeIndex, maxUnits, priority,
                          buildingCost[typeIndex]});
    };

    if (allowAutoConstExpansion && queueSlots > 0 &&
        !constrCap && inQueueCount[CONST_DEPT] == 0 &&
        totalRemaining / (constructionCapacity + Money(0.001)) > Money(52.0)) {
        const double overload =
            (totalRemaining / (constructionCapacity + Money(0.001))).toDouble();
        addCandidate(CONST_DEPT, 1, 10.0 + std::min(10.0, overload / 52.0));
        --queueSlots;
    }

    for (int t = 0; t < TYPE_COUNT && queueSlots > 0; ++t) {
        if (templates[t].isDevelopment() || templates[t].isFinancial) continue;
        if (inQueueCount[t] >= 50) continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0)
            continue;
        if (t == COAL_MINE && coalCap) continue;
        if (t == IRON_MINE && ironCap) continue;
        if (t == GOLD_MINE && goldCap) continue;

        int wanted = 0;
        double priority = 0.0;
        if (buildingCounts[t] > 0) {
            const double current = avgProfitRates[t];
            if (!std::isfinite(actualEmploymentRate[t]) ||
                actualEmploymentRate[t] <= 0.90 ||
                !std::isfinite(current) || current <= 0.0)
                continue;
            const auto& building = templates[t];
            if (building.outputGood < 0) continue;
            const Money expectedCost = building.getUnitCost(prices, wages[t]);
            if (!isfinite(expectedCost) || expectedCost <= Money(1e-6))
                continue;
            const double expectedRate =
                ((prices[building.outputGood] - expectedCost) /
                 expectedCost).toDouble();
            if (!std::isfinite(expectedRate) || expectedRate <= 0.0)
                continue;
            const double smoothed = std::max(0.0, smoothedProfitRate[t]);
            wanted = std::max(1, static_cast<int>(std::ceil(smoothed / 0.05)));
            const int maxExpand = std::max(
                1, static_cast<int>(std::ceil(buildingCounts[t] * 0.1)));
            wanted = std::min(wanted, maxExpand) - inQueueCount[t];
            priority = actualUnitProfits[t].toDouble();
        } else {
            const auto& building = templates[t];
            if (building.outputGood < 0) continue;
            const Money estimatedCost =
                building.getUnitCost(prices, wages[t]);
            const Money estimatedProfit =
                prices[building.outputGood] - estimatedCost;
            const double estimatedRate =
                estimatedCost > Money(1e-6)
                    ? (estimatedProfit / estimatedCost).toDouble()
                    : 0.0;
            if (estimatedRate <= aiProfitThreshold) continue;
            wanted = 1;
            priority = estimatedRate - aiProfitThreshold;
        }

        wanted = std::min(wanted, 50 - inQueueCount[t]);
        if (t == FARM_GRAIN || t == COTTON)
            wanted = std::min(wanted, farmSlotsRemaining);
        else if (t == COAL_MINE)
            wanted = std::min(wanted, maxCoalMines -
                                       buildingCounts[t] - inQueueCount[t]);
        else if (t == IRON_MINE)
            wanted = std::min(wanted, maxIronMines -
                                       buildingCounts[t] - inQueueCount[t]);
        else if (t == GOLD_MINE)
            wanted = std::min(wanted, maxGoldMines -
                                       buildingCounts[t] - inQueueCount[t]);

        const double laborPerUnit = templates[t].laborPerUnit;
        if (actualEmploymentRate[t] < 0.90 && laborPerUnit > 1e-6) {
            wanted = std::min(
                wanted, static_cast<int>(remainingLabor / laborPerUnit));
        }
        wanted = std::min(wanted, queueSlots);
        if (wanted <= 0) continue;

        addCandidate(t, wanted, priority);
        remainingLabor -= wanted * laborPerUnit;
        queueSlots -= wanted;
        if (t == FARM_GRAIN || t == COTTON)
            farmSlotsRemaining -= wanted;
    }
    const auto isProductionExpansion = [this](
        const AIExpansionCandidate& candidate) {
        return candidate.typeIndex >= 0 && candidate.typeIndex < TYPE_COUNT &&
               buildingCounts[candidate.typeIndex] > 0 &&
               !templates[candidate.typeIndex].isDevelopment() &&
               !templates[candidate.typeIndex].isFinancial;
    };
    std::sort(result.begin(), result.end(),
              [&isProductionExpansion](const AIExpansionCandidate& left,
                                       const AIExpansionCandidate& right) {
        const bool leftExpansion = isProductionExpansion(left);
        const bool rightExpansion = isProductionExpansion(right);
        if (leftExpansion != rightExpansion)
            return leftExpansion > rightExpansion;
        if (left.priority != right.priority)
            return left.priority > right.priority;
        return left.typeIndex < right.typeIndex;
    });
    return result;
}
