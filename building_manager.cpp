// building_manager.cpp
#include "building_manager.h"
#include <algorithm>
#include <cmath>
#include <numeric>

BuildingManager::BuildingManager() {
    templates = createBuildingTemplates();
    buildingCounts.fill(10); // 初始等级 10（根据需求调整）
    for (int t = 0; t < TYPE_COUNT; ++t) {
        ownedBuildings[t][OWNER_INITIAL] = buildingCounts[t];
        ownedBuildings[t][OWNER_GOVERNMENT] = 0;
        ownedBuildings[t][OWNER_FINANCE] = 0;
    }
    buildingCounts[FINANCE] = 0;
    buildingCounts[BANK] = 10;

    avgProfitRates.fill(0.0);
    smoothedProfitRate.fill(0.0);
    employmentRatio.fill(1.0);
    consecutiveLowEmpWeeks.fill(0);
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK)
            cashPools[t] = 100'000'000.0;
        else if (t == FINANCE || t == CONST_DEPT)
            cashPools[t] = 0.0;
        else
            cashPools[t] = buildingCounts[t] * 500000.0;
        clampCash(t);
    }
    lastDemolishStep.fill(-9999);
    currentSupplyRatio.fill(1.0);
}

void BuildingManager::placeOrder(int typeIdx, OwnerType owner) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT) return;
    int pending = 0;
    for (const auto& ord : constructionQueue)
        if (ord.typeIndex == typeIdx) pending++;
    if (pending >= 50) return;
    constructionQueue.push_back({typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], false, owner});
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
        ConstructionOrder order{typeIdx, buildingCost[typeIdx], buildingCost[typeIdx], false, OWNER_GOVERNMENT};
        if (top) constructionQueue.insert(constructionQueue.begin(), order);
        else     constructionQueue.push_back(order);
    }
}

void BuildingManager::demolishBuildings(int typeIdx, int count, int stepCount, double& investmentPool) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || buildingCounts[typeIdx] <= 0) return;
    if (stepCount - lastDemolishStep[typeIdx] < demolishCooldownPeriod) return;
    int actual = std::min(count, buildingCounts[typeIdx]);
    if (actual <= 0) return;

    double totalCash = cashPools[typeIdx];
    double cashPerLevel = (buildingCounts[typeIdx] > 0) ? totalCash / buildingCounts[typeIdx] : 0.0;
    std::array<int, OWNER_COUNT> local = ownedBuildings[typeIdx];
    int remaining = actual;
    for (int o = 0; o < OWNER_COUNT && remaining > 0; ++o) {
        int take = std::min(remaining, local[o]);
        if (take == 0) continue;
        double cashTrans = cashPerLevel * take;
        cashPools[typeIdx] -= cashTrans;
        if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) {
            investmentPool += cashTrans;
        } else if (o == OWNER_FINANCE) {
            cashPools[FINANCE] += cashTrans;
        }
        local[o] -= take;
        remaining -= take;
    }
    ownedBuildings[typeIdx] = local;
    buildingCounts[typeIdx] -= actual;
    lastDemolishStep[typeIdx] = stepCount;

    clampCash(typeIdx);
    clampCash(FINANCE);

    if (buildingCounts[typeIdx] == 0) {
        cleanupDeadBuilding(typeIdx, investmentPool);
    }
    syncFinanceCount();
}

std::array<int, TYPE_COUNT> BuildingManager::getInQueueCounts() const {
    std::array<int, TYPE_COUNT> counts{};
    for (const auto& ord : constructionQueue)
        counts[ord.typeIndex]++;
    return counts;
}

