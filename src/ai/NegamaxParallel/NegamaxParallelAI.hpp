#pragma once
#include "AIBase.hpp"
#include "BBState.hpp"
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <string>

// Negamax alpha-beta avec parallélisme TBB au niveau racine.
// Utilise une représentation bitboard (valide pour plateaux <= 8x8).
class NegamaxParallelAI : public AIBase {
public:
    explicit NegamaxParallelAI(int depth = 4) : depth_(depth) {}

    std::string name() const override {
        return "NegamaxPar(d=" + std::to_string(depth_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        BBState root_s = BBState::fromBoard(b);
        Move buf[BBState::MAX_MOVES];
        int n = root_s.genMoves(p, buf);
        if (n == 0) return {-1, -1, -1, -1};

        using Range = tbb::blocked_range<int>;
        struct Best { Move move; int16_t score; };

        Best result = tbb::parallel_reduce(
            Range(0, n),
            Best{buf[0], -10000},
            [&](const Range& r, Best cur) -> Best {
                for (int i = r.begin(); i != r.end(); ++i) {
                    BBState ns = root_s;
                    ns.applyMove(buf[i], p);
                    int16_t score = -search(ns, depth_ - 1, opponent(p), -10000, 10000);
                    if (score > cur.score) { cur.score = score; cur.move = buf[i]; }
                }
                return cur;
            },
            [](const Best& a, const Best& b) -> Best {
                return a.score >= b.score ? a : b;
            }
        );
        return result.move;
    }

private:
    int depth_;

    inline int16_t search(BBState s, int depth, Player current,
                          int16_t alpha, int16_t beta) const {
        if (s.getStatus() != GameStatus::Ongoing || depth == 0)
            return s.evalFor(current);

        Move buf[BBState::MAX_MOVES];
        int cnt = s.genMoves(current, buf);

        if (cnt == 0)
            return -search(s, depth - 1, opponent(current), -beta, -alpha);

        int16_t best = -10000;
        for (int i = 0; i < cnt; ++i) {
            BBState ns = s;
            ns.applyMove(buf[i], current);
            int16_t score = -search(ns, depth - 1, opponent(current), -beta, -alpha);
            best  = std::max(best, score);
            alpha = std::max(alpha, best);
            if (beta <= alpha) break;
        }
        return best;
    }
};
