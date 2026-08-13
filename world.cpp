// ==================== world.cpp ====================
#include "world.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

World& World::Instance() {
    static World instance;
    return instance;
}

World::World() {
    int id = nextMarketId++;
    markets.push_back(std::make_unique<LocalMarket>(id, "叶赫那拉主市场"));
    marketIndexById[id] = 0;
    currentIdx = 0;
}

int World::createMarket(const std::string& name) {
    int id = nextMarketId++;
    int index = (int)markets.size();
    markets.push_back(std::make_unique<LocalMarket>(id, name));
    marketIndexById[id] = index;
    return id;
}

bool World::switchMarket(int index) {
    if (index < 0 || index >= static_cast<int>(markets.size())) return false;
    currentIdx = index;
    return true;
}

LocalMarket& World::getCurrentMarket() {
    return *markets[currentIdx];
}

LocalMarket& World::getMarket(int index) {
    if (index < 0 || index >= static_cast<int>(markets.size()))
        throw std::out_of_range("market index out of range");
    return *markets[static_cast<size_t>(index)];
}

LocalMarket& World::getMarketById(int marketId) {
    auto it = marketIndexById.find(marketId);
    if (it == marketIndexById.end())
        throw std::out_of_range("market id not found");
    return *markets[it->second];
}

MarketSnapshot World::getMarketSnapshot(int index) const {
    if (index < 0 || index >= (int)markets.size())
        throw std::out_of_range("market snapshot index out of range");
    return markets[index]->getSnapshot();
}

int World::addTradePath(int sourceMarketId, int targetMarketId, int goodIndex,
                        Money maxVolumePerWeek, Money transportCostPerUnit) {
    if (sourceMarketId == targetMarketId) return -1;
    if (goodIndex < 0 || goodIndex >= NUM_GOODS) return -1;
    if (marketIndexById.find(sourceMarketId) == marketIndexById.end()) return -1;
    if (marketIndexById.find(targetMarketId) == marketIndexById.end()) return -1;

    int id = nextTradePathId++;
    TradePath path;
    path.id = id;
    path.sourceMarketId = sourceMarketId;
    path.targetMarketId = targetMarketId;
    path.goodIndex = goodIndex;
    path.maxVolumePerWeek = maxVolumePerWeek;
    path.transportCostPerUnit = transportCostPerUnit;
    path.active = true;
    tradePaths.push_back(path);
    return id;
}

void World::executeTrade() {
    for (const auto& path : tradePaths) {
        if (!path.active) continue;
        if (marketIndexById.find(path.sourceMarketId) == marketIndexById.end()) continue;
        if (marketIndexById.find(path.targetMarketId) == marketIndexById.end()) continue;

        LocalMarket& src = *markets[marketIndexById[path.sourceMarketId]];
        LocalMarket& dst = *markets[marketIndexById[path.targetMarketId]];

        Money srcPrice = src.getPriceState().prices[path.goodIndex];
        Money dstPrice = dst.getPriceState().prices[path.goodIndex];
        Money priceDiff = dstPrice - srcPrice - path.transportCostPerUnit;

        if (priceDiff <= Money(0)) continue;
        Money srcAvailable = src.getInventory()[path.goodIndex];
        if (srcAvailable <= Money(0)) continue;

        Money affordableVolume = dstPrice > Money(0)
            ? dst.getInvestmentPool() / dstPrice : Money(0);
        Money volume = std::min({path.maxVolumePerWeek, srcAvailable, affordableVolume});
        if (volume <= Money(0)) continue;

        // 货币结算：dst 按 dstPrice 支付给 src
        Money payment = dstPrice * volume;
        if (!dst.tryTradePayment(payment)) continue;
        Money transferred = src.takeFromInventory(path.goodIndex, volume);
        if (transferred <= Money(0)) {
            dst.addTradePayment(payment);
            continue;
        }
        dst.addToInventory(path.goodIndex, transferred);
        src.addTradePayment(payment);

        // 更新贸易余额
        src.addTradeBalance(path.goodIndex, -transferred);
        dst.addTradeBalance(path.goodIndex, transferred);
    }
}

void World::stepAll(bool runAI) {
    for (auto& market : markets) {
        market->step();
        if (runAI && market->getStepCount() % AI_INTERVAL == 0)
            market->aiBuild();
    }
    executeTrade();
}
