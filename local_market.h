// ==================== local_market.h ====================
#pragma once
#include "constants.h"
#include "construction.h"
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
class ConstructionSystem;


class LocalMarket {
public:
    LocalMarket(int id, const std::string& name);
    void step();
    void aiBuild();
    std::vector<AIExpansionCandidate> getAIExpansionCandidates(
        const std::array<int, TYPE_COUNT>& pendingCounts,
        Money totalRemainingConstruction) const;


    // Market identity.
    int getMarketId() const { return marketId; }
    const std::string& getMarketName() const { return marketName; }

    // Price accessors delegated to PriceState.
    const std::array<Money, NUM_GOODS>& getPrices() const { return priceState.prices; }
    void setPriceForSetup(int goodIdx, Money price);
    bool setBuildingCountForSetup(int typeIdx, int count,
                                  OwnerType owner = OWNER_INITIAL);
    bool configureProvinceScenarioForSetup(
        double population,
        const std::array<int, TYPE_COUNT>& buildings,
        const std::array<int, TYPE_COUNT>& resourceCaps);
    void finalizeDebugSetup();
    void finalizeStandardSetup();
    // Opening-state helpers, run once by the world setup after every market has
    // been configured: the first publishes the initial consumer demand into the
    // requirement graph, the second sizes the opening warehouse policy and stock
    // on the requirement that graph implies rather than on installed capacity.
    void publishInitialFinalDemand();
    void reconcileInitialWarehouseDemand();
    Money getPriceLevel() const { return priceState.priceLevel; }
    const std::array<Money, NUM_GOODS>& getBaseReferencePrice() const { return priceState.baseRef; }
    const PriceState& getPriceState() const { return priceState; }

    // Inventory system.
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

    // Market snapshots.
    MarketSnapshot getSnapshot() const;
    const MarketFlowSnapshot& getLatestFlow() const { return latestFlow; }

    // Public market interfaces.
    int getStepCount() const { return stepCount; }
    const std::vector<BuildingTemplate>& getBuildingTemplates() const { return bld.getTemplates(); }
    const std::array<int, TYPE_COUNT>& getBuildingCounts() const { return bld.getBuildingCounts(); }
    const std::array<int, TYPE_COUNT>& getResourceCaps() const {
        return resourceCaps;
    }
    int getResourceCap(int typeIdx) const {
        return typeIdx >= 0 && typeIdx < TYPE_COUNT
            ? resourceCaps[typeIdx] : -1;
    }
    const std::vector<ConstructionProject>& getConstructionQueue() const;
    const std::vector<ConstructionProject>& getConstructionHistory() const;
    const std::vector<ConstructionLedgerEntry>&
    getConstructionLedger() const;
    ConstructionQuote quoteConstruction(
        const ConstructionRequest& request) const;
    ConstructionCommandResult submitConstruction(
        const ConstructionRequest& request);
    bool cancelConstructionProject(ConstructionProjectId projectId);
    bool pauseConstructionProject(ConstructionProjectId projectId);
    bool resumeConstructionProject(ConstructionProjectId projectId);
    bool setConstructionProjectPriority(
        ConstructionProjectId projectId, int priority);
    bool moveConstructionProject(ConstructionProjectId projectId,
                                 bool up, bool toEdge = false);
    bool addConstructionProjectBudget(
        ConstructionProjectId projectId, Money amount);
    std::array<int, TYPE_COUNT> getPendingConstructionCounts() const;
    Money getWeeklyPrivateConstructionDemand(
        Money availableConstruction) const;
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
    const std::vector<std::array<Money, NUM_GOODS>>& getOutputHistory() const {
        return outputHist;
    }
    const std::vector<std::array<Money, NUM_GOODS>>& getDemandHistory() const {
        return demandHist;
    }
    const std::vector<Money>& getGDPHistory() const { return gdpHist; }
    // Unfloored production-approach GDP. Unlike gdpHist this series may contain
    // negative entries, so health checks and diagnostics can observe a market
    // whose value added fell below its intermediate input cost.
    const std::vector<Money>& getRawGDPHistory() const { return rawGdpHist; }
    const std::vector<double>& getPopulationHistory() const { return populationHist; }
    const std::array<Money, NUM_GOODS>& getLatestRawConsumerTarget() const {
        return latestRawConsumerTarget;
    }
    const std::array<Money, NUM_GOODS>& getLatestConsumerTarget() const { return latestConsumerTarget; }
    const std::array<Money, NUM_GOODS>& getSmoothedConsumerDemand() const {
        return smoothedConsumerDemand;
    }
    const std::array<Money, NUM_GOODS>& getLatestConsumerActual() const { return latestConsumerActual; }
    const std::array<std::array<Money, NUM_GOODS>, CLASS_COUNT>&
    getLatestClassConsumerActual() const {
        return latestClassConsumerActual;
    }
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
    Money getRawGDP() const;
    Money getRawGDPAtCycle(int cycle) const;
    double getPopulationAtCycle(int cycle) const;
    Money getTotalMoneySupply() const { return totalMoneySupply; }
    Money getInvestmentPool() const { return investmentPool; }
    // ---- Money audit -------------------------------------------------------
    // Every pool this market holds, including money that has already left a
    // pool but has not been credited to one yet (the pending* accumulators).
    // The world sums these with the country treasuries and the warehouse escrow
    // to check that money is conserved.
    Money moneyPoolsTotal() const;
    // Seigniorage is the only path that creates money.
    Money getSeigniorageThisCycle() const { return seigniorageThisCycle; }
    Money getSeigniorageCreated() const { return cumulativeSeigniorage; }
    // Labor-pool flow ledger, cumulative since setup. The channels are recorded
    // where the money actually moves, so an unexplained remainder means a
    // channel exists that this ledger does not know about.
    Money getLaborerWageInflow() const { return laborerWageInflow; }
    Money getLaborerSpendingOutflow() const { return laborerSpendingOutflow; }
    Money getLaborerDepositInflow() const { return laborerDepositInflow; }
    Money getLaborerPoolDelta() const { return laborerPoolDelta; }
    // Surplus investment capital returned to households, and the share each
    // class received. The labor pool is the terminal destination.
    Money getInvestmentPoolReturned() const { return investmentPoolReturned; }
    Money getHouseholdTransferInflow(int classIndex) const {
        return classIndex >= 0 && classIndex < CLASS_COUNT
            ? householdTransferInflow[static_cast<std::size_t>(classIndex)]
            : Money(0);
    }
    void recordLaborerDepositInterest(Money amount) {
        if (isfinite(amount) && amount > Money(0))
            laborerDepositInflow += amount;
    }
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

