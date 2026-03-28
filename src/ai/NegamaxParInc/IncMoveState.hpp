#pragma once
#include "BBState.hpp"
#include <cstdlib>
#include <algorithm>

// ── Globaux précalculés (définis dans le .cpp qui contient neighborMask) ──────
extern uint64_t neighborMask[64]; // 8 voisins directs (±1) — conversions
extern uint64_t reachAll[64];     // 24 cases atteignables (±1 et ±2) — coups

// Initialise reachAll pour une grille w×h (à appeler après initNeighborMasks)
inline void initReachAll(int w, int h) {
    for (int sq = 0; sq < w * h; ++sq) {
        const int x = sq % w, y = sq / w;
        reachAll[sq] = 0;
        for (int dy = -2; dy <= 2; ++dy)
        for (int dx = -2; dx <= 2; ++dx) {
            if (!dx && !dy) continue;
            const int nx = x + dx, ny = y + dy;
            if ((unsigned)nx < (unsigned)w && (unsigned)ny < (unsigned)h)
                reachAll[sq] |= 1ULL << (ny * w + nx);
        }
        // Zéro les slots hors grille (sécurité)
    }
    for (int sq = w * h; sq < 64; ++sq)
        reachAll[sq] = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// IncMoveState
//
// Invariant :
//   moveTable[sq] = reachAll[sq] & empty   si sq est occupé (bb[0]|bb[1])
//   moveTable[sq] = 0                       si sq est vide
//
// Coûts :
//   applyMove  : ~30-45 ops (clone) / ~45-60 ops (jump)
//   removeMove : ~30-45 ops (clone) / ~45-60 ops (jump)
//   forEachMove : O(pièces × reach) — lazy, zéro maintenance vecteur
// ─────────────────────────────────────────────────────────────────────────────
struct IncMoveState {

    BBState  board;
    uint64_t moveTable[64]; // coups jouables depuis chaque case
    uint64_t empty;         // ~(bb[0]|bb[1]) & boardMask — maintenu en cache

    // ── Construction ────────────────────────────────────────────────────────
    static inline IncMoveState fromBoard(const Board& b);

    // ── Apply / Undo ────────────────────────────────────────────────────────
    // blobMask : 1 bit par voisin de dst dans neighborMask, 1 = converti
    inline int8_t applyMove (const Move& m, Player p, int8_t& blobMask);
    inline void   removeMove(const Move& m, Player p,  int8_t  blobMask);

    // ── Évaluation ──────────────────────────────────────────────────────────
    inline int16_t    evalFor  (Player p) const { return board.evalFor(p); }
    inline GameStatus getStatus()         const;
    inline bool       hasAnyMove(int pi)  const;

    // ── Itération des coups (lazy, pour Negamax) ─────────────────────────── 
    // Callback : void(const Move&)
    // Retourne false depuis le callback pour court-circuiter (cutoff)
    template<typename Fn>
    inline void forEachMove(int pi, Fn&& fn) const;
};

// ── fromBoard ────────────────────────────────────────────────────────────────

inline IncMoveState IncMoveState::fromBoard(const Board& b) {
    IncMoveState s;
    s.board = BBState::fromBoard(b);

    const int      cells     = s.board.w * s.board.h;
    const uint64_t boardMask = (cells >= 64) ? ~0ULL : (1ULL << cells) - 1;
    const uint64_t occ       = s.board.bb[0] | s.board.bb[1];

    s.empty = ~occ & boardMask;

    for (int sq = 0; sq < cells; ++sq)
        s.moveTable[sq] = (occ & (1ULL << sq)) ? (reachAll[sq] & s.empty) : 0ULL;

    for (int sq = cells; sq < 64; ++sq)
        s.moveTable[sq] = 0;

    return s;
}

// ── applyMove ────────────────────────────────────────────────────────────────

inline int8_t IncMoveState::applyMove(const Move& m, Player p, int8_t& blobMask)
{
    // int start = clock();
    const int pi = (p == Player::P1) ? 0 : 1;
    const int oi = 1 - pi;
    auto& bb     = board.bb;

    const int src  = m.y1 * board.w + m.x1;
    const int dst  = m.y2 * board.w + m.x2;
    const bool jump = ((m.x2-m.x1)*(m.x2-m.x1) + (m.y2-m.y1)*(m.y2-m.y1)) > 2;

    const uint64_t srcMask = 1ULL << src;
    const uint64_t dstMask = 1ULL << dst;

    // ── 1. Bitboards ─────────────────────────────────────────────────────────
    if (jump) bb[pi] &= ~srcMask;
    bb[pi] |= dstMask;

    const uint64_t converted = neighborMask[dst] & bb[oi];
    bb[oi] ^= converted;
    bb[pi] |= converted;

    // blobMask : 1 bit par voisin dans neighborMask[dst]
    {
        uint64_t tmp = neighborMask[dst];
        int bit = 0;
        while (tmp) {
            const int sq = popLSB(tmp);
            if (converted & (1ULL << sq)) blobMask |= (int8_t)(1u << bit);
            ++bit;
        }
    }

    // ── 2. empty ─────────────────────────────────────────────────────────────
    if (jump) empty |=  srcMask;   // src libérée
              empty &= ~dstMask;   // dst occupée

    // ── 3. dst occupé → retirer dstMask de tous les voisins occupés ──────────
    //       puis initialiser moveTable[dst] pour la nouvelle pièce
    {
        uint64_t neighbors = reachAll[dst] & (bb[0] | bb[1]);
        while (neighbors)
            moveTable[popLSB(neighbors)] &= ~dstMask;

        moveTable[dst] = reachAll[dst] & empty;
    }

    // ── 4. src libéré (jump) → ajouter srcMask aux voisins, vider moveTable[src]
    if (jump) {
        uint64_t neighbors = reachAll[src] & (bb[0] | bb[1]);
        while (neighbors)
            moveTable[popLSB(neighbors)] |= srcMask;

        moveTable[src] = 0;
    }

    // ── 5. Conversions : seul bb[] change, empty et reachAll inchangés
    //       → moveTable[sq] reste valide pour les pièces converties ───────────

    // int end = clock();
    // double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
    // printf("Temps applyMove INC: %f secondes\n", time_spent);

    return blobMask;
}

// ── removeMove ───────────────────────────────────────────────────────────────

inline void IncMoveState::removeMove(const Move& m, Player p, int8_t blobMask)
{
    const int pi = (p == Player::P1) ? 0 : 1;
    const int oi = 1 - pi;
    auto& bb     = board.bb;

    const int src  = m.y1 * board.w + m.x1;
    const int dst  = m.y2 * board.w + m.x2;
    const bool jump = ((m.x2-m.x1)*(m.x2-m.x1) + (m.y2-m.y1)*(m.y2-m.y1)) > 2;

    const uint64_t srcMask = 1ULL << src;
    const uint64_t dstMask = 1ULL << dst;

    // Reconstruction de converted depuis blobMask
    uint64_t converted = 0;
    {
        uint64_t tmp = neighborMask[dst];
        int bit = 0;
        while (tmp) {
            const int sq = popLSB(tmp);
            if ((blobMask >> bit) & 1) converted |= (1ULL << sq);
            ++bit;
        }
    }

    // ── 1. Undo bitboards ────────────────────────────────────────────────────
    bb[pi] &= ~converted;
    bb[oi] |=  converted;
    bb[pi] &= ~dstMask;
    if (jump) bb[pi] |= srcMask;

    // ── 2. Undo empty ────────────────────────────────────────────────────────
              empty |=  dstMask;   // dst libérée
    if (jump) empty &= ~srcMask;   // src réoccupée

    // ── 3. dst libéré → rajouter dstMask aux voisins, vider moveTable[dst] ───
    {
        uint64_t neighbors = reachAll[dst] & (bb[0] | bb[1]);
        while (neighbors)
            moveTable[popLSB(neighbors)] |= dstMask;

        moveTable[dst] = 0;
    }

    // ── 4. src réoccupé (jump) → retirer srcMask des voisins, restaurer moveTable[src]
    if (jump) {
        uint64_t neighbors = reachAll[src] & (bb[0] | bb[1]);
        while (neighbors)
            moveTable[popLSB(neighbors)] &= ~srcMask;

        moveTable[src] = reachAll[src] & empty;
    }

    // ── 5. Conversions : moveTable[sq] reste valide (seul bb[] a changé) ─────
}

// ── hasAnyMove ───────────────────────────────────────────────────────────────

inline bool IncMoveState::hasAnyMove(int pi) const {
    uint64_t pieces = board.bb[pi];
    while (pieces) {
        const int sq = __builtin_ctzll(pieces);
        pieces &= pieces - 1;
        if (moveTable[sq]) return true;   // premier coup trouvé → stop
    }
    return false;
}

// ── forEachMove ──────────────────────────────────────────────────────────────
// Itère tous les coups du joueur pi.
// Fn = void(const Move&) — utiliser un flag cutoff externe pour alpha-beta.

template<typename Fn>
inline void IncMoveState::forEachMove(int pi, Fn&& fn) const {
    const int w    = board.w;
    uint64_t pieces = board.bb[pi];
    while (pieces) {
        const int src = __builtin_ctzll(pieces);
        pieces &= pieces - 1;
        uint64_t dsts = moveTable[src];
        while (dsts) {
            const int dst = __builtin_ctzll(dsts);
            dsts &= dsts - 1;
            fn(Move{
                (int8_t)(src % w), (int8_t)(src / w),
                (int8_t)(dst % w), (int8_t)(dst / w)
            });
        }
    }
}

// ── getStatus ────────────────────────────────────────────────────────────────

inline GameStatus IncMoveState::getStatus() const {
    const int n0 = __builtin_popcountll(board.bb[0]);
    const int n1 = __builtin_popcountll(board.bb[1]);

    if (n0 == 0) return GameStatus::P2Wins;
    if (n1 == 0) return GameStatus::P1Wins;

    // Plateau plein
    if (empty == 0) {
        if (n0 > n1) return GameStatus::P1Wins;
        if (n1 > n0) return GameStatus::P2Wins;
        return GameStatus::Draw;
    }

    // Au moins un joueur peut encore jouer
    if (hasAnyMove(0) || hasAnyMove(1))
        return GameStatus::Ongoing;

    // Cases vides mais aucun coup possible
    if (n0 > n1) return GameStatus::P1Wins;
    if (n1 > n0) return GameStatus::P2Wins;
    return GameStatus::Draw;
}