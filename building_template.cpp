#include "building_template.h"
#include <algorithm>

double BuildingTemplate::getUnitCost(const std::array<double, NUM_GOODS>& prices, double wageRate) const {
    double cost = laborPerUnit * wageRate;
    for (int g = 0; g < NUM_GOODS; ++g)
        cost += prices[g] * inputs[g];
    return cost;
}

double BuildingTemplate::getProfitFactor(const std::array<double, NUM_GOODS>& prices, double wageRate) const {
    if (isFinancial) return 1.0;
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
                           std::initializer_list<std::pair<int, double>> in,
                           bool isFin = false, double mult = 1.0) {
        BuildingTemplate& bt = temps[t];
        bt.name = name;
        bt.outputGood = outIdx;
        bt.outputRate = rate;
        bt.inputs.fill(0.0);
        for (auto [gi, amt] : in)
            bt.inputs[gi] = amt;
        // ===== 修改 1：劳动力固定 =====
        // 所有实业建筑（非金融）每级固定雇佣 5000 人
        // 金融建筑（银行、金融区）每级 1000 人
        if (isFin) {
            bt.laborPerUnit = 1000.0;      // 银行/金融区：1000 人/级
        } else {
            bt.laborPerUnit = 5000.0;      // 实业建筑：5000 人/级
        }
        bt.isFinancial = isFin;
        bt.moneyMultiplier = mult;
    };

    setTemplate(FARM_GRAIN,    "谷物农场",      0,  50, {});
    setTemplate(FOOD_PROC,     "加工食品厂",     1,  45, {{0, 40.0/45}});
    setTemplate(COTTON,        "棉花种植园",     2,  45, {});
    setTemplate(CLOTHES,       "服装厂",         3, 100, {{2, 60.0/100}});
    setTemplate(LUXURY_CLOTHES,"高档服装厂",     4,  30, {{2, 25.0/30}});
    setTemplate(COAL_MINE,     "煤矿",           5,  60, {{8, 15.0/60}, {5, 15.0/60}});
    setTemplate(IRON_MINE,     "铁矿",           6,  60, {{8, 15.0/60}, {5, 15.0/60}});
    setTemplate(STEEL_MILL,    "炼钢厂",         7,  90, {{6, 60.0/90}, {5, 30.0/90}});
    setTemplate(TOOL_FACT,     "工具厂",         8,  80, {{7, 20.0/80}});
    setTemplate(HOUSING,       "住房",           9,  60, {{7, 5.0/60}, {8, 5.0/60}});
    setTemplate(CONST_DEPT,    "建造部门",       10, 15, {{7, 25.0/15}, {6, 25.0/15}, {8, 20.0/15}});
    setTemplate(GOLD_MINE,     "金矿",           11, 40, {{8, 15.0/40}, {5, 5.0/40}});
    setTemplate(BANK,          "银行",          -1,  0, {{11, 20.0}}, true, BANK_MONEY_MULTIPLIER);
    setTemplate(FINANCE,       "金融区",         -1,  0, {{8, 5.0}}, true);

    return temps;
}