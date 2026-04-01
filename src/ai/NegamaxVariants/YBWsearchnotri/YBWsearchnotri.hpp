#pragma once
// Variante archivée : YBWsearchnotri — parallèle YBW sans tri heuristique.
// Source originale : src/ai/NegamaxParInc/NegamaxParIncAI.hpp
// Cette variante n'est plus appelée dans le code actif.

#include "AIBase.hpp"
#include "IncMoveState.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <vector>
#include <numeric>
#include <string>

extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

class NegamaxParIncAI_YBWsearchnotri : public AIBase {
public:
    explicit NegamaxParIncAI_YBWsearchnotri(int depth = 4)
        : depth_(depth) {}

    std::string name() const override {
        return "NegamaxParInc_YBWsearchnotri(d=" + std::to_string(depth_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());

        state_    = IncMoveState::fromBoard(b);
        bestMove_ = Move{-1, -1, -1, -1};

        YBWsearchnotri(depth_, p, -10000, 10000);
        return bestMove_;
    }

private:
    int          depth_;
    IncMoveState state_;
    Move         bestMove_;

    static constexpr int PARALLEL_THRESHOLD = 2;

    /*__________________________________________________________________________*/

    // ── YBWsearchnotri : parallèle sans tri ─────────────────────────────────

    /*__________________________________________________________________________*/

    inline int16_t YBWsearchnotri(int depth, Player current,
                              int16_t alpha, int16_t beta)
    {
        const int pi = (current == Player::P1) ? 0 : 1;

        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());

        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }

        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);

        int16_t best;
        {
            int8_t blobMask = 0;
            state_.applyMove(moves[order[0]], current, blobMask);
            best = (depth-1 <= PARALLEL_THRESHOLD)
                 ? -search         (depth-1, opponent(current), -beta, -alpha)
                 : -YBWsearchnotri (depth-1, opponent(current), -beta, -alpha);
            state_.removeMove(moves[order[0]], current, blobMask);
        }

        bestMove_ = moves[order[0]];
        if (n == 1 || best >= beta) return best;
        alpha = std::max(alpha, best);

        std::atomic<int64_t> atomicBest  { pack(best, order[0]) };
        std::atomic<int16_t> globalAlpha { alpha };

        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI_YBWsearchnotri worker = *this;
                    worker.state_.applyMove(moves[order[i]], current, blobMask);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search         (depth-1, opponent(current), -beta, -curAlpha)
                        : -worker.YBWsearchnotri (depth-1, opponent(current), -beta, -curAlpha);

                    int64_t oldBest = atomicBest.load(std::memory_order_relaxed);
                    while (score > score_of(oldBest)) {
                        if (atomicBest.compare_exchange_weak(oldBest, pack(score, order[i]),
                                std::memory_order_release, std::memory_order_relaxed)) break;
                    }
                    int16_t oldAlpha = globalAlpha.load(std::memory_order_relaxed);
                    while (score > oldAlpha) {
                        if (globalAlpha.compare_exchange_weak(oldAlpha, score,
                                std::memory_order_release, std::memory_order_relaxed)) break;
                    }
                }
            });

        const int64_t finalBest = atomicBest.load(std::memory_order_acquire);
        bestMove_ = moves[idx_of(finalBest)];
        return score_of(finalBest);
    }

    /*__________________________________________________________________________*/

    // ── search : séquentiel de base (feuilles) ───────────────────────────────

    /*__________________________________________________________________________*/

    inline int16_t search(int depth, Player current,
                          int16_t alpha, int16_t beta)
    {
        if (state_.getStatus() != GameStatus::Ongoing || depth == 0)
            return state_.evalFor(current);

        const int pi = (current == Player::P1) ? 0 : 1;

        if (!state_.hasAnyMove(pi))
            return -search(depth-1, opponent(current), -beta, -alpha);

        int16_t best   = -10000;
        bool    cutoff = false;

        state_.forEachMove(pi, [&](const Move& m) {
            if (cutoff) return;
            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);
            const int16_t sc = -search(depth-1, opponent(current), -beta, -alpha);
            state_.removeMove(m, current, blobMask);
            if (sc > best)  best  = sc;
            if (sc > alpha) alpha = sc;
            if (beta <= alpha) cutoff = true;
        });

        return best;
    }

    /*__________________________________________________________________________*/

    static int64_t pack     (int16_t score, int idx) {
        return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx;
    }
    static int16_t score_of (int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of   (int64_t v) { return (int)(uint32_t)v; }
};
