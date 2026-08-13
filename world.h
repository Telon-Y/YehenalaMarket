// ==================== world.h ====================
#pragma once
#include "local_market.h"
#include "sim_types.h"
#include <vector>
#include <memory>
#include <unordered_map>

class World {
public:
    static World& Instance();

    // ===== 市场管理 =====
    int createMarket(const std::string& name);
    bool switchMarket(int index);
    LocalMarket& getCurrentMarket();
    LocalMarket& getMarket(int index);
    LocalMarket& getMarketById(int marketId);
    MarketSnapshot getMarketSnapshot(int index) const;
    int getCurrentIndex() const { return currentIdx; }
    int getMarketCount() const { return (int)markets.size(); }

    // ===== 贸易路径（2.0 核心） =====
    int addTradePath(int sourceMarketId, int targetMarketId, int goodIndex,
                     Money maxVolumePerWeek, Money transportCostPerUnit);
    const std::vector<TradePath>& getTradePaths() const { return tradePaths; }
    void executeTrade();   // 按价差执行各路径贸易

    // ===== 推进 =====
    void stepAll(bool runAI = true);

private:
    World();

    std::vector<std::unique_ptr<LocalMarket>> markets;
    std::vector<TradePath> tradePaths;
    std::unordered_map<int, int> marketIndexById;  // marketId -> index
    int nextMarketId = 0;
    int nextTradePathId = 0;
    int currentIdx = 0;
};
