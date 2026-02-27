#pragma once
#include "AIBase.hpp"
#include <cstdint>

// Negamax alpha-beta avec parallélisme TBB au niveau racine :
// chaque coup candidat est évalué dans un thread indépendant.
class NegamaxParallelAI : public AIBase {
public:
    explicit NegamaxParallelAI(int depth = 4);
    Move        chooseMove(const Board& b, Player p) override;
    std::string name() const override;

private:
    int     depth_;
    int16_t search(Board b, int depth, Player current,
                   int16_t alpha, int16_t beta) const;
};
