#include "debug_report.h"

#include "world.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <map>
#include <set>
#include <tuple>
#include <vector>

namespace {

void WriteMoney(std::ostream& output, Money value) {
    output << std::setprecision(12) << value.toDouble();
}

void WriteString(std::ostream& output, const std::string& value) {
    output << '"';
    for (const unsigned char character : value) {
        switch (character) {
        case '\\': output << "\\\\"; break;
        case '"': output << "\\\""; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default: output << static_cast<char>(character); break;
        }
    }
    output << '"';
}

bool HasCompleteDomesticTopology(const World& world) {
    std::set<std::tuple<int, int, int>> links;
    std::map<int, int> marketCountry;
    std::size_t expectedLinks = 0;
    for (int countryIndex = 0; countryIndex < world.getCountryCount();
         ++countryIndex) {
        const Country& country = world.getCountry(countryIndex);
        const auto& localMarketIds =
            country.getNationalMarket().getLocalMarketIds();
        expectedLinks += localMarketIds.size() * (localMarketIds.size() - 1) *
                         static_cast<std::size_t>(NUM_GOODS - 2);
        for (const int marketId : localMarketIds)
            marketCountry[marketId] = country.getId();
    }

    for (const TradePath& path : world.getTradePaths()) {
        if (!path.active) continue;
        const auto source = marketCountry.find(path.sourceMarketId);
        const auto target = marketCountry.find(path.targetMarketId);
        if (source == marketCountry.end() || target == marketCountry.end() ||
            source->second != target->second) {
            return false;
        }
        links.emplace(path.sourceMarketId, path.targetMarketId,
                      path.goodIndex);
    }
    if (links.size() != expectedLinks) return false;

    for (int countryIndex = 0; countryIndex < world.getCountryCount();
         ++countryIndex) {
        const auto& localMarketIds =
            world.getCountry(countryIndex).getNationalMarket().getLocalMarketIds();
        for (const int sourceMarketId : localMarketIds) {
            for (const int targetMarketId : localMarketIds) {
                if (sourceMarketId == targetMarketId) continue;
                for (int good = 0; good < NUM_GOODS; ++good) {
                    if (good == CONSTR_GOOD_INDEX ||
                        good == TRANSPORT_CAPACITY_GOOD_INDEX) {
                        continue;
                    }
                    if (links.find({sourceMarketId, targetMarketId, good}) ==
                        links.end()) {
                        return false;
                    }
                }
            }
        }
    }
    return true;
}

struct MarketRegressionMetrics {
    double gdpCv104 = 0.0;
    int nearZeroGdpWeeks104 = 0;
    double populationRetention = 1.0;
};

MarketRegressionMetrics CalculateRegressionMetrics(
    const LocalMarket& market) {
    MarketRegressionMetrics metrics;
    const auto& history = market.getGDPHistory();
    const std::size_t count = std::min<std::size_t>(104, history.size());
    if (count > 0) {
        const std::size_t begin = history.size() - count;
        double mean = 0.0;
        for (std::size_t index = begin; index < history.size(); ++index) {
            const double value = history[index].toDouble();
            mean += value;
            if (std::fabs(value) <= 1.0e-6)
                ++metrics.nearZeroGdpWeeks104;
        }
        mean /= static_cast<double>(count);
        double variance = 0.0;
        for (std::size_t index = begin; index < history.size(); ++index) {
            const double delta = history[index].toDouble() - mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(count);
        if (std::fabs(mean) > 1.0e-9) {
            metrics.gdpCv104 =
                std::sqrt(variance) / std::fabs(mean);
        } else {
            metrics.gdpCv104 = variance <= 1.0e-18 ? 0.0 : 1.0e9;
        }
    }
    const double initialPopulation = market.getPopulationAtCycle(0);
    if (initialPopulation > 0.0) {
        metrics.populationRetention =
            market.getPopulation() / initialPopulation;
    }
    return metrics;
}

}  // namespace

bool WriteDebugStateReport(std::ostream& output, const World& world) {
    const WarehouseAudit audit = world.getWarehouseNetwork().audit();
    const TransportationSnapshot transport =
        world.getTransportationSnapshot();
    const bool topologyComplete = HasCompleteDomesticTopology(world);
    bool inventoryBalanced = true;
    bool allGdpFinite = true;
    bool allGdpPositive = world.getMarketCount() > 0;
    bool gdpStable = true;
    bool productionContinuous = true;
    bool populationRetained = true;
    bool inventoryReviewCadence = true;
    bool demandSignalsFinite = true;
    bool railRouteContract = !transport.routes.empty();
    bool remoteTradeProfitable = true;
    bool constructionPoolHealthy = true;
    int remoteOrderCount = 0;
    Money remoteReceived = Money(0);
    int cycle = 0;
    std::vector<MarketRegressionMetrics> regressionMetrics;
    regressionMetrics.reserve(
        static_cast<std::size_t>(world.getMarketCount()));

    for (int marketIndex = 0; marketIndex < world.getMarketCount();
         ++marketIndex) {
        const LocalMarket& market = world.getMarket(marketIndex);
        cycle = std::max(cycle, market.getStepCount());
        const MarketSnapshot snapshot = market.getSnapshot();
        inventoryReviewCadence = inventoryReviewCadence &&
            snapshot.inventoryReviewCount <=
                static_cast<std::uint64_t>(market.getStepCount()) &&
            snapshot.lastInventoryReviewCycle <
                world.getWarehouseNetwork().currentCycle();
        for (int good = 0; good < NUM_GOODS; ++good) {
            demandSignalsFinite = demandSignalsFinite &&
                isfinite(snapshot.warehouseStock[good].demand52) &&
                snapshot.warehouseStock[good].demand52 >= Money(0) &&
                isfinite(snapshot.productionDemand52[good]) &&
                snapshot.productionDemand52[good] >= Money(0) &&
                isfinite(snapshot.productionCommand[good]) &&
                snapshot.productionCommand[good] >= Money(0);
        }
        inventoryBalanced =
            inventoryBalanced && market.getLatestFlow().inventoryBalanced;
        allGdpFinite =
            allGdpFinite && isfinite(market.getGDP());
        allGdpPositive =
            allGdpPositive && market.getGDP() > Money(0);
        const MarketRegressionMetrics metrics =
            CalculateRegressionMetrics(market);
        regressionMetrics.push_back(metrics);
        if (market.getGDPHistory().size() >= 104) {
            gdpStable = gdpStable && std::isfinite(metrics.gdpCv104) &&
                metrics.gdpCv104 <= 0.50;
            productionContinuous = productionContinuous &&
                metrics.nearZeroGdpWeeks104 <= 4;
        }
        if (market.getStepCount() >= 260) {
            populationRetained = populationRetained &&
                metrics.populationRetention >= 0.75;
        }
    }

    for (const RouteSnapshot& route : transport.routes) {
        railRouteContract = railRouteContract && route.distanceKm > 0.0 &&
            route.transportCostPerUnit > Money(0) &&
            route.railwayChargePerUnit >= route.transportCostPerUnit;
    }
    for (const WarehouseOrder& order : world.getWarehouseNetwork().orders()) {
        if (order.routeId < 0) continue;
        ++remoteOrderCount;
        remoteReceived += order.received;
        remoteTradeProfitable = remoteTradeProfitable &&
            order.profitableTrade &&
            order.destinationUnitPrice >
                order.sourceUnitPrice + order.railwayChargePerUnit;
    }
    const bool remoteTradeObserved = cycle < 104 ||
        (remoteOrderCount > 0 && remoteReceived > Money(0));
    const auto countrySnapshots = world.getCountrySnapshots();
    for (const CountrySnapshot& country : countrySnapshots) {
        const auto finiteNonNegative = [](Money value) {
            return isfinite(value) && value >= Money(0);
        };
        const Money tolerance = Money(1.0e-8);
        constructionPoolHealthy = constructionPoolHealthy &&
            finiteNonNegative(country.industrialConstructionCapacity) &&
            finiteNonNegative(country.industrialConstructionAvailable) &&
            finiteNonNegative(country.baseConstructionSupplement) &&
            finiteNonNegative(country.industrialConstructionUsed) &&
            finiteNonNegative(country.baseConstructionUsed) &&
            country.industrialConstructionAvailable <=
                country.industrialConstructionCapacity + tolerance &&
            country.industrialConstructionUsed <=
                country.industrialConstructionCapacity + tolerance &&
            country.baseConstructionUsed <=
                country.baseConstructionSupplement + tolerance &&
            country.industrialConstructionUsed +
                    country.baseConstructionUsed <=
                country.industrialConstructionCapacity +
                    country.baseConstructionSupplement + tolerance;
    }
    const bool logisticsHealthy = audit.valid && topologyComplete &&
        inventoryBalanced && inventoryReviewCadence &&
        demandSignalsFinite && railRouteContract &&
        remoteTradeProfitable && remoteTradeObserved &&
        constructionPoolHealthy;

    // GDP volatility, idle weeks, and population retention remain visible as
    // diagnostics. The documented report gate is topology, accounting,
    // finite/positive 52-week GDP, and the logistics contract itself.
    const bool healthy = logisticsHealthy && allGdpFinite &&
        (cycle == 0 || allGdpPositive);

    output << "{\n";
    output << "  \"scenario\": "
           << (world.isDebugFiveMarketScenario() ? "\"five_markets\"" : "\"country_markets\"") << ",\n";
    output << "  \"cycle\": " << cycle << ",\n";
    output << "  \"checks\": {\n";
    output << "    \"healthy\": " << (healthy ? "true" : "false") << ",\n";
    output << "    \"logisticsHealthy\": "
           << (logisticsHealthy ? "true" : "false") << ",\n";
    output << "    \"marketCount\": " << world.getMarketCount() << ",\n";
    output << "    \"completeTopology\": "
           << (topologyComplete ? "true" : "false") << ",\n";
    output << "    \"inventoryBalanced\": "
           << (inventoryBalanced ? "true" : "false") << ",\n";
    output << "    \"allGdpFinite\": "
           << (allGdpFinite ? "true" : "false") << ",\n";
    output << "    \"allGdpPositive\": "
           << (allGdpPositive ? "true" : "false") << ",\n";
    output << "    \"gdpStable\": "
           << (gdpStable ? "true" : "false") << ",\n";
    output << "    \"productionContinuous\": "
           << (productionContinuous ? "true" : "false") << ",\n";
    output << "    \"populationRetained\": "
           << (populationRetained ? "true" : "false") << ",\n";
    output << "    \"inventoryReviewAtMostWeekly\": "
           << (inventoryReviewCadence ? "true" : "false") << ",\n";
    output << "    \"demandWindowWeeks\": "
           << DEMAND_AVERAGE_WEEKS << ",\n";
    output << "    \"demandSignalsFinite\": "
           << (demandSignalsFinite ? "true" : "false") << ",\n";
    output << "    \"railRouteContract\": "
           << (railRouteContract ? "true" : "false") << ",\n";
    output << "    \"remoteTradeProfitable\": "
           << (remoteTradeProfitable ? "true" : "false") << ",\n";
    output << "    \"remoteTradeObserved\": "
           << (remoteTradeObserved ? "true" : "false") << ",\n";
    output << "    \"constructionPoolHealthy\": "
           << (constructionPoolHealthy ? "true" : "false") << ",\n";
    output << "    \"warehouseAudit\": "
           << (audit.valid ? "true" : "false") << ",\n";
    output << "    \"warehouseAuditMessage\": ";
    WriteString(output, audit.message);
    output << "\n  },\n";
    output << "  \"transport\": {\n";
    output << "    \"routes\": " << transport.routes.size() << ",\n";
    output << "    \"activeOrders\": " << transport.orders.size() << ",\n";
    output << "    \"shipments\": " << transport.shipments.size() << ",\n";
    output << "    \"usedCapacity\": ";
    WriteMoney(output, transport.totalUsedCapacity);
    output << ",\n    \"queuedVolume\": ";
    WriteMoney(output, transport.totalQueuedVolume);
    output << ",\n    \"escrow\": ";
    WriteMoney(output, transport.totalEscrow);
    output << ",\n    \"profitableRoutes\": "
           << transport.profitableRoutes;
    output << ",\n    \"unprofitableRoutes\": "
           << transport.unprofitableRoutes;
    output << ",\n    \"railwayBlockedRoutes\": "
           << transport.railwayBlockedRoutes;
    output << ",\n    \"railwayRevenue\": ";
    WriteMoney(output, transport.totalRailwayRevenue);
    output << ",\n    \"warehouseProfit\": ";
    WriteMoney(output, transport.totalWarehouseProfit);
    output << ",\n    \"remoteOrders\": " << remoteOrderCount;
    output << ",\n    \"remoteReceived\": ";
    WriteMoney(output, remoteReceived);
    output << ",\n    \"completedTradeFlows\": "
           << transport.tradeFlows.size();
    output << "\n  },\n";
    output << "  \"countries\": [\n";
    for (std::size_t index = 0; index < countrySnapshots.size(); ++index) {
        const CountrySnapshot& country = countrySnapshots[index];
        output << "    {\"id\": " << country.countryId
               << ", \"code\": ";
        WriteString(output, country.countryCode);
        output << ", \"name\": ";
        WriteString(output, country.name);
        output << ", \"provinceCount\": " << country.provinceIds.size()
               << ", \"treasury\": ";
        WriteMoney(output, country.treasury);
        output << ", \"reservedConstructionBudget\": ";
        WriteMoney(output, country.reservedConstructionBudget);
        output << ", \"availableTreasury\": ";
        WriteMoney(output, country.availableTreasury);
        output << ", \"constructionProjects\": "
               << country.constructionProjects.size() << "}"
               << (index + 1 == countrySnapshots.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"tradeFlows\": [\n";
    for (std::size_t index = 0; index < transport.tradeFlows.size(); ++index) {
        const TradeFlowSnapshot& flow = transport.tradeFlows[index];
        output << "    {\"source\": " << flow.sourceWarehouseId
               << ", \"destination\": " << flow.destinationWarehouseId
               << ", \"good\": " << flow.goodIndex
               << ", \"quantity\": ";
        WriteMoney(output, flow.quantity);
        output << "}" << (index + 1 == transport.tradeFlows.size()
                                  ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"routeLedger\": [\n";
    for (std::size_t index = 0; index < transport.routes.size(); ++index) {
        const RouteSnapshot& route = transport.routes[index];
        output << "    {\"id\": " << route.id
               << ", \"source\": " << route.sourceWarehouseId
               << ", \"destination\": " << route.destinationWarehouseId
               << ", \"good\": " << route.goodIndex
               << ", \"distanceKm\": " << std::setprecision(12)
               << route.distanceKm << ", \"sourcePrice\": ";
        WriteMoney(output, route.sourceUnitPrice);
        output << ", \"destinationPrice\": ";
        WriteMoney(output, route.destinationUnitPrice);
        output << ", \"transportCost\": ";
        WriteMoney(output, route.transportCostPerUnit);
        output << ", \"transportCapacityPerUnit\": ";
        WriteMoney(output, route.transportCapacityPerUnit);
        output << ", \"railwayCapacityPrice\": ";
        WriteMoney(output, route.railwayCapacityPricePerUnit);
        output << ", \"railwayCharge\": ";
        WriteMoney(output, route.railwayChargePerUnit);
        output << ", \"railwayMarkup\": ";
        WriteMoney(output, route.railwayMarkupPerUnit);
        output << ", \"warehouseMargin\": ";
        WriteMoney(output, route.warehouseMarginPerUnit);
        output << ", \"contractPrice\": ";
        WriteMoney(output, route.unitPrice);
        output << ", \"railwayAvailable\": "
               << (route.railwayAvailable ? "true" : "false")
               << ", \"profitable\": "
               << (route.profitable ? "true" : "false")
               << "}" << (index + 1 == transport.routes.size()
                               ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"orderLedger\": [\n";
    const auto& orders = world.getWarehouseNetwork().orders();
    for (std::size_t index = 0; index < orders.size(); ++index) {
        const WarehouseOrder& order = orders[index];
        output << "    {\"id\": " << order.id
               << ", \"root\": " << order.rootDemandId
               << ", \"parent\": " << order.parentOrderId
               << ", \"kind\": " << static_cast<int>(order.kind)
               << ", \"status\": " << static_cast<int>(order.status)
               << ", \"created\": " << order.createdCycle
               << ", \"eligible\": " << order.eligibleCycle
               << ", \"buyer\": " << order.buyerWarehouseId
               << ", \"seller\": " << order.sellerWarehouseId
               << ", \"route\": " << order.routeId
               << ", \"building\": " << order.buildingType
               << ", \"good\": " << order.goodIndex
               << ", \"requested\": ";
        WriteMoney(output, order.requested);
        output << ", \"accepted\": ";
        WriteMoney(output, order.accepted);
        output << ", \"reserved\": ";
        WriteMoney(output, order.reserved);
        output << ", \"shipped\": ";
        WriteMoney(output, order.shipped);
        output << ", \"received\": ";
        WriteMoney(output, order.received);
        output << ", \"escrowed\": ";
        WriteMoney(output, order.escrowed);
        output << ", \"sourcePrice\": ";
        WriteMoney(output, order.sourceUnitPrice);
        output << ", \"destinationPrice\": ";
        WriteMoney(output, order.destinationUnitPrice);
        output << ", \"transportCost\": ";
        WriteMoney(output, order.transportCostPerUnit);
        output << ", \"transportCapacityPerUnit\": ";
        WriteMoney(output, order.transportCapacityPerUnit);
        output << ", \"railwayCapacityPrice\": ";
        WriteMoney(output, order.railwayCapacityPricePerUnit);
        output << ", \"railwayCharge\": ";
        WriteMoney(output, order.railwayChargePerUnit);
        output << ", \"warehouseMargin\": ";
        WriteMoney(output, order.warehouseMarginPerUnit);
        output << ", \"profitableTrade\": "
               << (order.profitableTrade ? "true" : "false");
        output << ", \"planned\": "
               << (order.productionPlanned ? "true" : "false")
               << "}" << (index + 1 == orders.size() ? "\n" : ",\n");
    }
    output << "  ],\n";
    output << "  \"markets\": [\n";

    for (int marketIndex = 0; marketIndex < world.getMarketCount();
         ++marketIndex) {
        const LocalMarket& market = world.getMarket(marketIndex);
        const MarketFlowSnapshot& flow = market.getLatestFlow();
        output << "    {\n";
        output << "      \"index\": " << marketIndex << ",\n";
        output << "      \"id\": " << market.getMarketId() << ",\n";
        output << "      \"name\": ";
        WriteString(output, market.getMarketName());
        output << ",\n      \"weeklyGdp\": ";
        WriteMoney(output, market.getWeeklyGDP());
        output << ",\n      \"annualizedGdp\": ";
        WriteMoney(output, market.getGDP());
        const MarketRegressionMetrics& metrics =
            regressionMetrics[static_cast<std::size_t>(marketIndex)];
        output << ",\n      \"gdpCv104\": "
               << std::setprecision(12) << metrics.gdpCv104;
        output << ",\n      \"nearZeroGdpWeeks104\": "
               << metrics.nearZeroGdpWeeks104;
        output << ",\n      \"grossOutputValue\": ";
        WriteMoney(output, flow.grossOutputValue);
        output << ",\n      \"intermediateCost\": ";
        WriteMoney(output, flow.intermediateCost);
        output << ",\n      \"consumerValue\": ";
        WriteMoney(output, flow.consumerValue);
        output << ",\n      \"constructionValue\": ";
        WriteMoney(output, flow.constructionValue);
        output << ",\n      \"investmentPool\": ";
        WriteMoney(output, market.getInvestmentPool());
        output << ",\n      \"totalMoneySupply\": ";
        WriteMoney(output, market.getTotalMoneySupply());
        output << ",\n      \"totalDebt\": ";
        WriteMoney(output, market.getTotalDebt());
        output << ",\n      \"satisfaction\": "
               << std::setprecision(12) << market.getSatisfaction();
        output << ",\n      \"instantSatisfaction\": "
               << std::setprecision(12)
               << market.getInstantSatisfaction();
        output << ",\n      \"population\": "
               << std::setprecision(12) << market.getPopulation();
        output << ",\n      \"populationRetention\": "
               << std::setprecision(12) << metrics.populationRetention;
        output << ",\n      \"subsistencePopulation\": "
               << std::setprecision(12) << market.getSubsistencePop();
        const MarketSnapshot marketSnapshot = market.getSnapshot();
        output << ",\n      \"inventoryReview\": {\"lastCycle\": "
               << marketSnapshot.lastInventoryReviewCycle
               << ", \"count\": " << marketSnapshot.inventoryReviewCount
               << ", \"suppressedDuplicates\": "
               << marketSnapshot.suppressedInventoryReviews << "}";
        output << ",\n      \"gdpHistory\": [";
        const auto& gdpHistory = market.getGDPHistory();
        for (std::size_t historyIndex = 0;
             historyIndex < gdpHistory.size(); ++historyIndex) {
            WriteMoney(output, gdpHistory[historyIndex]);
            if (historyIndex + 1 < gdpHistory.size()) output << ", ";
        }
        output << "],\n      \"populationHistory\": [";
        const auto& populationHistory = market.getPopulationHistory();
        for (std::size_t historyIndex = 0;
             historyIndex < populationHistory.size(); ++historyIndex) {
            output << std::setprecision(12)
                   << populationHistory[historyIndex];
            if (historyIndex + 1 < populationHistory.size()) output << ", ";
        }
        output << "]";
        output << ",\n      \"classCash\": [";
        for (int classIndex = 0; classIndex < CLASS_COUNT; ++classIndex) {
            WriteMoney(output, market.getClassCash(classIndex));
            if (classIndex + 1 < CLASS_COUNT) output << ", ";
        }
        output << "],\n";
        output << "      \"inventoryBalanced\": "
               << (flow.inventoryBalanced ? "true" : "false") << ",\n";
        if (market.getFiscalCountry() != nullptr) {
            const CountrySnapshot countrySnapshot =
                world.getCountrySnapshot(market.getFiscalCountry()->getId());
            output << "      \"constructionPool\": {\"industrialCapacity\": ";
            WriteMoney(output, countrySnapshot.industrialConstructionCapacity);
            output << ", \"industrialAvailable\": ";
            WriteMoney(output, countrySnapshot.industrialConstructionAvailable);
            output << ", \"baseSupplement\": ";
            WriteMoney(output, countrySnapshot.baseConstructionSupplement);
            output << ", \"industrialUsed\": ";
            WriteMoney(output, countrySnapshot.industrialConstructionUsed);
            output << ", \"baseUsed\": ";
            WriteMoney(output, countrySnapshot.baseConstructionUsed);
            output << ", \"baseExpenditure\": ";
            WriteMoney(output, countrySnapshot.baseConstructionExpenditure);
            output << "},\n";
        }
        output << "      \"buildings\": [\n";
        for (int type = 0; type < TYPE_COUNT; ++type) {
            const BuildingTemplate& building =
                market.getBuildingTemplates()[type];
            output << "        {\"type\": " << type << ", \"name\": ";
            WriteString(output, buildingTypeNames[type]);
            output << ", \"count\": " << market.getBuildingCounts()[type]
                   << ", \"cash\": ";
            WriteMoney(output, market.getCashPools()[type]);
            output << ", \"employmentTargetRate\": "
                   << std::setprecision(12)
                   << market.getEmploymentRatio()[type]
                   << ", \"wage\": ";
            WriteMoney(output, market.getWages()[type]);
            output << ", \"baseWage\": ";
            WriteMoney(output, market.getWages()[type]);
            output << ", \"bonusWage\": ";
            WriteMoney(output, market.getBonuses()[type]);
            output << ", \"effectiveWage\": ";
            WriteMoney(output, market.getWages()[type] +
                       market.getBonuses()[type]);
            output << ", \"profitRate\": " << std::setprecision(12)
                   << market.getAvgProfitRates()[type]
                   << ", \"smoothedProfitRate\": "
                   << market.getSmoothedProfitRate()[type];
            output << ", \"employment\": " << std::setprecision(12)
                   << market.getActualEmployment()[type]
                   << ", \"employmentRate\": "
                   << market.getActualEmploymentRate()[type]
                   << ", \"supplyRatio\": "
                   << market.getCurrentSupplyRatio()[type]
                   << ", \"materialAvailability\": "
                   << market.getMaterialAvailability()[type]
                   << ", \"capacityUtilization\": "
                   << market.getCapacityUtilization()[type]
                   << ", \"fundingAvailability\": "
                   << market.getFundingAvailability()[type]
                   << ", \"staffedCapacity\": ";
            WriteMoney(output, market.getStaffedCapacity()[type]);
            output << ", \"productionTarget\": ";
            WriteMoney(output, market.getProductionTarget()[type]);
            output << ", \"output\": ";
            WriteMoney(output, market.getLatestBuildingOutput()[type]);
            output << ", \"pendingProduction\": ";
            const Money pending = building.outputGood < 0
                ? Money(0)
                : world.getWarehouseNetwork().pendingProduction(
                      market.getMarketId(), type, building.outputGood);
            WriteMoney(output, pending);
            output << ", \"productionCommand\": ";
            const Money command = building.outputGood < 0
                ? Money(0)
                : world.getWarehouseNetwork().productionCommand(
                      market.getMarketId(), type, building.outputGood);
            WriteMoney(output, command);
            output << ", \"demand52\": ";
            const Money demand52 = building.outputGood < 0
                ? Money(0)
                : world.getWarehouseNetwork().productionDemand52(
                      market.getMarketId(), type, building.outputGood);
            WriteMoney(output, demand52);
            output << ", \"inputs\": [";
            bool firstInput = true;
            for (int good = 0; good < NUM_GOODS; ++good) {
                if (building.inputs[good] <= 0.0) continue;
                if (!firstInput) output << ", ";
                firstInput = false;
                const InventoryState& input =
                    market.getWarehouse().buildingInput(type, good);
                output << "{\"good\": " << good << ", \"onHand\": ";
                WriteMoney(output, input.onHand);
                output << ", \"reserved\": ";
                WriteMoney(output, input.reserved);
                output << ", \"confirmedInbound\": ";
                WriteMoney(output, input.confirmedInbound);
                output << ", \"physicalInTransit\": ";
                WriteMoney(output, input.physicalInTransit);
                output << ", \"backlog\": ";
                WriteMoney(output, input.backlog);
                output << "}";
            }
            output << "]}"
                   << (type + 1 == TYPE_COUNT ? "\n" : ",\n");
        }
        output << "      ],\n";
        output << "      \"goods\": [\n";
        const auto& rawConsumerTarget =
            market.getLatestRawConsumerTarget();
        const auto& consumerTarget = market.getLatestConsumerTarget();
        const auto& consumerActual = market.getLatestConsumerActual();
        for (int good = 0; good < NUM_GOODS; ++good) {
            output << "        {\"index\": " << good << ", \"name\": ";
            WriteString(output, commodityNames[good]);
            output << ", \"opening\": ";
            WriteMoney(output, flow.openingStock[good]);
            output << ", \"produced\": ";
            WriteMoney(output, flow.production[good]);
            output << ", \"received\": ";
            WriteMoney(output, flow.received[good]);
            output << ", \"dispatched\": ";
            WriteMoney(output, flow.dispatched[good]);
            output << ", \"toBuilding\": ";
            WriteMoney(output, flow.movedToBuilding[good]);
            output << ", \"buildingConsumed\": ";
            WriteMoney(output, flow.buildingConsumed[good]);
            output << ", \"directProductionUse\": ";
            WriteMoney(output, flow.directProductionUse[good]);
            output << ", \"consumerUse\": ";
            WriteMoney(output, flow.consumerUse[good]);
            output << ", \"rawConsumerTarget\": ";
            WriteMoney(output, rawConsumerTarget[good]);
            output << ", \"consumerTarget\": ";
            WriteMoney(output, consumerTarget[good]);
            output << ", \"consumerActual\": ";
            WriteMoney(output, consumerActual[good]);
            output << ", \"constructionUse\": ";
            WriteMoney(output, flow.constructionUse[good]);
            output << ", \"closing\": ";
            WriteMoney(output, flow.closingStock[good]);
            output << ", \"inTransit\": ";
            WriteMoney(output, flow.inTransit[good]);
            output << ", \"residual\": ";
            WriteMoney(output, flow.inventoryResidual[good]);
            const InventoryState& stock = market.getWarehouse().stock(good);
            output << ", \"reserved\": ";
            WriteMoney(output, stock.reserved);
            output << ", \"confirmedInbound\": ";
            WriteMoney(output, stock.confirmedInbound);
            output << ", \"physicalInTransit\": ";
            WriteMoney(output, stock.physicalInTransit);
            output << ", \"backlog\": ";
            WriteMoney(output, stock.backlog);
            output << ", \"weeklyDemand\": ";
            WriteMoney(output, stock.policy.weeklyDemand);
            output << ", \"rawReplenishment\": ";
            WriteMoney(output, stock.lastRawReplenishment);
            output << ", \"plannedReplenishment\": ";
            WriteMoney(output, stock.lastPlannedReplenishment);
            output << "}" << (good + 1 == NUM_GOODS ? "\n" : ",\n");
        }
        output << "      ]\n";
        output << "    }"
               << (marketIndex + 1 == world.getMarketCount()
                       ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    return healthy;
}
