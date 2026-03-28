#pragma once
#include "Board.hpp"
#include "Rules.hpp"
#include <cstdint>
#include <cstdlib>
#include <algorithm>

extern uint64_t neighborMask[64];

// Représentation bitboard du plateau (valide uniquement pour w*h <= 64, soit 8x8 max).
// bit i = cellule (i%w, i/w).
struct BBState {
    static constexpr int MAX_MOVES = 512;

    uint64_t bb[2];   // bb[0]=P1, bb[1]=P2
    uint64_t holes;
    uint64_t mask;    // masque complet du plateau (w*h bits)
    int w, h;

    inline uint64_t empty() const { return mask & ~(bb[0] | bb[1] | holes); }

    static inline BBState fromBoard(const Board& b) {
        BBState s;
        s.w = b.width();
        s.h = b.height();
        s.bb[0] = s.bb[1] = s.holes = s.mask = 0;
        for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x) {
            int sq = y * s.w + x;
            Cell c = b.get(x, y);
            s.mask |= (1ULL << sq);
            if      (c == Cell::P1)   s.bb[0] |= (1ULL << sq);
            else if (c == Cell::P2)   s.bb[1] |= (1ULL << sq);
            else if (c == Cell::Hole) s.holes  |= (1ULL << sq);
        }
        return s;
    }

    inline int16_t evalFor(Player p) const {
        int n0 = __builtin_popcountll(bb[0]);
        int n1 = __builtin_popcountll(bb[1]);
        if (n0 == 0) return (p == Player::P2) ?  10000 : -10000;
        if (n1 == 0) return (p == Player::P1) ?  10000 : -10000;
        int score = n0 - n1;
        return (p == Player::P1) ? (int16_t)score : (int16_t)-score;
    }

    inline GameStatus getStatus() const {
        int n0 = __builtin_popcountll(bb[0]);
        int n1 = __builtin_popcountll(bb[1]);
        if (n0 == 0) return GameStatus::P2Wins;
        if (n1 == 0) return GameStatus::P1Wins;
        if (empty() == 0) {
            if (n0 > n1) return GameStatus::P1Wins;
            if (n1 > n0) return GameStatus::P2Wins;
            return GameStatus::Draw;
        }
        return GameStatus::Ongoing;
    }

    inline int genMoves(Player p, Move* buf) const {

        // int start = clock();

        int pi = (p == Player::P1) ? 0 : 1;
        uint64_t own = bb[pi];
        uint64_t emp = empty();
        int cnt = 0;
        uint64_t tmp = own;
        while (tmp) {
            int sq = __builtin_ctzll(tmp);
            tmp &= tmp - 1;
            int x = sq % w, y = sq / w;
            for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                if (!dx && !dy) continue;
                int nx = x + dx, ny = y + dy;
                if ((unsigned)nx < (unsigned)w && (unsigned)ny < (unsigned)h) {
                    int nsq = ny * w + nx;
                    if ((emp >> nsq) & 1)
                        buf[cnt++] = {(int8_t)x, (int8_t)y, (int8_t)nx, (int8_t)ny};
                }
            }
        }

        // int end = clock();
        // double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        // printf("Temps searchmooves: %f secondes\n", time_spent);
        return cnt;
    }

    inline void genMovesVec(const Board& b, Player p, std::vector<Move>& moves) const {
        int pi = (p == Player::P1) ? 0 : 1;
        uint64_t own = bb[pi];
        uint64_t emp = empty();
        int cnt = 0;
        uint64_t tmp = own;
        while (tmp) {
            int sq = __builtin_ctzll(tmp);
            tmp &= tmp - 1;
            int x = sq % w, y = sq / w;
            for (int dy = -2; dy <= 2; ++dy)
            for (int dx = -2; dx <= 2; ++dx) {
                if (!dx && !dy) continue;
                int nx = x + dx, ny = y + dy;
                if ((unsigned)nx < (unsigned)w && (unsigned)ny < (unsigned)h) {
                    int nsq = ny * w + nx;
                    if ((emp >> nsq) & 1)
                        moves.push_back({(int8_t)x, (int8_t)y, (int8_t)nx, (int8_t)ny});
                }
            }
        }
    }

    inline void applyMove(const Move& m, Player p) {

        //int start = clock();
        
        int pi = (p == Player::P1) ? 0 : 1;
        int oi = 1 - pi;
        int src = m.y1 * w + m.x1;
        int dst = m.y2 * w + m.x2;
        bool jump = std::max(std::abs((int)m.x2 - m.x1), std::abs((int)m.y2 - m.y1)) == 2;

        if (jump) bb[pi] &= ~(1ULL << src);
        bb[pi] |= (1ULL << dst);

        uint64_t adj = 0;
        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
            if (!dx && !dy) continue;
            int nx = m.x2 + dx, ny = m.y2 + dy;
            if ((unsigned)nx < (unsigned)w && (unsigned)ny < (unsigned)h)
                adj |= (1ULL << (ny * w + nx));
        }
        uint64_t converted = bb[oi] & adj;
        bb[oi] ^= converted;
        bb[pi] |= converted;

        //int end = clock();
        //double time_spent = (double)(end - start) / CLOCKS_PER_SEC;
        //printf("Temps applyMove: %f secondes\n", time_spent);
    }
};

inline int popLSB(uint64_t& bb)
{
    int sq = __builtin_ctzll(bb);
    bb &= bb - 1;
    return sq;
};


uint64_t neighborMask[64];
uint64_t reachAll[64];

inline void initNeighborMasks(int w, int h)
{
    for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
    {
        int sq = y * w + x;
        uint64_t mask = 0;

        for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (!dx && !dy) continue;

            int nx = x + dx;
            int ny = y + dy;

            if ((unsigned)nx < (unsigned)w &&
                (unsigned)ny < (unsigned)h)
            {
                mask |= 1ULL << (ny * w + nx);
            }
        }

        neighborMask[sq] = mask;
    }
};