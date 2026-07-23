#pragma once
#include <string>
#include <vector>
#include <array>
#include <fstream>
#include "constants.h"

// 写入 CSV 的通用模板函数（在头文件中实现）
template<typename T>
void writeCsv(const std::string& fname, const std::string& header, const std::vector<std::array<T, NUM_GOODS>>& data) {
    std::ofstream f(fname);
    f << "\xEF\xBB\xBF" << header << "\n";  // UTF-8 BOM
    for (size_t row = 0; row < data.size(); ++row) {
        f << row;
        for (auto val : data[row]) f << "," << val;
        f << "\n";
    }
}

// 针对建筑类型（TYPE_COUNT）的特化
template<typename T>
void writeCsvBuilding(const std::string& fname, const std::string& header, const std::vector<std::array<T, TYPE_COUNT>>& data) {
    std::ofstream f(fname);
    f << "\xEF\xBB\xBF" << header << "\n";
    for (size_t row = 0; row < data.size(); ++row) {
        f << row;
        for (auto val : data[row]) f << "," << val;
        f << "\n";
    }
}

// 所有 CSV 输出函数声明
void writeAllCsvs(class MarketSimulation& sim);