#pragma once
#include "Board.hpp"
#include <vector>

namespace MoveGen {
    // Retourne tous les coups légaux du joueur p
    std::vector<Move> generateMoves(const Board& b, Player p);
}
