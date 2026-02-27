#include "Rules.hpp"
#include <cstdlib>

static int chebyshev(int x1, int y1, int x2, int y2) {
    return std::max(std::abs(x2 - x1), std::abs(y2 - y1));
}

bool Rules::isValidMove(const Board& b, const Move& m, Player p) {
    if (!b.inBounds(m.x1, m.y1) || !b.inBounds(m.x2, m.y2)) return false;
    if (b.get(m.x1, m.y1) != playerCell(p))   return false;
    if (b.get(m.x2, m.y2) != Cell::Empty)      return false;
    int d = chebyshev(m.x1, m.y1, m.x2, m.y2);
    return d == 1 || d == 2;
}

void Rules::applyMove(Board& b, const Move& m, Player p) {
    if (chebyshev(m.x1, m.y1, m.x2, m.y2) == 2)
        b.set(m.x1, m.y1, Cell::Empty);   // saut : case d'origine libérée

    b.set(m.x2, m.y2, playerCell(p));

    // Conversion des pièces adverses adjacentes (dist Chebyshev = 1)
    Cell enemy = playerCell(opponent(p));
    Cell own   = playerCell(p);
    for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
        if (dx == 0 && dy == 0) continue;
        int nx = m.x2 + dx, ny = m.y2 + dy;
        if (b.inBounds(nx, ny) && b.get(nx, ny) == enemy)
            b.set(nx, ny, own);
    }
}

bool Rules::canMove(const Board& b, Player p) {
    Cell own = playerCell(p);
    for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width();  ++x) {
        if (b.get(x, y) != own) continue;
        for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx, ny = y + dy;
            if (b.inBounds(nx, ny) && b.get(nx, ny) == Cell::Empty)
                return true;
        }
    }
    return false;
}

GameStatus Rules::getStatus(const Board& b) {
    int s1 = b.countPieces(Player::P1);
    int s2 = b.countPieces(Player::P2);

    // Fin immédiate si un joueur n'a plus de billes
    if (s1 == 0) return GameStatus::P2Wins;
    if (s2 == 0) return GameStatus::P1Wins;

    bool p1can = canMove(b, Player::P1);
    bool p2can = canMove(b, Player::P2);

    if (b.countEmpty() > 0 && (p1can || p2can))
        return GameStatus::Ongoing;

    if (s1 > s2) return GameStatus::P1Wins;
    if (s2 > s1) return GameStatus::P2Wins;
    return GameStatus::Draw;
}
