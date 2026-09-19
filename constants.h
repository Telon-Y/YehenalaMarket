// constants.h
#pragma once
#include <vector>
#include <string>
#include <array>
#include "decimal.h"

// 模拟参数
// Expansion planning is intentionally less frequent than the weekly market
// cycle. Re-evaluating every province after every accepted project stalls the
// render thread without materially improving a quarterly-scale decision.
constexpr int AI_INTERVAL = 4;

// Commodity catalog
constexpr int NUM_GOODS = 13;

// Building catalog
constexpr int TYPE_COUNT = 17;

// Logistics pricing is quoted once per weekly inventory review. Railways sell
// transport capacity at labor/material cost. Distance changes the amount
// of capacity consumed by a shipment, rather than the price of one capacity unit.
constexpr double RAILWAY_MARKUP_RATE = 0.15; // legacy snapshot field, unused
constexpr double WAREHOUSE_MARGIN_SHARE = 0.35; // legacy snapshot field, unused
// Intended transit rate. Nothing reads it: addRailRoute() pins every route to
// one cycle, so this constant documents the model that is not implemented yet.
constexpr double RAIL_KM_PER_TRANSIT_WEEK = 500.0;
// 100 units of cargo over 100 km consume one unit of railway capacity.
constexpr double RAIL_DISTANCE_CAPACITY_COEFFICIENT = 0.01;
constexpr double RAIL_CAPACITY_PER_LEVEL = 20.0;
constexpr int INITIAL_RAILWAY_LEVELS = 10;
constexpr double PRODUCTION_BONUS_GAP_PREMIUM = 0.25;
constexpr double PRODUCTION_BONUS_SMOOTHING = 0.20;
constexpr double MAX_BONUS_TO_BASE_WAGE = 2.0;
constexpr int TRANSPORT_CAPACITY_GOOD_INDEX = 12;
constexpr int DEMAND_AVERAGE_WEEKS = 52;
constexpr int CONSTRUCTION_BACKLOG_DRAIN_WEEKS = 4;
constexpr int INVENTORY_TARGET_COVERAGE_WEEKS = 10;
constexpr int INVENTORY_REVIEW_INTERVAL_WEEKS = 1;
constexpr int INVENTORY_SAFETY_WEEKS = 2;
constexpr int PRODUCTION_INVENTORY_RECOVERY_WEEKS = 4;
constexpr double EMPLOYMENT_WEEKLY_HIRE_CAPACITY_SHARE = 0.05;
constexpr double EMPLOYMENT_WEEKLY_LAYOFF_CURRENT_SHARE = 0.02;
constexpr int COUNTRY_BASE_CONSTRUCTION_CAPACITY = 10;
constexpr int CONSTRUCTION_MAX_PER_BUILDING_PER_CYCLE = 30;
constexpr int CONSTRUCTION_INPUT_SAFETY_WEEKS = 2;
// Recursive production forecasts include a small operating margin so a
// balanced recipe graph can absorb rounding, transit, and startup losses
// without starving a downstream construction department.
constexpr double PRODUCTION_FORECAST_SAFETY_FACTOR = 1.10;

// 消费组数量
constexpr int GROUP_COUNT = 4;

// 商品名称列表
inline const std::vector<std::string> commodityNames = {
    "谷物", "加工食品", "织物", "服装", "高档服装",
    "煤炭", "铁", "钢", "工具", "住房", "建造力",
    "贵金属", "运力"   // 非库存运输服务
};

// 建筑类型名称
inline const std::vector<std::string> buildingTypeNames = {
    "谷物农场", "加工食品厂", "棉花种植园", "服装厂", "高档服装厂",
    "煤矿", "铁矿", "炼钢厂", "工具厂", "住房", "建造部门",
    "金矿", "中央银行", "金融区", "工商银行", "储蓄银行",
    "铁路枢纽"   // Warehouse-trade intermediary
};

// Building construction costs
inline const std::vector<double> buildingCost = {
    200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100,
    600, 800, 800, 800, 800, 500   // 金矿、金融建筑、铁路枢纽
};