void BuildingManager::aiBuild(double aiProfitThreshold,
                              const std::array<double, NUM_GOODS>& prices,
                              double averageWage,
                              double availableLabor,
                              const std::array<double, TYPE_COUNT>& actualEmploymentRate) {
    auto inQueueCount = getInQueueCounts();

    // 计算当前总劳动力需求（含在建）
    double totalLaborDemand = 0.0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        totalLaborDemand += (buildingCounts[t] + inQueueCount[t]) * templates[t].laborPerUnit;
    }
    double remainingLabor = std::max(0.0, availableLabor - totalLaborDemand);

    int totalFarms = buildingCounts[FARM_GRAIN] + buildingCounts[COTTON]
                     + inQueueCount[FARM_GRAIN] + inQueueCount[COTTON];
    int farmSlotsRemaining = maxTotalFarms - totalFarms;
    bool farmCap = (totalFarms >= maxTotalFarms);
    bool coalCap = (buildingCounts[COAL_MINE] + inQueueCount[COAL_MINE] >= maxCoalMines);
    bool ironCap = (buildingCounts[IRON_MINE] + inQueueCount[IRON_MINE] >= maxIronMines);
    bool goldCap = (buildingCounts[GOLD_MINE] + inQueueCount[GOLD_MINE] >= maxGoldMines);
    bool constrCap = (buildingCounts[CONST_DEPT] + inQueueCount[CONST_DEPT] >= maxConstDept);
    bool bankCap = (buildingCounts[BANK] + inQueueCount[BANK] >= maxBanks);
    bool finCap = (buildingCounts[FINANCE] + inQueueCount[FINANCE] >= maxFinances);

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
        double laborNeeded = templates[CONST_DEPT].laborPerUnit;
        if (laborNeeded <= remainingLabor + 1e-9) {
            constructionQueue.insert(constructionQueue.begin(),
                { CONST_DEPT, buildingCost[CONST_DEPT], buildingCost[CONST_DEPT], false, OWNER_FINANCE });
            remainingLabor -= laborNeeded;
        }
    }

    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK || t == FINANCE) continue;
        if (inQueueCount[t] >= 50) continue;
        if ((t == FARM_GRAIN || t == COTTON) && farmSlotsRemaining <= 0) continue;
        if (t == COAL_MINE && coalCap) continue;
        if (t == IRON_MINE && ironCap) continue;
        if (t == GOLD_MINE && goldCap) continue;
        if (t == CONST_DEPT && constrCap) continue;

        double smoothed = smoothedProfitRate[t];
        if (buildingCounts[t] == 0 && inQueueCount[t] == 0) {
            const auto& bt = templates[t];
            if (bt.isFinancial) continue;
            double estCost = bt.getUnitCost(prices, averageWage);
            double estProfit = (estCost > 1e-6) ? (prices[bt.outputGood] - estCost) / estCost : 0.0;
            if (estProfit > aiProfitThreshold) {
                if (bt.laborPerUnit > remainingLabor + 1e-9) continue;
                if (t == FARM_GRAIN || t == COTTON) {
                    if (farmSlotsRemaining > 0) {
                        placeOrder(t, OWNER_FINANCE);
                        remainingLabor -= bt.laborPerUnit;
                        farmSlotsRemaining--;
                    }
                } else if (t == GOLD_MINE) {
                    if (buildingCounts[GOLD_MINE] + inQueueCount[GOLD_MINE] < maxGoldMines) {
                        placeOrder(t, OWNER_FINANCE);
                        remainingLabor -= bt.laborPerUnit;
                    }
                } else {
                    placeOrder(t, OWNER_FINANCE);
                    remainingLabor -= bt.laborPerUnit;
                }
            }
            continue;
        }
        if (smoothed > aiProfitThreshold && buildingCounts[t] > 0 && !templates[t].isFinancial) {
            int N_wanted = (int)ceil((smoothed - aiProfitThreshold) / 0.05);
            N_wanted = std::max(0, N_wanted);
            int N_remaining = N_wanted - inQueueCount[t];
            if (N_remaining <= 0) continue;
            if (t == FARM_GRAIN || t == COTTON) N_remaining = std::min(N_remaining, farmSlotsRemaining);
            else if (t == COAL_MINE) N_remaining = std::min(N_remaining, maxCoalMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == IRON_MINE) N_remaining = std::min(N_remaining, maxIronMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == GOLD_MINE) N_remaining = std::min(N_remaining, maxGoldMines - (buildingCounts[t] + inQueueCount[t]));
            else if (t == CONST_DEPT) N_remaining = std::min(N_remaining, maxConstDept - (buildingCounts[t] + inQueueCount[t]));

            int maxQueue = 50 - inQueueCount[t];
            if (N_remaining > maxQueue) N_remaining = maxQueue;
            int maxExpand = std::max(1, (int)ceil(buildingCounts[t] * 0.1));
            int N_final = std::min(N_remaining, maxExpand);

            // ===== 劳动力限制：实际雇佣率>=90%的建筑不受限制，否则受剩余劳动力限制 =====
            double laborPerUnit = templates[t].laborPerUnit;
            bool highEmployment = actualEmploymentRate[t] >= 0.90;
            if (highEmployment) {
                // 高雇佣率：不限制，但不超过队列上限等
                N_final = N_final;
            } else {
                int maxAllowedByLabor = (laborPerUnit > 1e-6) ? (int)(remainingLabor / laborPerUnit) : 0;
                N_final = std::min(N_final, maxAllowedByLabor);
            }

            for (int j = 0; j < N_final; ++j) placeOrder(t, OWNER_FINANCE);
            remainingLabor -= N_final * laborPerUnit;
            if (t == FARM_GRAIN || t == COTTON) farmSlotsRemaining -= N_final;
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
        if (bt.isFinancial) {
            idealLabor += buildingCounts[t] * bt.laborPerUnit * 1.0;
        } else {
            idealLabor += buildingCounts[t] * bt.laborPerUnit * employmentRatio[t];
        }
    }

    double scale = (idealLabor > 1e-9) ? std::min(1.0, maxLabor / idealLabor) : 1.0;
    if (scale < 1.0) {
        for (int t = 0; t < TYPE_COUNT; ++t)
            rates[t] *= scale;
    }
    return rates;
}