    // Player operations.
    ConstructionCommandResult playerBuildCommand(int typeIdx, int count);
    void playerBuild(int typeIdx, int count);
    bool canPlayerDemolish(int typeIdx) const;
    int playerDemolish(int typeIdx, int count);
    void setAIProfitThreshold(double v) { aiProfitThreshold = v; }
    double getAIProfitThreshold() const { return aiProfitThreshold; }
    BuildingManager& getBuildingManager() { return bld; }
    const BuildingManager& getBuildingManager() const { return bld; }
    bool performOwnershipTransfer(int typeIdx, int count, OwnerType from, OwnerType to);

    // Player accounting.
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
    Money getReservedSandboxConstructionBudget() const {
        return reservedSandboxConstructionBudget;
    }
    Money getAvailableSandboxConstructionBudget() const {
        return std::max(
            Money(0), playerCash - reservedSandboxConstructionBudget);
    }
    bool reserveSandboxConstructionBudget(Money amount);
    bool settleSandboxConstructionPayment(Money amount);
    void releaseSandboxConstructionBudget(Money amount);

    // Construction transfer ledger.
    Money getBuildTransferTotal() const { return buildTransferTotal; }

    // Trade payment API.
    void addTradePayment(int goodIdx, Money quantity, Money amount);
    void addLogisticsRevenue(Money railwayRevenue,
                             Money warehouseProfit);
    bool tryTradePayment(Money amount);
    void refundTradePayment(int goodIdx, Money quantity, Money amount);
    Money getReservedInvestmentConstructionBudget() const {
        return reservedInvestmentConstructionBudget;
    }
    Money getAvailableInvestmentForConstruction() const {
        return std::max(
            Money(0), investmentPool - reservedInvestmentConstructionBudget);
    }
    bool reserveInvestmentConstructionBudget(Money amount);
    bool settleInvestmentConstructionPayment(Money amount);
    void releaseInvestmentConstructionBudget(Money amount);
    bool capitalizePrivateConstruction(int typeIndex, Money amount);
    void setInvestmentPoolForSetup(Money amount) {
        investmentPool = clamp(amount, Money(0), INVEST_POOL_MAX_MONEY);
        reservedInvestmentConstructionBudget = Money(0);
        bld.syncBankLevels(investmentPool);
    }
    void setInvestmentLoanForSetup(Money balance, int dueStep,
                                   int delinquentWeeks = 0);
    void processInvestmentLoansForSetup(Money weeklyConstrDemand,
                                        Money constrPrice) {
        processBankLoans(weeklyConstrDemand, constrPrice);
    }

    // Securities.
    void issueSecurities(int typeIdx, int count, OwnerType owner);
    bool transferSecurities(int typeIdx, int count, OwnerType from, OwnerType to);
    const std::vector<Security>& getSecurities() const { return securities; }

    // Loan settlement helpers.
    void settleLoansBeforeDemolish(int typeIdx, int removeCount);

private:
    friend class World;
    friend class ConstructionSystem;
    // Identity.
    int marketId;
    std::string marketName;

