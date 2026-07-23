#include <iostream>
#include <windows.h>
#include "market_simulation.h"
#include "csv_writer.h"

int main() {
    SetConsoleOutputCP(CP_UTF8);
    MarketSimulation sim;

    std::cout << "模拟开始，共 " << TOTAL_STEPS << " 周...\n" << std::flush;
    for (int t = 0; t < TOTAL_STEPS; ++t) {
        sim.step();
        if (t % AI_INTERVAL == 0) sim.aiBuild();
        if (t % 52 == 0) std::cout << "周数: " << t << "/" << TOTAL_STEPS << "     \r" << std::flush;
    }
    std::cout << "\n写入CSV...\n" << std::flush;
    writeAllCsvs(sim);
    std::cout << "完成。\n";
    return 0;
}