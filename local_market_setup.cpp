#include "local_market.h"
#include "local_market_internal.h"
#include "world.h"
#include "country.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <iostream>
#include <limits>

using namespace std;
bool LocalMarket::attachWarehouseNetwork(WarehouseNetwork& network) {
    if (&network == logisticsNetwork) return true;
    if (!network.hasWarehouse(marketId) &&
        !network.attachWarehouse(warehouse)) {
        return false;
    }
    logisticsNetwork = &network;
    externalLogistics = true;
    logisticsNetwork->attachSettlementAccount(
        marketId,
        [this](int good, Money quantity, Money amount) {
            if (!this->tryTradePayment(amount)) return false;
            this->addTradeBalance(good, -quantity);
            return true;
        },
        [this](int good, Money quantity, Money amount) {
            this->addTradePayment(good, quantity, amount);
        },
        [this](Money amount) { return this->quoteTransactionTax(amount); },
        [this](Money amount) { this->collectTransactionTax(amount); },
        [this](int good) { return this->quoteTradePrice(good); },
        [this]() { return this->bld.getBuildingCounts()[RAILWAY]; },
        [this](Money railwayRevenue, Money warehouseProfit) {
            this->addLogisticsRevenue(railwayRevenue, warehouseProfit);
        },
        [this](int good, Money quantity, Money amount) {
            this->refundTradePayment(good, quantity, amount);
        },
        [this]() { return this->quoteRailCapacityPrice(); });
    syncWarehouseProducers();
    return true;
}

void LocalMarket::attachFiscalCountry(Country* country) {
    fiscalCountry = country;
    if (country != nullptr) playerCash = Money(0);
    if (!externalLogistics || logisticsNetwork == nullptr) return;
    logisticsNetwork->attachSettlementAccount(
        marketId,
        [this](int good, Money quantity, Money amount) {
            if (!this->tryTradePayment(amount)) return false;
            this->addTradeBalance(good, -quantity);
            return true;
        },
        [this](int good, Money quantity, Money amount) {
            this->addTradePayment(good, quantity, amount);
        },
        [this](Money amount) { return this->quoteTransactionTax(amount); },
        [this](Money amount) { this->collectTransactionTax(amount); },
        [this](int good) { return this->quoteTradePrice(good); },
        [this]() { return this->bld.getBuildingCounts()[RAILWAY]; },
        [this](Money railwayRevenue, Money warehouseProfit) {
            this->addLogisticsRevenue(railwayRevenue, warehouseProfit);
        },
        [this](int good, Money quantity, Money amount) {
            this->refundTradePayment(good, quantity, amount);
        },
        [this]() { return this->quoteRailCapacityPrice(); });
}

Money LocalMarket::quoteTransactionTax(Money taxableAmount) const {
    return fiscalCountry == nullptr
        ? Money(0)
        : fiscalCountry->quoteTransactionTax(taxableAmount, stepCount);
}

Money LocalMarket::quoteRailCapacityPrice() const {
    const BuildingTemplate& railway = bld.getTemplates()[RAILWAY];
    const Money effectiveWage = buildingWages[RAILWAY] + buildingBonuses[RAILWAY];
    Money cost = Money(railway.laborPerUnit) * effectiveWage /
                Money(std::max(railway.outputRate, 1.0e-9));
    for (int good = 0; good < NUM_GOODS; ++good)
        cost += priceState.prices[good] * Money(railway.inputs[good]);
    return std::max(Money(0), cost);
}

Money LocalMarket::quoteTradePrice(int goodIndex) const {
    if (goodIndex < 0 || goodIndex >= NUM_GOODS)
        return Money(0);
    const Money observed = std::max(Money(0), priceState.prices[goodIndex]);
    const InventoryState& stock = warehouse.stock(goodIndex);
    const Money target = std::max(stock.policy.targetStock,
                                  stock.policy.reorderPoint);
    if (target <= Money(0)) return observed;

    const Money shortage = std::clamp(target - stock.position(),
                                      Money(0), target);
    const Money shortageRatio = shortage / target;
    // At most a 3x quote is exposed to remote sellers.  This premium is
    // intentionally bounded so a stale order cannot create an unbounded
    // arbitrage opportunity or bypass the normal affordability checks.
    return observed * (Money(1) + shortageRatio * Money(2));
}

