#include "building_template.h"
#include <algorithm>

Money BuildingTemplate::getUnitCost(const std::array<Money, NUM_GOODS>& prices, Money wageRate) const {
    Money cost = Money(laborPerUnit) * wageRate; // 劳动成本
    for (int g = 0; g < NUM_GOODS; ++g)
        cost += prices[g] * Money(inputs[g]);    // 原料成本
    return cost;
}

Money BuildingTemplate::getProfitFactor(const std::array<Money, NUM_GOODS>& prices, Money wageRate) const {
    if (isFinancial) return Money(1.0);
    Money unitCost = getUnitCost(prices, wageRate);
    Money outputPrice = prices[outputGood];
    if (unitCost < Money(1e-6)) return Money(1.0);
    Money margin = outputPrice - unitCost;
    Money ratio = margin / unitCost;
    Money factor = std::max(Money(0.2), Money(1.0) + ratio * Money(2.0));
    return std::min(factor, Money(1.0));
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
        if (isFin) {
            bt.laborPerUnit = 1000.0;
        } else {
            bt.laborPerUnit = 5000.0;
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
    setTemplate(GOLD_MINE,     "金矿",           11, 25, {{8, 15.0/25}, {5, 15.0/25}});
    setTemplate(BANK,          "银行",          -1,  0, {{11, 20.0}}, true, BANK_MONEY_MULTIPLIER);
    setTemplate(FINANCE,       "金融区",         -1,  0, {{8, 5.0}}, true);

    return temps;
}