#pragma once
#include "Board.hpp"
#include "Rules.hpp"
#include <cstdint>
#include <cstdlib>
#include <algorithm>

// État incrémental : moves et score maintenus sans rescan complet.
// Trade mémoire/copie contre évitement de MoveGen et Eval à chaque nœud.
struct IncState {
    static constexpr int MAX_CELLS = 100;   // 10×10 max
    static constexpr int MAX_MOVES = 512;   // safe pour 8×8 et 10×10

    Cell    cells[MAX_CELLS];
    int     w, h;

    // Listes de coups par joueur (tableau fixe, pas de heap sur copie)
    Move moveBuf[2][MAX_MOVES];
    int  moveCount[2];

    int16_t score;       // matériel P1 - P2, × evalFactor
    int     evalFactor;  // 2 pour 8×8, 1 sinon (réplique Eval::evaluate)
    int     pieceCount[2];
    int     emptyCount;

    // ── Accesseurs de base ──────────────────────────────────────────────
    int  idx(int x, int y)       const { return y * w + x; }
    bool inBounds(int x, int y)  const {
        return (unsigned)x < (unsigned)w && (unsigned)y < (unsigned)h;
    }
    Cell get(int x, int y)       const { return cells[idx(x, y)]; }
    void set(int x, int y, Cell c)     { cells[idx(x, y)] = c; }

    // ── Interface publique ──────────────────────────────────────────────
    // Initialisation depuis un Board moteur (appelé UNE fois par thread racine)
    static IncState fromBoard(const Board& b);

    // Applique un coup et met à jour moves + score incrémentalement
    void applyMove(const Move& m, Player p);

    GameStatus getStatus()       const;
    int16_t    evalFor(Player p) const;

private:
    void removeFrom  (int sx, int sy, int pi);
    void removeTo    (int tx, int ty, int pi);
    void addMovesFrom(int sx, int sy, int pi);
    void addMovesTo  (int tx, int ty, int pi);
};
