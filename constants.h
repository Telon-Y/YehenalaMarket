// constants.h
#pragma once
#include <vector>
#include <string>
#include <array>
#include "decimal.h"

// 模拟参数
constexpr int TOTAL_STEPS = 6000;
constexpr int AI_INTERVAL = 1;

// 商品数量 (1.2: 新增贵金属)
constexpr int NUM_GOODS = 12;

// 建筑类型数量 (1.2: 新增金矿、银行、金融区)
constexpr int TYPE_COUNT = 14;

// 消费组数量
constexpr int GROUP_COUNT = 4;

// 商品名称列表
inline const std::vector<std::string> commodityNames = {
    "谷物", "加工食品", "织物", "服装", "高档服装",
    "煤炭", "铁", "钢", "工具", "住房", "建造力",
    "贵金属"   // 1.2: 新增
};

// 建筑类型名称
inline const std::vector<std::string> buildingTypeNames = {
    "谷物农场", "加工食品厂", "棉花种植园", "服装厂", "高档服装厂",
    "煤矿", "铁矿", "炼钢厂", "工具厂", "住房", "建造部门",
    "金矿", "银行", "金融区"   // 1.2: 新增
};

// 建造成本 (1.2: 新增金矿600, 银行800, 金融区800)
inline const std::vector<double> buildingCost = {
    200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100,
    600, 800, 800
};

// 初始参考价格 (1.2: 贵金属初始参考价8000)
inline const std::array<double, NUM_GOODS> referencePrice = {
    2400.0, 4000.0, 5000.0, 12000.0, 40000.0,
    4000.0, 4000.0, 8000.0, 4000.0, 1600.0, 24000.0,
    8000.0
};

// 价格抑制系数 (加强版：默认0.30，原为0.15)
inline const std::array<double, NUM_GOODS> priceSuppressBase = [](){
    std::array<double, NUM_GOODS> arr;
    arr.fill(0.30);          // 默认：从0.15提高至0.30
    arr[1] = 0.36;           // 加工食品
    arr[4] = 0.24;           // 高档服装
    arr[10] = 0.50;          // 建造力
    arr[11] = 0.20;          // 贵金属
    return arr;
}();

// 需求表（每10万人）
inline const std::array<std::array<double, GROUP_COUNT>, 3> demandTable = {{
    {39.0, 210.0, 0.0, 20.0},   // 财富5 (劳工)
    {41.0, 210.0, 7.0, 74.0},   // 财富10 (工程师)
    {0.0,  210.0, 122.0, 130.0} // 财富20 (资本家)
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

// 建筑类型枚举 (1.2: 新增)
enum BuildingType {
    FARM_GRAIN, FOOD_PROC, COTTON, CLOTHES, LUXURY_CLOTHES,
    COAL_MINE, IRON_MINE, STEEL_MILL, TOOL_FACT, HOUSING, CONST_DEPT,
    GOLD_MINE, BANK, FINANCE
};

// 消费组枚举
enum ConsGroup {
    GRP_SIMPLE_CLOTHES = 0,
    GRP_BASIC_FOOD,
    GRP_STANDARD_CLOTHES,
    GRP_HOUSING
};

// 阶层枚举 (1.2)
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

// 1.2 金融常量
constexpr double BASE_CREDIT_PER_BANK = 500000.0;
constexpr double BASE_CREDIT_PER_FINANCE = 1000000.0;
constexpr double BANK_MONEY_MULTIPLIER = 2.0;
constexpr double ANNUAL_INTEREST_RATE = 0.05;

// ===== 新增：劳工负债挂钩系数 =====
constexpr double LABOR_DEBT_SCALE = 1e9;

// ===== 新增：现金池上限常量 =====
constexpr double CLASS_CASH_MAX    = 1e12;
constexpr double BUILDING_CASH_MAX  = 1e12;
constexpr double INVEST_POOL_MAX    = 1e12;
constexpr double MONEY_SUPPLY_MAX   = 1e13;

// ===== 新增：注册字体所需字符常量（如需离线收集可保留） =====
// （本版本不再需要手动维护字符，main.cpp 会自动注册全部汉字）

// ===== 新增：贷款系统常量 =====
constexpr double BANK_LOAN_UNIT_VALUE = 1000000.0;          // 每单位贷款 = 100万
constexpr int    BANK_MAX_LOAN_PER_LEVEL = 50;              // 每级银行最多50单位
constexpr double INVEST_LOAN_TRIGGER_RATIO = 2.0;           // 投资池缺口触发倍数(200%)
constexpr int    INVEST_LOAN_TERM_WEEKS = 260;              // 5年 = 260周

// ===== 高精度数值类型与上限 =====
using Money = Decimal;
inline const Money CLASS_CASH_MAX_MONEY    = Money(1e12L);
inline const Money BUILDING_CASH_MAX_MONEY = Money(1e12L);
inline const Money INVEST_POOL_MAX_MONEY   = Money(1e12L);
inline const Money MONEY_SUPPLY_MAX_MONEY  = Money(1e13L);

inline const Money BASE_CREDIT_PER_BANK_MONEY      = Money(500000.0);
inline const Money BASE_CREDIT_PER_FINANCE_MONEY   = Money(1000000.0);
inline const Money AVERAGE_WAGE_MONEY              = Money(6.75);