Money LocalMarket::collectTransactionTax(Money taxableAmount) {
    return fiscalCountry == nullptr
        ? Money(0)
        : fiscalCountry->collectTransactionTax(taxableAmount, stepCount);
}

void LocalMarket::initializeWarehousePolicies() {
    std::array<Money, NUM_GOODS> weeklyCapacity{};
    const auto& templates = bld.getTemplates();
    const auto& counts = bld.getBuildingCounts();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const BuildingTemplate& bt = templates[type];
        if (bt.isFinancial || bt.outputGood < 0 || counts[type] <= 0)
            continue;
        weeklyCapacity[bt.outputGood] +=
            Money(counts[type]) * Money(bt.outputRate);
    }
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (good == CONSTR_GOOD_INDEX || good == TRANSPORT_CAPACITY_GOOD_INDEX) {
            warehouse.stock(good) = InventoryState{};
            continue;
        }
        const Money weeklyBase =
            std::max(Money(100), weeklyCapacity[good]);
        const int leadCycles = logisticsNetwork == nullptr
            ? 1 : logisticsNetwork->inboundLeadCycles(marketId, good);
        const int reorderWeeks = std::min(
            INVENTORY_TARGET_COVERAGE_WEEKS,
            leadCycles + INVENTORY_REVIEW_INTERVAL_WEEKS +
                INVENTORY_SAFETY_WEEKS);
        warehouse.stock(good).policy.targetStock =
            weeklyBase * Money(INVENTORY_TARGET_COVERAGE_WEEKS);
        warehouse.stock(good).policy.reorderPoint =
            weeklyBase * Money(reorderWeeks);
        warehouse.stock(good).policy.weeklyDemand = weeklyBase;
        warehouse.stock(good).onHand =
            weeklyBase * Money(reorderWeeks);
    }
    syncBuildingInputPolicies();
}

void LocalMarket::updateWarehouseDemandPolicies(
    const std::array<Money, NUM_GOODS>& plannedConsumerDemand,
    const std::array<Money, NUM_GOODS>& plannedIntermediateDemand) {
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (good == CONSTR_GOOD_INDEX || good == TRANSPORT_CAPACITY_GOOD_INDEX) continue;
        const Money exportDemand = logisticsNetwork == nullptr
            ? Money(0)
            : logisticsNetwork->expectedOutboundDemand(marketId, good);
        const Money rawDemand = std::max(
            Money(0), plannedConsumerDemand[good]) +
            std::max(Money(0), plannedIntermediateDemand[good]) +
            std::max(Money(0), exportDemand);
        const Money weeklyDemand =
            warehouseDemandFilters[good].update(rawDemand, 1.0);
        smoothedWarehouseDemand[good] = weeklyDemand;
        InventoryPolicy& policy = warehouse.stock(good).policy;
        policy.weeklyDemand = weeklyDemand;
        policy.targetStock =
            weeklyDemand * Money(INVENTORY_TARGET_COVERAGE_WEEKS);
        const int leadCycles = logisticsNetwork == nullptr
            ? 1 : logisticsNetwork->inboundLeadCycles(marketId, good);
        const int reorderWeeks = std::min(
            INVENTORY_TARGET_COVERAGE_WEEKS,
            leadCycles + INVENTORY_REVIEW_INTERVAL_WEEKS +
                INVENTORY_SAFETY_WEEKS);
        policy.reorderPoint = weeklyDemand * Money(reorderWeeks);
    }
}

