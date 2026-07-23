#include "csv_writer.h"
#include "market_simulation.h"

void writeAllCsvs(MarketSimulation& sim) {
    // 价格
    {
        std::string header = "Step";
        for (const auto& name : commodityNames) header += "," + name;
        writeCsv("prices.csv", header, sim.priceHist);
    }
    // 建筑数量
    {
        std::string header = "Step";
        for (const auto& name : buildingTypeNames) header += "," + name;
        writeCsvBuilding("buildings.csv", header, sim.buildingHist);
    }
    // 产量
    {
        std::string header = "Step";
        for (const auto& name : commodityNames) header += "," + name + "产量";
        writeCsv("outputs.csv", header, sim.outputHist);
    }
    // 利润率
    {
        std::string header = "Step";
        for (const auto& name : buildingTypeNames) header += "," + name + "利润率";
        writeCsvBuilding("profitRate.csv", header, sim.profitRateHist);
    }
    // GDP
    {
        std::ofstream f("gdp.csv");
        f << "\xEF\xBB\xBFStep,GDP\n";
        for (size_t t = 0; t < sim.gdpHist.size(); ++t)
            f << t << "," << sim.gdpHist[t] << "\n";
    }
    // 现金池
    {
        std::string header = "Step";
        for (const auto& name : buildingTypeNames) header += "," + name;
        writeCsvBuilding("cashPool.csv", header, sim.cashPoolHist);
    }
    // 人口
    {
        std::ofstream f("population.csv");
        f << "\xEF\xBB\xBFStep,Population\n";
        for (size_t t = 0; t < sim.populationHist.size(); ++t)
            f << t << "," << sim.populationHist[t] << "\n";
    }
}