    // Core modules.
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

    // Inventory and trade.
    std::array<Money, NUM_GOODS> tradeBalance;
    std::array<Money, NUM_GOODS> pendingTradeRevenue;
    Money pendingRailwayRevenue = Money(0);
    Money pendingWarehouseProfit = Money(0);

    // Labor and population.
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
    std::array<int, TYPE_COUNT> resourceCaps{};

    // Finance.
    std::array<Money, CLASS_COUNT> classCash;
    std::array<Money, CLASS_COUNT> classLastSpending;
    Money investmentPool = Money(200000000.0);
    Money reservedInvestmentConstructionBudget = Money(0);
    Money totalMoneySupply = Money(0);
    Money initialTotalMoneySupply = Money(0);
    // Money audit: seigniorage created this cycle and cumulative, plus the
    // labor-pool channel ledger used to attribute its long-run drift.
    Money seigniorageThisCycle = Money(0);
    Money cumulativeSeigniorage = Money(0);
    Money laborerWageInflow = Money(0);
    Money laborerSpendingOutflow = Money(0);
    Money laborerDepositInflow = Money(0);
    Money laborerPoolDelta = Money(0);
    Money laborerCycleStartCash = Money(0);
    Money investmentPoolReturned = Money(0);
    std::array<Money, CLASS_COUNT> householdTransferInflow{};
    Money totalDebt = Money(0);
    Money playerCash = Money(0);                    // standalone compatibility only; country markets keep zero
    Money reservedSandboxConstructionBudget = Money(0);
    CountryConstructionState standaloneConstructionState;
    ConstructionProjectId nextStandaloneConstructionProjectId = 1;
    Money buildTransferTotal = Money(0);            // Cumulative construction transfers.
    std::array<Money, TYPE_COUNT> loanBalance;
    std::array<int, TYPE_COUNT> buildingLoanCount{};
    std::array<int, TYPE_COUNT> loanDelinquentWeeks{};
    Money bankLoanCapacity = Money(0);
    // Intermediation fee earned by the financial district during the current
    // cycle. It is reported as FINANCE revenue (and therefore enters the profit
    // rate and GDP) although the cash is credited directly to the district when
    // the borrower pays interest.
    Money financeFeeThisCycle = Money(0);
    // Interest income the commercial bank retained this cycle. The savings bank
    // pays household deposit interest out of this spread, which keeps the
    // payment bounded by income actually earned.
    Money bankSpreadThisCycle = Money(0);
    // Part of the deposit spread the savings bank keeps after paying households.
    Money savingsBankIncomeThisCycle = Money(0);
    Money investmentLoanBalance = Money(0);
    int   investmentLoanDueStep = -1;
    int   investmentLoanDelinquentWeeks = 0;

    // Securities.
    std::vector<Security> securities;
    int nextSecurityId = 0;

    // Simulation parameters.
    double averageWage = 6.75;
    double dt = 0.2;
    double aiProfitThreshold = 0.1;
    int stepCount = 0;
    int subsistenceFarms = 0;

    // Statistics.
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
    std::array<std::array<Money, NUM_GOODS>, CLASS_COUNT>
        latestClassConsumerActual{};
    std::array<Money, NUM_GOODS> latestPotentialIn;
    std::array<Money, NUM_GOODS> latestRealOut;
    std::array<Money, TYPE_COUNT> latestBuildingOutput;
    MarketFlowSnapshot latestFlow;
    bool flowTraceActive = false;

    // History.
    std::vector<std::array<Money, NUM_GOODS>> priceHist;
    std::vector<std::array<Money, NUM_GOODS>> outputHist;
    std::vector<std::array<Money, NUM_GOODS>> demandHist;
    std::vector<std::array<int, TYPE_COUNT>> buildingHist;
    std::vector<Money> gdpHist;
    std::vector<Money> rawGdpHist;
    std::vector<std::array<Money, TYPE_COUNT>> cashPoolHist;
    std::vector<double> populationHist;
    int historyFirstCycle = 1;

    std::unordered_map<std::string, int> goodIndex;

    // Internal workflow.
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
    // Returns surplus investment capital to households. Money the market's own
    // construction demand cannot absorb must not accumulate in the pool
    // forever; the class pools - the labor pool first - are its terminal
    // destination.
    void distributeExcessInvestmentPool();
    void processMoneySupply();
    void processPopulationGrowth();
    void reconcileSecurities();
    void processSecurityMarket();
    void syncFinanceLevelFromSecurities();
    void recordHistory(const std::array<Money, NUM_GOODS>& realOut,
                       const std::array<Money, NUM_GOODS>& demand,
                       const std::array<Money, TYPE_COUNT>& buildingOutput,
                       Money gdp, Money rawGdp);
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
