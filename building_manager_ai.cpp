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
                              const std::array<double, TYPE_COUNT>& actualEmploymentRate) {
    auto inQueueCount = getInQueueCounts();

    double totalLaborDemand = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        totalLaborDemand += (buildingCounts[t] + inQueueCount[t]) * templates[t].laborPerUnit;
    }
    double remainingLabor = std::max(0.0, availableLabor - totalLaborDemand);

    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                     + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    bool coalCap = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
    bool ironCap = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
    bool goldCap = (buildingCounts[GOLD_MINE] + inQueueCount[GOLD_MINE] >= maxGoldMines);
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
            { CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT], false, OWNER_GOVERNMENT });
        ++inQueueCount[CONST_DEPT];
        --queueSlots;
    }

    // ===== 全局队列限制：防止建造请求无限堆积 =====
    for (int t = 0; t < TYPE_COUNT; ++t) {
        // 跳过所有金融建筑：央行、金融区、工商银行、储蓄银行
        if (templates[t].isDevelopment()) continue;
        if (inQueueCount[t] >= 50) continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0) continue;
        if (t == COAL_MINE && coalCap) continue;
        if (t == IRON_MINE && ironCap) continue;
        if (t == GOLD_MINE && goldCap) continue;

        double smoothed = smoothedProfitRate[t];
        // 对于已有建筑：要求平滑利润率和当前利润率都高于阈值
        if (buildingCounts[t] > 0) {
            if (smoothed <= aiProfitThreshold || avgProfitRates[t] <= aiProfitThreshold)
                continue;

            int N_wanted = (int)ceil((smoothed - aiProfitThreshold) / 0.05);
            N_wanted = std::max(0, N_wanted);
            int maxExpand = std::max(1, (int)ceil(buildingCounts[t] * 0.1));
            int N_remaining = std::min(N_wanted, maxExpand) - inQueueCount[t];
            if (N_remaining <= 0) continue;

            if (t == FARM_GRAIN || t == COTTON) N_remaining = std::min(N_remaining, farmSlotsRemaining);
            else if (t == COAL_MINE) N_remaining = std::min(N_remaining, maxCoalMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == IRON_MINE) N_remaining = std::min(N_remaining, maxIronMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == GOLD_MINE) N_remaining = std::min(N_remaining, maxGoldMines - (buildingCounts[t] + inQueueCount[t]));

            int maxQueue = 50 - inQueueCount[t];
            if (N_remaining > maxQueue) N_remaining = maxQueue;
            int N_final = N_remaining;

            double laborPerUnit = templates[t].laborPerUnit;
            bool highEmployment = actualEmploymentRate[t] >= 0.90;
            if (!highEmployment) {
                int maxAllowedByLabor = (laborPerUnit > 1e-6) ? (int)(remainingLabor / laborPerUnit) : 0;
                N_final = std::min(N_final, maxAllowedByLabor);
            }

            N_final = std::min(N_final, queueSlots);
            for (int j = 0; j < N_final; ++j) placeOrder(t, OWNER_FINANCE);
            remainingLabor -= N_final * laborPerUnit;
            queueSlots -= N_final;
            if (t == FARM_GRAIN || t == COTTON) farmSlotsRemaining -= N_final;
        }
        else if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
            // 新建筑：使用估算利润率
            const auto& bt = templates[t];
            Money estCost = bt.getUnitCost(prices, wages[t]);
            Money estProfit = (estCost > Money(1e-6)) ? (prices[bt.outputGood] - estCost) / estCost : Money(0);
            if (estProfit > Money(aiProfitThreshold)) {
                if (queueSlots <= 0) continue;
                if (bt.laborPerUnit > remainingLabor + 1e-9) continue;
                if (t == FARM_GRAIN || t == COTTON) {
                    if (farmSlotsRemaining > 0) {
                        placeOrder(t, OWNER_FINANCE);
                        remainingLabor -= bt.laborPerUnit;
                        farmSlotsRemaining--;
                        --queueSlots;
                    }
                } else if (t == GOLD_MINE) {
                    if (buildingCounts[GOLD_MINE] + inQueueCount[GOLD_MINE] < maxGoldMines) {
                        placeOrder(t, OWNER_FINANCE);
                        remainingLabor -= bt.laborPerUnit;
                        --queueSlots;
                    }
                } else {
                    placeOrder(t, OWNER_FINANCE);
                    remainingLabor -= bt.laborPerUnit;
                    --queueSlots;
                }
            }
        }
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
