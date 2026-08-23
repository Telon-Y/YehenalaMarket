#include "building_template.h"
#include <algorithm>

Money BuildingTemplate::getUnitCost(const std::array<Money, NUM_GOODS>& prices, Money wageRate) const {
    Money output = Money(std::max(outputRate, 1e-9));
    Money cost = Money(laborPerUnit) * wageRate / output; // 单位产出的劳动成本
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
            bt.workforceShares = {0.50, 0.25, 0.25};
        } else {
            bt.laborPerUnit = 5000.0;
            bt.workforceShares = {0.75, 0.20, 0.05};
        }
        bt.isFinancial = isFin;
        bt.category = isFin ? BuildingCategory::FINANCIAL
                            : BuildingCategory::PRODUCTION;
        bt.moneyMultiplier = mult;
    };

    setTemplate(FARM_GRAIN,    "谷物农场",      0,  50, {});
    setTemplate(FOOD_PROC,     "加工食品厂",     1,  45, {{0, 40.0/45}});
    setTemplate(COTTON,        "棉花种植园",     2,  45, {});
    setTemplate(CLOTHES,       "服装厂",         3, 100, {{2, 60.0/100}});
    setTemplate(LUXURY_CLOTHES,"高档服装厂",     4,  30, {{2, 25.0/30}});
    // Coal cannot be a mandatory input to coal production: once the buffer
    // reaches zero that recursive recipe can never bootstrap again.
    setTemplate(COAL_MINE,     "煤矿",           5,  60, {{8, 15.0/60}});
    setTemplate(IRON_MINE,     "铁矿",           6,  60, {{8, 15.0/60}, {5, 15.0/60}});
    setTemplate(STEEL_MILL,    "炼钢厂",         7,  90, {{6, 60.0/90}, {5, 30.0/90}});
    setTemplate(TOOL_FACT,     "工具厂",         8,  80, {{7, 20.0/80}});
    setTemplate(HOUSING,       "住房",           9,  60, {{7, 5.0/60}, {8, 5.0/60}});
    setTemplate(CONST_DEPT,    "建造部门",       10, 15, {{7, 25.0/15}, {6, 25.0/15}, {8, 20.0/15}});
    temps[CONST_DEPT].category = BuildingCategory::DEVELOPMENT;
    setTemplate(GOLD_MINE,     "金矿",           11, 25, {{8, 15.0/25}, {5, 15.0/25}});
    // 中央银行：铸币权，不参与商业贷款
    setTemplate(BANK,          "中央银行",      -1,  0, {{11, 20.0}}, true, BANK_MONEY_MULTIPLIER);
    setTemplate(FINANCE,       "金融区",         -1,  0, {{8, 5.0}}, true);
    // 工商银行和储蓄银行负责资金中介，不行使铸币权，也不消耗黄金。
    setTemplate(INDUSTRIAL_BANK, "工商银行",    -1,  0, {}, true);
    setTemplate(SAVINGS_BANK,  "储蓄银行",      -1,  0, {}, true);
    // Railways are development buildings that produce transport capacity.
    setTemplate(RAILWAY,       "铁路枢纽",       TRANSPORT_CAPACITY_GOOD_INDEX,
                 RAIL_CAPACITY_PER_LEVEL, {{7, 0.02}, {8, 0.01}});
    temps[RAILWAY].category = BuildingCategory::DEVELOPMENT;
    temps[RAILWAY].laborPerUnit = 1000.0;

    return temps;
}
