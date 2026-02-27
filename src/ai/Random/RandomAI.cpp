#include "RandomAI.hpp"
#include "MoveGen.hpp"
#include <cstdlib>

Move RandomAI::chooseMove(const Board& b, Player p) {
    auto moves = MoveGen::generateMoves(b, p);
    if (moves.empty()) return {-1, -1, -1, -1};
    return moves[std::rand() % moves.size()];
}