double BuildingManager::processConstruction(double availableConstr, double constrPrice,
                                           double& investmentPool) {
    double soldConstr = 0.0;
    for (auto& ord : constructionQueue) {
        if (availableConstr <= 0) break;
        double maxAfford = (constrPrice > 1e-9) ? investmentPool / constrPrice : 0.0;
        if (maxAfford <= 0.0) break;
        double invest = std::min({ availableConstr, 30.0, ord.remainingCost, maxAfford });
        if (invest <= 0) continue;

        double cost = invest * constrPrice;
        investmentPool -= cost;
        ord.remainingCost -= invest;
        availableConstr -= invest;
        soldConstr += invest;
    }

    constructionQueue.erase(
        std::remove_if(constructionQueue.begin(), constructionQueue.end(),
            [&](ConstructionOrder& o) {
                if (o.remainingCost <= 0) {
                    buildingCounts[o.typeIndex]++;
                    ownedBuildings[o.typeIndex][o.owner]++;
                    syncFinanceCount();
                    return true;
                }
                return false;
            }),
        constructionQueue.end());
    return soldConstr;
}

void BuildingManager::updateActualProfitRates(const std::array<double, TYPE_COUNT>& actualRates) {
    std::array<double, TYPE_COUNT> safeRates = actualRates;
    for (int t = 0; t < TYPE_COUNT; ++t)
        if (!std::isfinite(safeRates[t])) safeRates[t] = 0.0;

    avgProfitRates = safeRates;
    if (!profitInitialized) {
        smoothedProfitRate = safeRates;
        profitInitialized = true;
    } else {
        double alpha = 0.2;
        for (int t = 0; t < TYPE_COUNT; ++t)
            smoothedProfitRate[t] = smoothedProfitRate[t] * (1.0 - alpha) + safeRates[t] * alpha;
    }
}

void BuildingManager::adjustEmployment() {
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) continue;
        if (templates[t].isFinancial) continue;
        if (t == CONST_DEPT) {
            employmentRatio[t] = 1.0;
            continue;
        }
        if (!std::isfinite(smoothedProfitRate[t])) smoothedProfitRate[t] = 0.0;
        if (!std::isfinite(employmentRatio[t])) employmentRatio[t] = 1.0;

        if (smoothedProfitRate[t] < 0.0) {
            employmentRatio[t] *= 0.80;
        } else {
            employmentRatio[t] += (1.0 - employmentRatio[t]) * 0.30;
        }
        employmentRatio[t] = std::clamp(employmentRatio[t], 0.0, 1.0);
    }
}

