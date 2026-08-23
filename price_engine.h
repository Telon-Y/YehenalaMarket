// ==================== price_engine.h ====================
// 价格 ODE 引擎：二阶弹力系统独立模块
#pragma once
#include "constants.h"
#include "decimal.h"

struct PriceState {
    std::array<Money, NUM_GOODS> prices;
    std::array<Money, NUM_GOODS> velocity;      // 价格一阶导数
    std::array<Money, NUM_GOODS> mass;          // 市场惯性 m = k * G
    std::array<Money, NUM_GOODS> suppress;      // 价格抑制系数 b
    std::array<Money, NUM_GOODS> baseRef;       // 基础参考价
    std::array<Money, NUM_GOODS> dynamicRef;    // 动态参考价（随价格水平）
    Money priceLevel = Money(1.0);
    Money targetPriceLevel = Money(1.0);

    double inertiaCoeff = 0.5;
    double dampRatio = 0.50;
};

// 初始化价格状态（设定基础参考价与抑制系数）
void initPriceState(PriceState& ps);

// 更新动态参考价（价格水平缓慢随动活跃货币）
void updateReferencePrices(PriceState& ps, Money activeMoneyRatio);

// 执行一期价格 ODE 步进
//   supply          实际产出
//   demandInput     中间投入需求
//   demandConsumer  实际消费需求
//   debtFactor      劳工负债抑制因子
//   dt              时间步长
//   excludedGood1   不参与ODE的商品索引（建造力）
//   excludedGood2   不参与ODE的商品索引（黄金）
void stepPrices(PriceState& ps,
                const std::array<Money, NUM_GOODS>& supply,
                const std::array<Money, NUM_GOODS>& demandInput,
                const std::array<Money, NUM_GOODS>& demandConsumer,
                double debtFactor,
                double dt,
                int excludedGood1,
                int excludedGood2,
                int excludedGood3 = TRANSPORT_CAPACITY_GOOD_INDEX);