#pragma once
#include "AIBase.hpp"

class RandomAI : public AIBase {
public:
    Move        chooseMove(const Board& b, Player p) override;
    std::string name() const override { return "Random"; }
};
