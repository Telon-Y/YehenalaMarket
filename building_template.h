#pragma once
#include "constants.h"
#include <array>
#include <string>

struct BuildingTemplate {
    std::string name;
    int outputGood;                  // 商品索引，-1 表示无商品产出（金融建筑）
    double outputRate;               // 基础产出率（金融建筑可忽略）
    std::array<double, NUM_GOODS> inputs;
    double laborPerUnit;
    bool isFinancial = false;        // 1.2: 金融建筑标记
    double moneyMultiplier = 1.0;    // 1.2: 银行铸币乘数（仅银行有效）

    double getUnitCost(const std::array<double, NUM_GOODS>& prices, double wageRate) const;
    double getProfitFactor(const std::array<double, NUM_GOODS>& prices, double wageRate) const;
};

std::vector<BuildingTemplate> createBuildingTemplates();