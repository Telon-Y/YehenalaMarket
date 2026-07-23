#include "building_template.h"
#include <algorithm>

double BuildingTemplate::getUnitCost(const std::array<double, NUM_GOODS>& prices, double wageRate) const {
    double cost = laborPerUnit * wageRate;
    for (int g = 0; g < NUM_GOODS; ++g)
        cost += prices[g] * inputs[g];
    return cost;
}

double BuildingTemplate::getProfitFactor(const std::array<double, NUM_GOODS>& prices, double wageRate) const {
    double unitCost = getUnitCost(prices, wageRate);
    double outputPrice = prices[outputGood];
    if (unitCost < 1e-6) return 1.0;
    double margin = outputPrice - unitCost;
    double ratio = margin / unitCost;
    double factor = std::max(0.2, 1.0 + ratio * 2.0);
    return std::min(factor, 1.0);
}

std::vector<BuildingTemplate> createBuildingTemplates() {
    std::vector<BuildingTemplate> temps;
    temps.resize(TYPE_COUNT);

    auto setTemplate = [&](BuildingType t, const std::string& name, int outIdx, double rate,
                           std::initializer_list<std::pair<int, double>> in) {
        BuildingTemplate& bt = temps[t];
        bt.name = name;
        bt.outputGood = outIdx;
        bt.outputRate = rate;
        bt.inputs.fill(0.0);
        for (auto [gi, amt] : in)
            bt.inputs[gi] = amt;
        bt.laborPerUnit = 5000.0 / rate;
    };

    // 使用索引而非字符串，因为 constants.h 已经定义了索引
    setTemplate(FARM_GRAIN,    "谷物农场",   0,  50, {});
    setTemplate(FOOD_PROC,     "加工食品厂",  1,  45, {{0, 40.0/45}});
    setTemplate(COTTON,        "棉花种植园",  2,  45, {});
    setTemplate(CLOTHES,       "服装厂",      3, 100, {{2, 60.0/100}});
    setTemplate(LUXURY_CLOTHES,"高档服装厂",  4,  30, {{2, 25.0/30}});
    setTemplate(COAL_MINE,     "煤矿",        5,  60, {{8, 15.0/60}, {5, 15.0/60}});
    setTemplate(IRON_MINE,     "铁矿",        6,  60, {{8, 15.0/60}, {5, 15.0/60}});
    setTemplate(STEEL_MILL,    "炼钢厂",      7,  90, {{6, 60.0/90}, {5, 30.0/90}});
    setTemplate(TOOL_FACT,     "工具厂",      8,  80, {{7, 20.0/80}});
    setTemplate(HOUSING,       "住房",        9,  60, {{7, 5.0/60}, {8, 5.0/60}});
    setTemplate(CONST_DEPT,    "建造部门",    10, 15, {{7, 25.0/15}, {6, 25.0/15}, {8, 20.0/15}});

    return temps;
}