#pragma once
// Variante archivée : Parallelsearch — parallèle via tbb::parallel_reduce, sans tri.
// Source originale : src/ai/NegamaxParInc/NegamaxParIncAI.hpp
// Cette variante n'est plus appelée dans le code actif.

#include "AIBase.hpp"
#include "IncMoveState.hpp"
#include <tbb/parallel_for.h>
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <vector>
#include <string>

extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

class NegamaxParIncAI_Parallelsearch : public AIBase {
public:
    explicit NegamaxParIncAI_Parallelsearch(int depth = 4)
        : depth_(depth) {}

    std::string name() const override {
        return "NegamaxParInc_Parallelsearch(d=" + std::to_string(depth_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());

        state_    = IncMoveState::fromBoard(b);
        bestMove_ = Move{-1, -1, -1, -1};

        Parallelsearch(depth_, p, -10000, 10000);
        return bestMove_;
    }

private:
    int          depth_;
    IncMoveState state_;
    Move         bestMove_;

    static constexpr int PARALLEL_THRESHOLD = 2;

    struct Result { int16_t score; int index; };

    /*__________________________________________________________________________*/

    // ── Parallelsearch ───────────────────────────────────────────────────────

    /*__________________________________________________________________________*/

    inline int16_t Parallelsearch(int depth, Player current,
                                  int16_t alpha, int16_t beta)
    {
        const int pi = (current == Player::P1) ? 0 : 1;

        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());

        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }

        Result result = tbb::parallel_reduce(
            tbb::blocked_range<int>(0, n),
            Result{ -10000, -1 },
            [&](const tbb::blocked_range<int>& r, Result local) -> Result {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int8_t blobMask = 0;
                    NegamaxParIncAI_Parallelsearch worker = *this;
                    worker.state_.applyMove(moves[i], current, blobMask);

                    int16_t localAlpha = alpha;
                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search         (depth-1, opponent(current), -beta, -localAlpha)
                        : -worker.Parallelsearch (depth-1, opponent(current), -beta, -localAlpha);

                    if (score > local.score) { local.score = score; local.index = i; }
                }
                return local;
            },
            [](Result a, Result b) -> Result { return (a.score > b.score) ? a : b; }
        );

        if (result.index >= 0) bestMove_ = moves[result.index];
        else                   bestMove_ = Move{-1,-1,-1,-1};
        return result.score;
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
