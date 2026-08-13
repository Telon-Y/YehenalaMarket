// security.h
#pragma once
#include "decimal.h"
#include "constants.h"

enum class SecurityType {
    FARM_ESTATE,        // 农业庄园
    INDUSTRIAL_SHARE,   // 工业股份
};

struct Security {
    int id;
    int buildingType;          // 对应生产建筑类型
    int owner;                 // OWNER_GOVERNMENT / OWNER_INITIAL / OWNER_FINANCE
    SecurityType type;
    Money faceValue;           // 面值，等于建造成本 × 建造力价格
    Money lastTradePrice;      // 最近交易价格
    int tradeSequence;         // 0=政府, 1=劳动力(资本家), 2=金融区
    bool active;
};