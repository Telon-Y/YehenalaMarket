// ==================== sim_types.h ====================
// 跨市场共享数据结构
#pragma once
#include "constants.h"
#include "decimal.h"
#include <string>

// 跨市场贸易路径
struct TradePath {
    int id = -1;
    int sourceMarketId = -1;
    int targetMarketId = -1;
    int goodIndex = 0;
    Money maxVolumePerWeek = Money(0);       // 每周最大运输量
    Money transportCostPerUnit = Money(0);   // 单位运输成本（价差超过此值才触发贸易）
    bool active = true;
};

// 市场快照（供世界层查询，避免直接耦合内部状态）
struct MarketSnapshot {
    int marketId = -1;
    std::string marketName;
    std::array<Money, NUM_GOODS> prices{};
    std::array<Money, NUM_GOODS> inventory{};
    Money gdp = Money(0);
    double population = 0.0;
};