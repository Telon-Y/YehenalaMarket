#pragma once
#include "constants.h"
#include <array>
#include <string>

struct BuildingTemplate {
    std::string name;
    int outputGood;                  // 商品索引，-1 表示无商品产出（金融建筑）
    double outputRate;               // 基础产出率（金融建筑可忽略）
    std::array<double, NUM_GOODS> inputs;   // 输入系数（保持 double，非货币）
    double laborPerUnit;             // 劳动人数（非货币）
    bool isFinancial = false;
    double moneyMultiplier = 1.0;    // 倍率（非货币）

    Money getUnitCost(const std::array<Money, NUM_GOODS>& prices, Money wageRate) const;
    Money getProfitFactor(const std::array<Money, NUM_GOODS>& prices, Money wageRate) const;
};

std::vector<BuildingTemplate> createBuildingTemplates();