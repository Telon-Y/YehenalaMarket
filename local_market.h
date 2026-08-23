// ==================== local_market.h ====================
#pragma once
#include "constants.h"
#include "building_manager.h"
#include "price_engine.h"
#include "sim_types.h"
#include "security.h"
#include "warehouse.h"
#include <unordered_map>
#include <array>
#include <stdexcept>
#include <vector>
class Country;
class World;


class LocalMarket {
public:
    LocalMarket(int id, const std::string& name);
    void step();
    void aiBuild();
    bool payAIExpansionStartup(int typeIndex);
    std::vector<AIExpansionCandidate> getAIExpansionCandidates() const;
    int placeGovernmentExpansion(int typeIndex, int count,
                                 Money reservedBudgetPerOrder);

    // ===== 市场标识 =====
    int getMarketId() const { return marketId; }
    const std::string& getMarketName() const { return marketName; }

    // ===== 价格访问器（委托 PriceState） =====
    const std::array<Money, NUM_GOODS>& getPrices() const { return priceState.prices; }
    void setPriceForSetup(int goodIdx, Money price);
    bool setBuildingCountForSetup(int typeIdx, int count,
                                  OwnerType owner = OWNER_INITIAL);
    void finalizeDebugSetup();
    void finalizeStandardSetup();
    Money getPriceLevel() const { return priceState.priceLevel; }
    const std::array<Money, NUM_GOODS>& getBaseReferencePrice() const { return priceState.baseRef; }
    const PriceState& getPriceState() const { return priceState; }

    // ===== 库存系统（2.0 准备） =====
    std::array<Money, NUM_GOODS> getInventory() const {
        return warehouse.onHandSnapshot();
    }
    Warehouse& getWarehouse() { return warehouse; }
    const Warehouse& getWarehouse() const { return warehouse; }
    bool attachWarehouseNetwork(WarehouseNetwork& network);
    void attachFiscalCountry(Country* country);
    Country* getFiscalCountry() { return fiscalCountry; }
    const Country* getFiscalCountry() const { return fiscalCountry; }
    void attachWorldContext(World* world, int provinceId);
    World* getOwnerWorld() { return ownerWorld; }
    const World* getOwnerWorld() const { return ownerWorld; }
    int getProvinceId() const { return provinceId; }
    Money consumeNationalConstruction(Money constructionUnits);
    void rollbackNationalConstruction(Money constructionUnits);
    void recordNationalConstructionSale(Money constructionUnits,
                                         Money payment);
    void recordNationalConstructionBase(Money constructionUnits,
                                         Money payment);
    void completeNationalConstruction(int typeIndex, int count,
                                      OwnerType owner = OWNER_GOVERNMENT);
    Money getAvailableNationalConstructionCapacity() const;
    bool hasWorldConstructionContext() const {
        return ownerWorld != nullptr && fiscalCountry != nullptr;
    }
    Money quoteTransactionTax(Money taxableAmount) const;
    // Settlement quotes include a bounded shortage premium.  This is only a
    // trade-clearing signal; the UI and production accounting continue to use
    // the observed local price.  Without it, a market whose input buffer is
    // empty can quote a falling price and make the very shipment that would
    // restart production appear unprofitable forever.
    Money quoteTradePrice(int goodIndex) const;
    Money quoteRailCapacityPrice() const;
    Money collectTransactionTax(Money taxableAmount);
    WarehouseNetwork& getWarehouseNetwork() {
        return *logisticsNetwork;
    }
    const WarehouseNetwork& getWarehouseNetwork() const {
        return *logisticsNetwork;
    }
    void addToInventory(int goodIdx, Money amount);
    void setInventoryForSetup(int goodIdx, Money amount);
    Money takeFromInventory(int goodIdx, Money amount);
    const std::array<Money, NUM_GOODS>& getTradeBalance() const { return tradeBalance; }
    void addTradeBalance(int goodIdx, Money amount);

    // ===== 市场快照 =====
    MarketSnapshot getSnapshot() const;
    const MarketFlowSnapshot& getLatestFlow() const { return latestFlow; }

