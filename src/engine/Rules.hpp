#pragma once
#include "Board.hpp"

enum class GameStatus { Ongoing, P1Wins, P2Wins, Draw };

namespace Rules {
    bool       isValidMove(const Board& b, const Move& m, Player p);
    void       applyMove  (Board& b, const Move& m, Player p);
    bool       canMove    (const Board& b, Player p);
    GameStatus getStatus  (const Board& b);
}
