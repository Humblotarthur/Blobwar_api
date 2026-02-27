#pragma once
#include "Board.hpp"
#include <string>

// Interface commune à toutes les IA
class AIBase {
public:
    virtual ~AIBase() = default;
    virtual Move        chooseMove(const Board& b, Player p) = 0;
    virtual std::string name() const = 0;
};