    // ===== 原有接口 =====
    int getStepCount() const { return stepCount; }
    const std::vector<BuildingTemplate>& getBuildingTemplates() const { return bld.getTemplates(); }
    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return bld.getBuildingCounts(); }
    const std::vector<ConstructionOrder>& getConstructionQueue() const { return bld.getQueue(); }
    const std::array<double, TYPE_COUNT>& getEmploymentRatio() const { return bld.getEmploymentRatio(); }
    const std::array<double, TYPE_COUNT>& getAvgProfitRates() const { return bld.getAvgProfitRates(); }
    const std::array<double, TYPE_COUNT>& getSmoothedProfitRate() const { return bld.getSmoothedProfitRate(); }
    const std::array<Money, TYPE_COUNT>& getActualUnitProfits() const {
        return bld.getActualUnitProfits();
    }
    const std::array<Money, TYPE_COUNT>& getCashPools() const { return bld.getCashPools(); }
    const std::array<double, TYPE_COUNT>& getCurrentSupplyRatio() const { return bld.getCurrentSupplyRatio(); }
    const std::array<double, TYPE_COUNT>& getCapacityUtilization() const {
        return bld.getCapacityUtilization();
    }
    const std::array<double, TYPE_COUNT>& getFundingAvailability() const {
        return bld.getFundingAvailability();
    }
    const std::array<double, TYPE_COUNT>& getMaterialAvailability() const {
        return bld.getMaterialAvailability();
    }
    const std::array<Money, TYPE_COUNT>& getStaffedCapacity() const {
        return bld.getStaffedCapacity();
    }
    const std::array<Money, TYPE_COUNT>& getProductionTarget() const {
        return bld.getProductionTarget();
    }
    const std::vector<std::array<Money, NUM_GOODS>>& getPriceHistory() const { return priceHist; }
    const std::vector<Money>& getGDPHistory() const { return gdpHist; }
    const std::vector<double>& getPopulationHistory() const { return populationHist; }
    const std::array<Money, NUM_GOODS>& getLatestRawConsumerTarget() const {
        return latestRawConsumerTarget;
    }
    const std::array<Money, NUM_GOODS>& getLatestConsumerTarget() const { return latestConsumerTarget; }
    const std::array<Money, NUM_GOODS>& getSmoothedConsumerDemand() const {
        return smoothedConsumerDemand;
    }
    const std::array<Money, NUM_GOODS>& getLatestConsumerActual() const { return latestConsumerActual; }
    const std::array<Money, CLASS_COUNT>& getClassLastSpending() const {
        return classLastSpending;
    }
    double getClassPopulation(int classIndex) const {
        switch (classIndex) {
            case LABORER: return totalLaborers;
            case ENGINEER: return totalEngineers;
            case CAPITALIST: return totalCapitalists;
            default: return 0.0;
        }
    }
    double getClassEmployment(int classIndex) const {
        switch (classIndex) {
            case LABORER: return std::max(0.0, totalLaborers - dependentPopulation);
            case ENGINEER: return totalEngineers;
            case CAPITALIST: return totalCapitalists;
            default: return 0.0;
        }
    }
    double getEmploymentRate() const {
        double employed = 0.0;
        for (double value : actualEmployment) employed += value;
        return maxLabor > 0.0 ? std::clamp(employed / maxLabor, 0.0, 1.0) : 0.0;
    }    const std::array<Money, NUM_GOODS>& getLatestPotentialIn() const { return latestPotentialIn; }
    const std::array<Money, NUM_GOODS>& getLatestRealOut() const { return latestRealOut; }
    const std::array<Money, TYPE_COUNT>& getLatestBuildingOutput() const { return latestBuildingOutput; }
    int getSubsistenceFarms() const { return subsistenceFarms; }
    double getPopulation() const { return laborPopulation; }
    double getLaborForcePopulation() const { return maxLabor; }
    double getDependentPopulation() const { return dependentPopulation; }
    double getSatisfaction() const { return satisfaction; }
    double getInstantSatisfaction() const { return instantSatisfaction; }
    double getSubsistencePop() const { return subsistencePop; }
    Money getWeeklyGDP() const {
        return gdpHist.empty() ? Money(0) : gdpHist.back();
    }
    Money getGDP() const;
    Money getGDPAtCycle(int cycle) const;
    double getPopulationAtCycle(int cycle) const;
    Money getTotalMoneySupply() const { return totalMoneySupply; }
    Money getInvestmentPool() const { return investmentPool; }
    Money getClassCash(int idx) const {
        if (idx < 0 || idx >= CLASS_COUNT) throw std::out_of_range("class index out of range");
        return classCash[idx];
    }
    Money getLoanBalance(int typeIdx) const {
        if (typeIdx < 0 || typeIdx >= TYPE_COUNT)
            throw std::out_of_range("building type index out of range");
        return loanBalance[typeIdx];
    }
    int getBuildingLoanCount(int typeIdx) const {
        return (typeIdx >= 0 && typeIdx < TYPE_COUNT) ? buildingLoanCount[typeIdx] : 0;
    }
    const std::array<double, TYPE_COUNT>& getActualEmployment() const { return actualEmployment; }
    const std::array<double, TYPE_COUNT>& getActualEmploymentRate() const { return actualEmploymentRate; }
    const std::array<double, TYPE_COUNT>& getTargetEmployment() const {
        return targetEmployment;
    }
    const std::array<Money, TYPE_COUNT>& getWages() const { return buildingWages; }
    const std::array<Money, TYPE_COUNT>& getBonuses() const { return buildingBonuses; }
    std::array<Money, TYPE_COUNT> getEffectiveWages() const {
        std::array<Money, TYPE_COUNT> result = buildingWages;
        for (int type = 0; type < TYPE_COUNT; ++type) result[type] += buildingBonuses[type];
        return result;
    }
    Money getLastConstrProduced() const { return lastConstrProduced; }
    Money getLastConstrUsed() const { return lastConstrUsed; }
    // Installed construction capacity after staffing, before material limits.
    Money constructionCapacityForPlan() const;
    // Capacity that can be sustained by physical input and current supplier
    // forecasts over the input lead-time horizon.
    Money constructionSustainableCapacityForPlan() const;
    Money getPlannedConstructionOutput() const {
        return plannedConstructionOutput;
    }
    Money getBankLoanCapacity() const { return bankLoanCapacity; }
    Money getTotalDebt() const { return totalDebt; }
    Money getInvestmentLoanBalance() const { return investmentLoanBalance; }
    int   getInvestmentLoanDueStep() const { return investmentLoanDueStep; }
    int   getInvestmentLoanDelinquentWeeks() const { return investmentLoanDelinquentWeeks; }

    // ===== 玩家操作 =====
    void playerBuild(int typeIdx, int count);
    void playerDemolish(int typeIdx, int count);
    void setAIProfitThreshold(double v) { aiProfitThreshold = v; }
    double getAIProfitThreshold() const { return aiProfitThreshold; }
    BuildingManager& getBuildingManager() { return bld; }
    const BuildingManager& getBuildingManager() const { return bld; }
    bool performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to);

    // ===== 玩家记账账户 =====
    Money getPlayerCash() const {
        return (fiscalCountry != nullptr ||
                (ownerWorld != nullptr && !legacyDebugControls))
            ? Money(0) : playerCash;
    }
    void setPlayerCash(Money v) {
        if (fiscalCountry != nullptr ||
            (ownerWorld != nullptr && !legacyDebugControls)) {
            playerCash = Money(0);
            return;
        }
        playerCash = clamp(v, -CLASS_CASH_MAX_MONEY, CLASS_CASH_MAX_MONEY);
    }

    // ===== 建造划转累计（记账） =====
    Money getBuildTransferTotal() const { return buildTransferTotal; }

    // ===== 贸易支付接口（2.0预留） =====
    void addTradePayment(int goodIdx, Money quantity, Money amount);
    void addLogisticsRevenue(Money railwayRevenue,
                             Money warehouseProfit);
    bool tryTradePayment(Money amount);
    void refundTradePayment(int goodIdx, Money quantity, Money amount);
    void setInvestmentPoolForSetup(Money amount) {
        investmentPool = clamp(amount, Money(0), INVEST_POOL_MAX_MONEY);
        bld.syncBankLevels(investmentPool);
    }
    void setInvestmentLoanForSetup(Money balance, int dueStep,
                                   int delinquentWeeks = 0);
    void processInvestmentLoansForSetup(Money weeklyConstrDemand,
                                        Money constrPrice) {
        processBankLoans(weeklyConstrDemand, constrPrice);
    }

    // ===== 证券系统 =====
    void issueSecurities(int typeIdx, int count, OwnerType owner);
    bool transferSecurities(int typeIdx, int count, OwnerType from, OwnerType to);
    const std::vector<Security>& getSecurities() const { return securities; }

    // ===== 贷款结算辅助 =====
    void settleLoansBeforeDemolish(int typeIdx, int removeCount);

