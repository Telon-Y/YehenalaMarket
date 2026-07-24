#pragma once
#include <vector>
#include <string>
#include <array>

// 模拟参数
constexpr int TOTAL_STEPS = 6000;
constexpr int AI_INTERVAL = 1;

// 商品数量
constexpr int NUM_GOODS = 11;

// 建筑类型数量（注意：自给农场不在列表中）
constexpr int TYPE_COUNT = 11;

// 消费组数量
constexpr int GROUP_COUNT = 4;

// 商品名称列表
inline const std::vector<std::string> commodityNames = {
    "谷物", "加工食品", "织物", "服装", "高档服装",
    "煤炭", "铁", "钢", "工具", "住房", "建造力"
};

// 建筑类型名称
inline const std::vector<std::string> buildingTypeNames = {
    "谷物农场", "加工食品厂", "棉花种植园", "服装厂", "高档服装厂",
    "煤矿", "铁矿", "炼钢厂", "工具厂", "住房", "建造部门"
};

// 建造成本
inline const std::vector<double> buildingCost = {
    200, 600, 200, 600, 600, 600, 600, 800, 800, 800, 100
};

// 初始参考价格（v1.1 草案）
inline const std::array<double, NUM_GOODS> referencePrice = {
    2400.0,   // 谷物
    4000.0,   // 加工食品
    5000.0,   // 织物
    12000.0,  // 服装
    40000.0,  // 高档服装
    4000.0,   // 煤炭
    4000.0,   // 铁
    8000.0,   // 钢
    4000.0,   // 工具
    1600.0,   // 住房
    24000.0   // 建造力
};

// 价格抑制系数（部分有微调）
inline const std::array<double, NUM_GOODS> priceSuppressBase = [](){
    std::array<double, NUM_GOODS> arr;
    arr.fill(0.15);
    arr[1] = 0.18;   // 加工食品
    arr[4] = 0.12;   // 高档服装
    arr[10] = 0.25;  // 建造力
    return arr;
}();

// 需求表（每10万人需求量）【基础食物统一为210】
inline const std::array<std::array<double, GROUP_COUNT>, 3> demandTable = {{
    {39.0, 210.0, 0.0, 20.0},   // 财富 5
    {41.0, 210.0, 7.0, 74.0},   // 财富 10
    {0.0,  210.0, 122.0, 130.0}  // 财富 20
}};

// 消费组对应的商品列表（已修正：简朴衣物对应服装索引3）
inline const std::vector<std::vector<int>> groupGoods = {
    {3},        // 简朴衣物 → 服装（索引3）
    {0, 1},     // 基础食物 → 谷物(0)、加工食品(1)
    {4},        // 标准衣物 → 高档服装(4)
    {9}         // 住宅 → 住房(9)
};

// 使用价值系数 [商品][消费组]（已修正：服装为索引3）
inline const std::array<std::array<double, GROUP_COUNT>, NUM_GOODS> valueCoeff = [](){
    std::array<std::array<double, GROUP_COUNT>, NUM_GOODS> arr{};
    // 初始化为0
    for (int i = 0; i < NUM_GOODS; ++i)
        for (int g = 0; g < GROUP_COUNT; ++g)
            arr[i][g] = 0.0;

    arr[0][1] = 1.0;   // 谷物 → 基础食物
    arr[1][1] = 1.5;   // 加工食品 → 基础食物
    arr[3][0] = 1.0;   // 服装 → 简朴衣物
    arr[4][2] = 1.0;   // 高档服装 → 标准衣物
    arr[9][3] = 1.0;   // 住房 → 住宅
    return arr;
}();

// 建筑类型枚举
enum BuildingType {
    FARM_GRAIN, FOOD_PROC, COTTON, CLOTHES, LUXURY_CLOTHES,
    COAL_MINE, IRON_MINE, STEEL_MILL, TOOL_FACT, HOUSING, CONST_DEPT
};

// 消费组枚举
enum ConsGroup {
    GRP_SIMPLE_CLOTHES = 0,
    GRP_BASIC_FOOD,
    GRP_STANDARD_CLOTHES,
    GRP_HOUSING
};