// Initial reference prices
inline const std::array<double, NUM_GOODS> referencePrice = {
    2400.0, 4000.0, 5000.0, 12000.0, 40000.0,
    4000.0, 4000.0, 8000.0, 4000.0, 1600.0, 24000.0,
    8000.0, 1.0
};

// 价格抑制系数 (加强版：默认0.30，原为0.15)
inline const std::array<double, NUM_GOODS> priceSuppressBase = [](){
    std::array<double, NUM_GOODS> arr;
    arr.fill(0.30);          // 默认：从0.15提高至0.30
    arr[1] = 0.36;           // 加工食品
    arr[4] = 0.24;           // 高档服装
    arr[10] = 0.50;          // 建造力
    arr[11] = 0.20;          // 贵金属
    arr[12] = 0.0;            // transport service is not consumer demand
    return arr;
}();

// 需求表（每10万人）。数值已按当前初始产能校准。
inline const std::array<std::array<double, GROUP_COUNT>, 3> demandTable = {{
    {1.95, 10.5, 0.0, 1.0},      // 财富5 (劳工)
    {2.05, 10.5, 0.35, 3.7},     // 财富10 (工程师)
    {0.0,  10.5, 6.1, 6.5}       // 财富20 (资本家)
}};

// 消费组对应商品
inline const std::vector<std::vector<int>> groupGoods = {
    {3},        // 简朴衣物 -> 服装
    {0, 1},     // 基础食物 -> 谷物、加工食品
    {4},        // 标准衣物 -> 高档服装
    {9}         // 住宅 -> 住房
};

// 使用价值系数 [商品][消费组]
inline const std::array<std::array<double, GROUP_COUNT>, NUM_GOODS> valueCoeff = [](){
    std::array<std::array<double, GROUP_COUNT>, NUM_GOODS> arr{};
    for (int i = 0; i < NUM_GOODS; ++i)
        for (int g = 0; g < GROUP_COUNT; ++g)
            arr[i][g] = 0.0;

    arr[0][1] = 1.0;   // 谷物 -> 基础食物
    arr[1][1] = 1.5;   // 加工食品 -> 基础食物
    arr[3][0] = 1.0;   // 服装 -> 简朴衣物
    arr[4][2] = 1.0;   // 高档服装 -> 标准衣物
    arr[9][3] = 1.0;   // 住房 -> 住宅
    return arr;
}();

// Building type enum
enum BuildingType {
    FARM_GRAIN, FOOD_PROC, COTTON, CLOTHES, LUXURY_CLOTHES,
    COAL_MINE, IRON_MINE, STEEL_MILL, TOOL_FACT, HOUSING, CONST_DEPT,
    GOLD_MINE, BANK, FINANCE, INDUSTRIAL_BANK, SAVINGS_BANK, RAILWAY
};

// 消费组枚举
enum ConsGroup {
    GRP_SIMPLE_CLOTHES = 0,
    GRP_BASIC_FOOD,
    GRP_STANDARD_CLOTHES,
    GRP_HOUSING
};

// Population class enum
enum PopClass {
    LABORER = 0,
    ENGINEER,
    CAPITALIST,
    CLASS_COUNT
};

// ******** 新增：建筑所有者枚举 ********
enum OwnerType {
    OWNER_GOVERNMENT = 0,   // 政府（玩家）
    OWNER_INITIAL,          // 初始私有资本家
    OWNER_FINANCE,          // 金融区
    OWNER_COUNT
};

// Financial constants
constexpr double BANK_MONEY_MULTIPLIER = 2.0;

// ===== 金融中介参数 =====
// The financial district is paid out of the interest a borrower already hands
// over, so the split is money-neutral: it moves income between two institutions
// rather than creating any.
constexpr double FINANCE_INTERMEDIATION_FEE_SHARE = 0.25;
// The savings bank earns a deposit spread on the funds it intermediates and
// passes part of it on to households as deposit interest. The remaining part is
// retained as bank capital, which is what lets the institution grow.
constexpr double SAVINGS_DEPOSIT_RATE_PER_WEEK = 0.0005;
constexpr double SAVINGS_PASS_THROUGH_SHARE = 0.60;

// ===== 劳工负债挂钩系数 =====
constexpr double LABOR_DEBT_SCALE = 1e9;