private:
    friend class World;
    // ===== 标识 =====
    int marketId;
    std::string marketName;

    // ===== 核心模块 =====
    PriceState priceState;
    BuildingManager bld;
    Warehouse warehouse;
    WarehouseNetwork standaloneWarehouseNetwork;
    WarehouseNetwork* logisticsNetwork = nullptr;
    bool externalLogistics = false;
    World* ownerWorld = nullptr;
    int provinceId = -1;
    Country* fiscalCountry = nullptr;
    bool legacyDebugControls = false;

    // ===== 库存与贸易 =====
    std::array<Money, NUM_GOODS> tradeBalance;
    std::array<Money, NUM_GOODS> pendingTradeRevenue;
    Money pendingRailwayRevenue = Money(0);
    Money pendingWarehouseProfit = Money(0);

    // ===== 劳动力/人口 =====
    double laborPopulation = 10'000'000.0;
    double initialLaborPopulation = 10'000'000.0;
    double maxLabor = 2'500'000.0;
    double dependentPopulation = 7'500'000.0;
    double subsistencePop = 0.0;
    double totalLaborers = 0.0;
    double totalEngineers = 0.0;
    double totalCapitalists = 0.0;
    double satisfaction = 0.75;
    double instantSatisfaction = 0.75;
    double smoothedLuxuryFactor = 1.0;
    RecentAverageFilter<double, 13> satisfactionFilter{};
    std::array<double, TYPE_COUNT> actualEmployment{};
    std::array<double, TYPE_COUNT> actualEmploymentRate{};
    std::array<double, TYPE_COUNT> targetEmployment{};
    std::array<Money, TYPE_COUNT> buildingWages;
    std::array<Money, TYPE_COUNT> buildingBonuses;

    // ===== 金融 =====
    std::array<Money, CLASS_COUNT> classCash;
    std::array<Money, CLASS_COUNT> classLastSpending;
    Money investmentPool = Money(200000000.0);
    Money totalMoneySupply = Money(0);
    Money initialTotalMoneySupply = Money(0);
    Money totalDebt = Money(0);
    Money playerCash = Money(0);                    // standalone compatibility only; country markets keep zero
    Money buildTransferTotal = Money(0);            // 建造累计划转金额
    std::array<Money, TYPE_COUNT> loanBalance;
    std::array<int, TYPE_COUNT> buildingLoanCount{};
    std::array<int, TYPE_COUNT> loanDelinquentWeeks{};
    Money bankLoanCapacity = Money(0);
    Money investmentLoanBalance = Money(0);
    int   investmentLoanDueStep = -1;
    int   investmentLoanDelinquentWeeks = 0;

    // ===== 证券 =====
    std::vector<Security> securities;
    int nextSecurityId = 0;

    // ===== 模拟参数 =====
    double averageWage = 6.75;
    double dt = 0.2;
    double aiProfitThreshold = 0.1;
    int stepCount = 0;
    int subsistenceFarms = 0;

    // ===== 统计信息 =====
    Money lastConstrProduced = Money(0);
    Money plannedConstructionOutput = Money(0);
    Money plannedConstructionInputOutput = Money(0);
    bool constructionPlanActive = false;
    Money lastConstrUsed = Money(0);
    Money localConstructionUsedThisCycle = Money(0);
    Money nationalConstructionUsedSinceStep = Money(0);
    Money pendingNationalConstructionRevenue = Money(0);
    std::array<Money, NUM_GOODS> latestConsumerTarget;
    std::array<Money, NUM_GOODS> latestRawConsumerTarget;
    std::array<Money, NUM_GOODS> smoothedConsumerDemand;
    std::array<RecentAverageFilter<Money, DEMAND_AVERAGE_WEEKS>, NUM_GOODS>
        consumerDemandFilters{};
    std::array<Money, NUM_GOODS> smoothedWarehouseDemand;
    std::array<RecentAverageFilter<Money, DEMAND_AVERAGE_WEEKS>, NUM_GOODS>
        warehouseDemandFilters{};
    std::array<Money, NUM_GOODS> latestPlannedConsumerDemand;
    std::array<Money, NUM_GOODS> latestConsumerActual;
    std::array<Money, NUM_GOODS> latestPotentialIn;
    std::array<Money, NUM_GOODS> latestRealOut;
    std::array<Money, TYPE_COUNT> latestBuildingOutput;
    MarketFlowSnapshot latestFlow;
    bool flowTraceActive = false;

    // ===== 历史记录 =====
    std::vector<std::array<Money, NUM_GOODS>> priceHist;
    std::vector<std::array<Money, NUM_GOODS>> outputHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<Money> gdpHist;
    std::vector<std::array<Money, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;
    int historyFirstCycle = 1;

    std::unordered_map<std::string, int> goodIndex;

    // ===== 内部流程 =====
    void processPriceLevelUpdate();
    Money fiscalTreasuryShare(bool initial) const;
    Money moneySupplyBaseline() const;
    void processLaborAllocation(std::array<double, TYPE_COUNT>& idealEmployment, bool& laborShortage);
    void processSupplyRatios(std::array<double, TYPE_COUNT>& activityRate, 
                             std::array<double, TYPE_COUNT>& supplyRatio,
                             std::array<Money, NUM_GOODS>& potentialIn);
    void processProduction(const std::array<double, TYPE_COUNT>& activityRate,
                           std::array<Money, NUM_GOODS>& formalOut,
                           std::array<Money, NUM_GOODS>& realOut,
                           std::array<Money, NUM_GOODS>& realIn,
                           std::array<Money, TYPE_COUNT>& buildingOutput);
    void processConsumption(const std::array<Money, NUM_GOODS>& realOut,
                            const std::array<Money, NUM_GOODS>& realIn,
                            std::array<Money, NUM_GOODS>& consumerTarget,
                            std::array<Money, NUM_GOODS>& consumerPlanned,
                            std::array<Money, NUM_GOODS>& consumerActual,
                            std::array<Money, NUM_GOODS>& consumerSpending);
    void processWagePayment(std::array<Money, TYPE_COUNT>& laborCostByBuilding);
    void processBankLoans(Money weeklyConstrDemand, Money constrPrice);
    Money calculateInvestmentCreditCapacity() const;
    void processBuildingBorrowing();
    void processConstruction(Money constrPrice, Money availConstr, 
                             Money& soldConstr, Money& constrRevenue);
    void processRevenueAllocation(const std::array<Money, NUM_GOODS>& formalOut,
                                  const std::array<Money, NUM_GOODS>& consumerSpending,
                                  const std::array<Money, NUM_GOODS>& intermediatePayment,
                                  const Money& constrRevenue,
                                  const std::array<double, TYPE_COUNT>& activityRate,
                                  std::array<Money, TYPE_COUNT>& revenueByBuilding);
    void processProfitDistribution(const std::array<Money, TYPE_COUNT>& revenueByBuilding,
                                   const std::array<Money, TYPE_COUNT>& inputCostByBuilding,
                                   const std::array<Money, TYPE_COUNT>& laborCostByBuilding,
                                   const std::array<Money, TYPE_COUNT>& buildingOutput,
                                   bool laborShortage,
                                   std::array<double, TYPE_COUNT>& actualProfitRates);
    void processBuildingRepayment(int typeIdx, Money& curCash, Money targetCash);
    void recalculateTotalDebt();
    void processMoneySupply();
    void processPopulationGrowth();
    void reconcileSecurities();
    void processSecurityMarket();
    void syncFinanceLevelFromSecurities();
    void recordHistory(const std::array<Money, NUM_GOODS>& realOut,
                       const std::array<Money, TYPE_COUNT>& buildingOutput,
                       Money gdp);
    void recordInventoryChange(const std::array<Money, NUM_GOODS>& supply,
                               const std::array<Money, NUM_GOODS>& demand);
    void initializeWarehousePolicies();
    void updateWarehouseDemandPolicies(
        const std::array<Money, NUM_GOODS>& plannedConsumerDemand,
        const std::array<Money, NUM_GOODS>& plannedIntermediateDemand);
    void syncBuildingInputPolicies();
    void syncWarehouseProducers();
    void prepareWarehouseCycle();
    void planWarehouseReplenishments();
    Money constructionOutputPlanForPolicy() const;
    Money constructionInputPlanForPolicy() const;
    void setConstructionOutputPlan(Money output, Money inputOutput,
                                   bool active);
    void beginFlowTrace(int cycle);
    void finalizeFlowTrace(const WarehouseCycleFlow& warehouseFlow);
};
