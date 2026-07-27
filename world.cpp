#include "world.h"

World& World::Instance() {
    static World instance;
    return instance;
}

int World::addMarket() {
    markets.push_back(std::make_unique<LocalMarket>());
    return markets.size() - 1;
}

void World::switchMarket(int index) {
    if (index >= 0 && index < markets.size())
        currentIdx = index;
}

LocalMarket& World::getCurrentMarket() {
    return *markets[currentIdx];
}

LocalMarket& World::getMarket(int index) {
    return *markets[index];
}

void World::stepAll() {
    for (auto& market : markets)
        market->step();
}