// ===== 贷款系统常量 =====
constexpr double BANK_LOAN_UNIT_VALUE = 500000.0;          // 每单位贷款 = 50万（修改）
constexpr int    BANK_MAX_LOAN_PER_LEVEL = 50;              // 每级银行最多50单位
constexpr double BANK_LOAN_CAPACITY_PER_LEVEL =
    BANK_MAX_LOAN_PER_LEVEL * BANK_LOAN_UNIT_VALUE;
constexpr double INVEST_LOAN_TRIGGER_RATIO = 2.0;           // 投资池缺口触发倍数(200%)
constexpr int    INVEST_LOAN_TERM_WEEKS = 260;              // 5年 = 260周
constexpr double BANK_MAX_SYSTEM_CREDIT_RATIO = 0.25;       // 信贷余额不超过初始货币量25%

// ===== 新增：贷款利息常量 =====
constexpr double LOAN_INTEREST_RATE_ANNUAL = 0.05;         // 年利率 5%
constexpr double LOAN_INTEREST_PER_WEEK = LOAN_INTEREST_RATE_ANNUAL / 52.0;

// ===== 黄金固定平价 =====
constexpr double GOLD_FIXED_PRICE = 10000.0;               // 1黄金 = 10,000货币

// ===== 劳动人口系数（男0.25 + 女0.15 = 0.40） =====
constexpr double LABOR_FORCE_PARTICIPATION = 0.40;

// ===== 投资池回流：劳动力资金池作为终点资金池 =====
// The investment pool is working capital for construction, not a store of
// value. With no outlet it absorbs the whole household circulation: over 208
// weeks of the standard world it grew by 4.0e10 while every class pool drained
// (labor -8.2e9, engineers -1.6e10, capitalists -6.6e9), and the resulting loss
// of household purchasing power is what drags consumer demand down. Money above
// the market's own construction requirement is therefore paid out to
// households, whose pools - the labor pool above all - are its terminal
// destination.
constexpr int INVESTMENT_POOL_WORKING_WEEKS = 52;
// Share of a surplus returned per cycle, so an accumulated surplus is paid out
// smoothly instead of in one shock.
constexpr double INVESTMENT_POOL_RETURN_SHARE = 0.02;

// ===== 商品索引常量（避免魔法数字） =====
constexpr int CONSTR_GOOD_INDEX = 10;    // 建造力
constexpr int GOLD_GOOD_INDEX = 11;      // 贵金属
constexpr int TRANSPORT_GOOD_INDEX = TRANSPORT_CAPACITY_GOOD_INDEX;  // 运力

// ===== 高精度数值类型与上限 =====
using Money = Decimal;
inline const Money CLASS_CASH_MAX_MONEY    = Money(1e12L);
inline const Money COUNTRY_INITIAL_TREASURY_MONEY = Money(50000000.0);
inline const Money BUILDING_CASH_MAX_MONEY = Money(1e12L);
inline const Money INVEST_POOL_MAX_MONEY   = Money(1e12L);
inline const Money MONEY_SUPPLY_MAX_MONEY  = Money(1e13L);
// Floor of the investment pool's working requirement, so a market with no
// current construction demand still keeps a usable capital balance.
inline const Money INVESTMENT_POOL_MIN_WORKING_MONEY = Money(5000000.0);

// One-time capital reserved by the private investment pool when it submits
// an AI expansion request. The fee is separate from the construction
// budget and is paid per building request.
inline Money expansionStartupCapital(int typeIndex) {
    switch (typeIndex) {
    case FARM_GRAIN:
    case COTTON:
        return Money(100000.0);
    case FOOD_PROC:
    case CLOTHES:
    case LUXURY_CLOTHES:
    case HOUSING:
        return Money(200000.0);
    case STEEL_MILL:
    case TOOL_FACT:
    case COAL_MINE:
    case IRON_MINE:
    case GOLD_MINE:
        return Money(400000.0);
    default:
        return Money(0);
    }
}

inline const Money BASE_CREDIT_PER_BANK_MONEY      = Money(500000.0);
inline const Money BASE_CREDIT_PER_FINANCE_MONEY   = Money(1000000.0);
inline const Money AVERAGE_WAGE_MONEY              = Money(6.75);