void BuildingManager::checkDecay(int stepCount, double& investmentPool,
                                 std::array<double, CLASS_COUNT>& classCash) {
    if (stepCount <= 52) {
        resetDecayCounters();
        return;
    }
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (buildingCounts[t] == 0) {
            consecutiveLowEmpWeeks[t] = 0;
            continue;
        }
        bool distressed = (employmentRatio[t] <= 0.75) || (cashPools[t] < 0.0);
        if (distressed) {
            consecutiveLowEmpWeeks[t]++;
        } else {
            consecutiveLowEmpWeeks[t] = 0;
        }

        if (consecutiveLowEmpWeeks[t] >= 156) {
            int reduce = std::max(1, (int)ceil(buildingCounts[t] * 0.05));
            double totalCash = cashPools[t];
            double cashPerLevel = (buildingCounts[t] > 0) ? totalCash / buildingCounts[t] : 0.0;
            std::array<int, OWNER_COUNT> local = ownedBuildings[t];
            int remaining = reduce;
            for (int o = 0; o < OWNER_COUNT && remaining > 0; ++o) {
                int take = std::min(remaining, local[o]);
                if (take == 0) continue;
                double cashTrans = cashPerLevel * take;
                cashPools[t] -= cashTrans;
                if (o == OWNER_GOVERNMENT || o == OWNER_INITIAL) {
                    investmentPool += cashTrans;
                } else if (o == OWNER_FINANCE) {
                    cashPools[FINANCE] += cashTrans;
                }
                local[o] -= take;
                remaining -= take;
            }
            ownedBuildings[t] = local;
            buildingCounts[t] = std::max(0, buildingCounts[t] - reduce);
            if (buildingCounts[t] == 0) {
                cleanupDeadBuilding(t, investmentPool);
            }
            consecutiveLowEmpWeeks[t] = 0;
            syncFinanceCount();
            clampCash(t);
            clampCash(FINANCE);
        }
    }
    clampAllCash();
}

void BuildingManager::resetDecayCounters() {
    consecutiveLowEmpWeeks.fill(0);
}

void BuildingManager::cleanupDeadBuilding(int typeIdx, double& investmentPool) {
    if (buildingCounts[typeIdx] > 0) return;
    if (cashPools[typeIdx] != 0.0) {
        investmentPool += cashPools[typeIdx];
        cashPools[typeIdx] = 0.0;
    }
    for (int o = 0; o < OWNER_COUNT; ++o) ownedBuildings[typeIdx][o] = 0;
    clampCash(typeIdx);
}

void BuildingManager::syncFinanceCount() {
    int totalOwnedByFinance = 0;
    for (int t = 0; t < TYPE_COUNT; ++t) {
        if (t == BANK || t == FINANCE || t == CONST_DEPT) continue;
        totalOwnedByFinance += ownedBuildings[t][OWNER_FINANCE];
    }
    int diff = totalOwnedByFinance - buildingCounts[FINANCE];
    if (diff > 0) {
        buildingCounts[FINANCE] += diff;
    } else if (diff < 0) {
        int remove = -diff;
        if (remove > buildingCounts[FINANCE]) remove = buildingCounts[FINANCE];
        buildingCounts[FINANCE] -= remove;
    }
}

double BuildingManager::transferOwnership(int typeIdx, int count, OwnerType from, OwnerType to,
                                         double& investmentPool, std::array<double, CLASS_COUNT>& classCash) {
    if (typeIdx < 0 || typeIdx >= TYPE_COUNT || count <= 0 || from == to) return 0.0;
    count = std::min(count, ownedBuildings[typeIdx][from]);
    if (count == 0) return 0.0;

    double unitPrice = (buildingCounts[typeIdx] > 0) ? cashPools[typeIdx] / buildingCounts[typeIdx] : 500000.0;
    double totalPrice = unitPrice * count;

    if (to == OWNER_FINANCE) {
        if (cashPools[FINANCE] < totalPrice) return 0.0;
        cashPools[FINANCE] -= totalPrice;
        clampCash(FINANCE);
    } else if (to == OWNER_GOVERNMENT) {
        if (investmentPool < totalPrice) return 0.0;
        investmentPool -= totalPrice;
    } else if (to == OWNER_INITIAL) {
        if (classCash[CAPITALIST] < totalPrice) return 0.0;
        classCash[CAPITALIST] -= totalPrice;
    }

    if (from == OWNER_GOVERNMENT) {
        investmentPool += totalPrice;
    } else if (from == OWNER_INITIAL) {
        classCash[CAPITALIST] += totalPrice;
    } else if (from == OWNER_FINANCE) {
        cashPools[FINANCE] += totalPrice;
        clampCash(FINANCE);
    }

    ownedBuildings[typeIdx][from] -= count;
    ownedBuildings[typeIdx][to]   += count;
    syncFinanceCount();
    return totalPrice;
}