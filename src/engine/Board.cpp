#include "Board.hpp"

Board::Board(int width, int height)
    : w_(width), h_(height), cells_(width * height, Cell::Empty) {}

void Board::setupDefault() {
    set(0,    0,    Cell::P1);
    set(w_-1, 0,    Cell::P1);
    set(0,    h_-1, Cell::P2);
    set(w_-1, h_-1, Cell::P2);
}

void Board::setupCross8x8() {
    set(0,    0,    Cell::P1);
    set(w_-1, 0,    Cell::P1);
    set(0,    h_-1, Cell::P2);
    set(w_-1, h_-1, Cell::P2);

    for (int x = 0; x <= 2; x++){
        setHole((w_-1)/2 - x,     (h_-1)/2 - x);
        setHole((w_-1)/2 + x + 1, (h_-1)/2 + x + 1);
        setHole((w_-1)/2 + x + 1, (h_-1)/2 - x);
        setHole((w_-1)/2 - x,     (h_-1)/2 + 1 + x);
    }
}

void Board::setupCross() {
    // Trous 2x2 dans chaque coin
    for (int dy = 0; dy < 2; ++dy)
        for (int dx = 0; dx < 2; ++dx) {
            setHole(dx,       dy);
            setHole(w_-1-dx,  dy);
            setHole(dx,       h_-1-dy);
            setHole(w_-1-dx,  h_-1-dy);
        }
    // Pièces hors des trous
    set(2, 0,      Cell::P1);
    set(0, 2,      Cell::P1);
    set(w_-3, h_-1, Cell::P2);
    set(w_-1, h_-3, Cell::P2);
}

int Board::countPieces(Player p) const {
    Cell target = playerCell(p);
    int n = 0;
    for (Cell c : cells_) n += (c == target);
    return n;
}

int Board::countEmpty() const {
    int n = 0;
    for (Cell c : cells_) n += (c == Cell::Empty);
    return n;
}
