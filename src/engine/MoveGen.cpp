#include "MoveGen.hpp"

std::vector<Move> MoveGen::generateMoves(const Board& b, Player p) {
    std::vector<Move> moves;
    moves.reserve(64);
    Cell own = playerCell(p);

    for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width();  ++x) {
        if (b.get(x, y) != own) continue;
        for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (b.inBounds(nx, ny) && b.get(nx, ny) == Cell::Empty)
                moves.push_back({(int8_t)x, (int8_t)y, (int8_t)nx, (int8_t)ny});
        }
    }
    return moves;
}