void LocalMarket::syncBuildingInputPolicies() {
    if (logisticsNetwork == nullptr) return;
    const auto& templates = bld.getTemplates();
    const auto& counts = bld.getBuildingCounts();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const BuildingTemplate& bt = templates[type];
        for (int good = 0; good < NUM_GOODS; ++good) {
            InventoryPolicy policy;
            if (!bt.isFinancial && bt.outputGood >= 0 &&
                counts[type] > 0 && bt.inputs[good] > 0.0) {
                const int leadCycles =
                    logisticsNetwork->inboundLeadCycles(marketId, good);
                const int targetWeeks =
                    leadCycles + INVENTORY_REVIEW_INTERVAL_WEEKS +
                    INVENTORY_SAFETY_WEEKS;
                Money plannedOutput = type == CONST_DEPT
                    ? constructionInputPlanForPolicy()
                    : logisticsNetwork->productionCommand(
                          marketId, type, bt.outputGood);
                if (type != CONST_DEPT) {
                    plannedOutput = std::max(
                        plannedOutput,
                        logisticsNetwork->productionForecastDemand(
                            marketId, type, bt.outputGood));
                }
                if (stepCount == 0 || legacyDebugControls) {
                    const Money startupOutput =
                        Money(counts[type]) * Money(bt.outputRate) *
                        Money(legacyDebugControls ? 0.25 : 1.0);
                    plannedOutput = std::max(plannedOutput, startupOutput);
                }
                const Money weeklyUse =
                    plannedOutput * Money(bt.inputs[good]);
                policy.targetStock = weeklyUse * Money(targetWeeks);
                policy.reorderPoint = policy.targetStock;
                policy.weeklyDemand = weeklyUse;
                policy.baseStockReplenishment = true;
                if (legacyDebugControls && type != CONST_DEPT) {
                    const Money command = logisticsNetwork->productionCommand(
                        marketId, type, bt.outputGood);
                    const Money commandUse =
                        command * Money(bt.inputs[good]);
                    const Money forecast = logisticsNetwork->
                        productionForecastDemand(
                            marketId, type, bt.outputGood);
                    const Money forecastUse =
                        forecast * Money(bt.inputs[good]);
                    const Money capacityUse =
                        Money(counts[type]) * Money(bt.outputRate) *
                        Money(bt.inputs[good]);
                    const Money weeklyUse = std::max(
                        {Money(100), commandUse, forecastUse,
                         capacityUse * Money(0.25)});
                    const Money debugUse = std::max(
                        {weeklyUse, commandUse, forecastUse,
                         capacityUse * Money(0.25)});
                    policy.targetStock = debugUse * Money(targetWeeks);
                    policy.reorderPoint = policy.targetStock;
                    policy.weeklyDemand = debugUse;
                }
            }
            logisticsNetwork->setBuildingPolicy(
                marketId, type, good, policy);
        }
    }
}

void LocalMarket::syncWarehouseProducers() {
    if (logisticsNetwork == nullptr) return;
    const auto& templates = bld.getTemplates();
    const auto& counts = bld.getBuildingCounts();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const BuildingTemplate& bt = templates[type];
        // Construction departments consume their inputs directly during the
        // local simulation.  They are replenished below as a dedicated
        // building demand, so registering them as ordinary producers would
        // let a normal output order throttle their input supply.
        if (type == CONST_DEPT) {
            if (bt.outputGood >= 0)
                logisticsNetwork->removeProducer(marketId, bt.outputGood);
            continue;
        }
        if (bt.isFinancial || bt.outputGood < 0 ||
            bt.outputGood == TRANSPORT_CAPACITY_GOOD_INDEX) continue;
        if (counts[type] <= 0) {
            logisticsNetwork->removeProducer(marketId, bt.outputGood);
            continue;
        }
        ProductionRecipe recipe;
        recipe.warehouseId = marketId;
        recipe.buildingType = type;
        recipe.outputGood = bt.outputGood;
        recipe.outputPerBatch = Money(1);
        recipe.maxOutputPerCycle =
            Money(counts[type]) * Money(bt.outputRate);
        for (int good = 0; good < NUM_GOODS; ++good)
            recipe.inputs[good] = Money(bt.inputs[good]);
        logisticsNetwork->upsertProducer(recipe);
    }
}

void LocalMarket::prepareWarehouseCycle() {
    if (logisticsNetwork == nullptr) return;
    syncWarehouseProducers();
    syncBuildingInputPolicies();
}

