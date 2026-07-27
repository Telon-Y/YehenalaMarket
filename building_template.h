#pragma once
#include "constants.h"
#include <array>
#include <string>

struct BuildingTemplate {
    std::string name;
    int outputGood;
    double outputRate;
    std::array<double, NUM_GOODS> inputs;
    double laborPerUnit;

    double getUnitCost(const std::array<double, NUM_GOODS>& prices, double wageRate) const;
    double getProfitFactor(const std::array<double, NUM_GOODS>& prices, double wageRate) const;
};

std::vector<BuildingTemplate> createBuildingTemplates();