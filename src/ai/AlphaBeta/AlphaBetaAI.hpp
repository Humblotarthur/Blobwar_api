#pragma once
#include "AIBase.hpp"
#include <cstdint>

class AlphaBetaAI : public AIBase {
public:
    explicit AlphaBetaAI(int depth = 4);
    Move        chooseMove(const Board& b, Player p) override;
    std::string name() const override;

private:
    int     depth_;
    int16_t search(Board b, int depth, bool isMax, Player us,
                   int16_t alpha, int16_t beta) const;
};