void LocalMarket::planWarehouseReplenishments() {
    if (logisticsNetwork == nullptr) return;
    // Apply the one globally refreshed forecast before creating requests.
    syncBuildingInputPolicies();
    const auto& templates = bld.getTemplates();
    const auto& counts = bld.getBuildingCounts();
    const int cycle = logisticsNetwork->currentCycle();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        if (counts[type] <= 0 || templates[type].isFinancial ||
            templates[type].outputGood < 0) {
            continue;
        }
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (templates[type].inputs[good] <= 0.0) continue;
            const InventoryState& input = logisticsNetwork->buildingStock(
                marketId, type, good);
            if (input.policy.targetStock <= Money(0) &&
                input.policy.reorderPoint <= Money(0)) {
                continue;
            }
            const std::string key =
                "building-buffer:" + std::to_string(marketId) + ":" +
                std::to_string(type) + ":" + std::to_string(good) +
                ":cycle:" + std::to_string(cycle);
            logisticsNetwork->planBuildingReplenishment(
                marketId, type, good, key);
        }
    }
    logisticsNetwork->planWeeklyInventoryReview(marketId);
}

void LocalMarket::setPriceForSetup(int goodIdx, Money price) {
    if (goodIdx < 0 || goodIdx >= NUM_GOODS || !isfinite(price) || price <= Money(0))
        return;
    priceState.prices[goodIdx] = price;
}

bool LocalMarket::setBuildingCountForSetup(int typeIdx, int count,
                                            OwnerType owner) {
    if (!bld.setBuildingCountForSetup(typeIdx, count, owner)) return false;
    // Setup fixtures replace the building population; clear any bonus that
    // belonged to the previous configuration.
    buildingBonuses[typeIdx] = Money(0);
    reconcileSecurities();
    syncBuildingInputPolicies();
    syncWarehouseProducers();
    return true;
}

void LocalMarket::finalizeDebugSetup() {
    legacyDebugControls = true;
    playerCash = Money(50000000.0);
    // Setup fixtures can reuse the singleton debug world after prior cycles.
    // Reset the employment snapshot along with the building counts so the
    // first national plan sees the newly configured departments as staffed.
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const double fullEmployment =
            bld.getBuildingCounts()[type] *
            bld.getTemplates()[type].laborPerUnit;
        actualEmployment[type] = fullEmployment;
        targetEmployment[type] = fullEmployment;
        actualEmploymentRate[type] = fullEmployment > 0.0 ? 1.0 : 0.0;
    }
    // Setup fixtures may reuse the singleton debug world after a previous
    // cycle. Do not carry the old world's national plan into fresh buffers.
    setConstructionOutputPlan(Money(0), Money(0), false);
    initializeWarehousePolicies();
    for (int good = 0; good < NUM_GOODS; ++good) {
        // Construction power is virtual/current-cycle only and never gets a
        // debug warehouse buffer.
        if (good == CONSTR_GOOD_INDEX || good == TRANSPORT_CAPACITY_GOOD_INDEX) {
            warehouse.stock(good) = InventoryState{};
            continue;
        }
        // A fixed, small order-up-to buffer keeps the five-market trace
        // demand-responsive. Capacity-sized buffers make a specialist produce
        // one huge batch and then report zero weekly output for months.
        InventoryState& stock = warehouse.stock(good);
        stock.onHand = Money(400);
        stock.reserved = Money(0);
        stock.confirmedInbound = Money(0);
        stock.physicalInTransit = Money(0);
        stock.backlog = Money(0);
        stock.policy.targetStock =
            Money(100 * INVENTORY_TARGET_COVERAGE_WEEKS);
        const int leadCycles =
            logisticsNetwork->inboundLeadCycles(marketId, good);
        const int reorderWeeks = std::min(
            INVENTORY_TARGET_COVERAGE_WEEKS,
            leadCycles + INVENTORY_REVIEW_INTERVAL_WEEKS +
                INVENTORY_SAFETY_WEEKS);
        stock.policy.reorderPoint = Money(100 * reorderWeeks);
        stock.policy.weeklyDemand = Money(100);
    }
    syncBuildingInputPolicies();
    const auto& templates = bld.getTemplates();
    const auto& counts = bld.getBuildingCounts();
    for (int type = 0; type < TYPE_COUNT; ++type) {
        const BuildingTemplate& bt = templates[type];
        if (bt.isFinancial || bt.outputGood < 0 || counts[type] <= 0)
            continue;
        for (int good = 0; good < NUM_GOODS; ++good) {
            if (bt.inputs[good] <= 0.0) continue;
            const InventoryState& input = logisticsNetwork->buildingStock(
                marketId, type, good);
            Money seededStock = input.policy.targetStock;
            if (type == CONST_DEPT) {
                seededStock += input.policy.weeklyDemand *
                    Money(CONSTRUCTION_INPUT_SAFETY_WEEKS);
            }
            logisticsNetwork->setBuildingOnHandForSetup(
                marketId, type, good, seededStock);
        }
    }
    syncWarehouseProducers();

    initialTotalMoneySupply = Money(0);
    for (int type = 0; type < TYPE_COUNT; ++type)
        initialTotalMoneySupply += bld.getCashPools()[type];
    for (int classIndex = 0; classIndex < CLASS_COUNT; ++classIndex)
        initialTotalMoneySupply += classCash[classIndex];
    initialTotalMoneySupply += investmentPool + playerCash;
    totalMoneySupply = initialTotalMoneySupply;
}

