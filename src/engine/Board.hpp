#pragma once
#include <cstdint>
#include <vector>

enum class Cell   : uint8_t { Empty = 0, P1 = 1, P2 = 2, Hole = 3 };
enum class Player : uint8_t { None  = 0, P1 = 1, P2 = 2 };

inline Player opponent(Player p) {
    return (p == Player::P1) ? Player::P2 : Player::P1;
}
inline Cell playerCell(Player p) {
    return (p == Player::P1) ? Cell::P1 : Cell::P2;
}

struct Move {
    int8_t x1, y1, x2, y2;
};

class Board {
public:
    Board(int width, int height);

    Cell get(int x, int y) const       { return cells_[y * w_ + x]; }
    void set(int x, int y, Cell c)     { cells_[y * w_ + x] = c; }
    bool inBounds(int x, int y) const  { return (unsigned)x < (unsigned)w_
                                              && (unsigned)y < (unsigned)h_; }

    int  width()  const { return w_; }
    int  height() const { return h_; }

    void setHole(int x, int y) { set(x, y, Cell::Hole); }
    void setupDefault();    // 8x8 classique : coins seulement, sans trous
    void setupCross8x8();   // 8x8 croix : coins + trous diamant centre
    void setupCross();      // 9x9 croix : trous 2x2 aux angles

    int countPieces(Player p) const;
    int countEmpty()          const;

    int  moveCount()    const { return moveCount_; }
    void incMoveCount()       { ++moveCount_; }

private:
    int w_, h_;
    int moveCount_ = 0;
    std::vector<Cell> cells_;
};
