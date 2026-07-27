#pragma once
#include "local_market.h"
#include <vector>
#include <memory>

class World {
public:
    static World& Instance();

    int addMarket();
    void switchMarket(int index);
    LocalMarket& getCurrentMarket();
    LocalMarket& getMarket(int index);
    int getCurrentIndex() const { return currentIdx; }
    int getMarketCount() const { return markets.size(); }

    void stepAll();

private:
    World() { markets.push_back(std::make_unique<LocalMarket>()); }
    std::vector<std::unique_ptr<LocalMarket>> markets;
    int currentIdx = 0;
};