void LocalMarket::setInvestmentLoanForSetup(Money balance, int dueStep,
                                             int delinquentWeeks) {
    investmentLoanBalance = clamp(balance, Money(0), MONEY_SUPPLY_MAX_MONEY);
    investmentLoanDueStep = investmentLoanBalance > Money(0) ? dueStep : -1;
    investmentLoanDelinquentWeeks = investmentLoanBalance > Money(0)
        ? std::max(0, delinquentWeeks) : 0;
    recalculateTotalDebt();
}

void LocalMarket::finalizeStandardSetup() {
    legacyDebugControls = false;
    playerCash = Money(0);

    for (int type = 0; type < TYPE_COUNT; ++type) {
        const double fullEmployment =
            bld.getBuildingCounts()[type] *
            bld.getTemplates()[type].laborPerUnit;
        actualEmployment[type] = fullEmployment;
        targetEmployment[type] = fullEmployment;
        actualEmploymentRate[type] = fullEmployment > 0.0 ? 1.0 : 0.0;
    }

    setConstructionOutputPlan(Money(0), Money(0), false);
    initializeWarehousePolicies();
    for (int good = 0; good < NUM_GOODS; ++good) {
        if (good == CONSTR_GOOD_INDEX || good == TRANSPORT_CAPACITY_GOOD_INDEX) {
            warehouse.stock(good) = InventoryState{};
            continue;
        }
        InventoryState& stock = warehouse.stock(good);
        stock.onHand = std::max(Money(0), stock.policy.reorderPoint);
        stock.reserved = Money(0);
        stock.confirmedInbound = Money(0);
        stock.physicalInTransit = Money(0);
        stock.backlog = Money(0);
        stock.lastRawReplenishment = Money(0);
        stock.lastPlannedReplenishment = Money(0);
    }

    syncBuildingInputPolicies();
    if (logisticsNetwork != nullptr) {
        const auto& templates = bld.getTemplates();
        const auto& counts = bld.getBuildingCounts();
        for (int type = 0; type < TYPE_COUNT; ++type) {
            const BuildingTemplate& bt = templates[type];
            if (bt.isFinancial || bt.outputGood < 0 || counts[type] <= 0)
                continue;
            for (int good = 0; good < NUM_GOODS; ++good) {
                if (bt.inputs[good] <= 0.0) continue;
                const InventoryState& input = logisticsNetwork->buildingStock(
                    marketId, type, good);
                Money seededStock = input.policy.targetStock;
                if (type == CONST_DEPT) {
                    seededStock += input.policy.weeklyDemand *
                        Money(CONSTRUCTION_INPUT_SAFETY_WEEKS);
                }
                logisticsNetwork->setBuildingOnHandForSetup(
                    marketId, type, good, seededStock);
            }
        }
    }
    syncWarehouseProducers();

    initialTotalMoneySupply = Money(0);
    for (int type = 0; type < TYPE_COUNT; ++type)
        initialTotalMoneySupply += bld.getCashPools()[type];
    for (int classIndex = 0; classIndex < CLASS_COUNT; ++classIndex)
        initialTotalMoneySupply += classCash[classIndex];
    initialTotalMoneySupply += investmentPool + playerCash;
    totalMoneySupply = initialTotalMoneySupply;
}
