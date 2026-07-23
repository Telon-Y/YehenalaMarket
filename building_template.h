#pragma once
#include "constants.h"
#include <array>
#include <string>

struct BuildingTemplate {
    std::string name;
    int outputGood;           // 产出商品索引
    double outputRate;        // 每单位建筑的基础产出速率
    std::array<double, NUM_GOODS> inputs;  // 生产每单位产出所需投入
    double laborPerUnit;      // 每单位产出所需劳动力（人）

    // 计算单位产出的成本（不含利润率）
    double getUnitCost(const std::array<double, NUM_GOODS>& prices, double wageRate) const;

    // 根据利润因子调整产出效率（用于劳动力不足等）
    double getProfitFactor(const std::array<double, NUM_GOODS>& prices, double wageRate) const;
};

// 所有建筑模板的初始化函数（在 .cpp 中实现）
std::vector<BuildingTemplate> createBuildingTemplates();