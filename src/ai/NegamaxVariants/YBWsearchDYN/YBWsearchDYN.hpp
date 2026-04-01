#pragma once
// YBWsearchDYN : YBWsearch + iterative deepening (time limit)
//                + killer heuristic + best-move reordering from previous iteration.

#include "AIBase.hpp"
#include "IncMoveState.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <vector>
#include <numeric>
#include <algorithm>
#include <string>
#include <chrono>
#include <cstring>

extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

class NegamaxParIncAI_YBWsearchDYN : public AIBase {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr int MAX_DEPTH         = 32;
    static constexpr int MAX_KILLERS       = 10;
    static constexpr int PARALLEL_THRESHOLD = 2;

public:
    explicit NegamaxParIncAI_YBWsearchDYN(int timeLimitMs = 1000, int maxDepth = 20)
        : timeLimitMs_(timeLimitMs), maxDepth_(maxDepth),
          timeUp_(nullptr), ownsTimeUp_(false),
          prevBestMove_{-1,-1,-1,-1}
    {
        std::memset(killers_,      0, sizeof(killers_));
        std::memset(killerCount_,  0, sizeof(killerCount_));
    }

    NegamaxParIncAI_YBWsearchDYN(const NegamaxParIncAI_YBWsearchDYN& o)
        : timeLimitMs_(o.timeLimitMs_), maxDepth_(o.maxDepth_),
          state_(o.state_), bestMove_(o.bestMove_),
          deadline_(o.deadline_), timeUp_(o.timeUp_), ownsTimeUp_(false),
          prevBestMove_(o.prevBestMove_)
    {
        std::memcpy(killers_,     o.killers_,     sizeof(killers_));
        std::memcpy(killerCount_, o.killerCount_, sizeof(killerCount_));
    }

    NegamaxParIncAI_YBWsearchDYN& operator=(const NegamaxParIncAI_YBWsearchDYN& o) {
        timeLimitMs_  = o.timeLimitMs_;
        maxDepth_     = o.maxDepth_;
        state_        = o.state_;
        bestMove_     = o.bestMove_;
        deadline_     = o.deadline_;
        timeUp_       = o.timeUp_;
        ownsTimeUp_   = false;
        prevBestMove_ = o.prevBestMove_;
        std::memcpy(killers_,     o.killers_,     sizeof(killers_));
        std::memcpy(killerCount_, o.killerCount_, sizeof(killerCount_));
        return *this;
    }

    ~NegamaxParIncAI_YBWsearchDYN() {
        if (ownsTimeUp_) delete timeUp_;
    }

    std::string name() const override {
        return "NegamaxParInc_YBWsearchDYN(t=" + std::to_string(timeLimitMs_) + "ms)";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());
        state_ = IncMoveState::fromBoard(b);

        // Reset killers and best move for this call
        std::memset(killers_,     0, sizeof(killers_));
        std::memset(killerCount_, 0, sizeof(killerCount_));
        prevBestMove_ = {-1, -1, -1, -1};

        if (!ownsTimeUp_) {
            if (timeUp_) delete timeUp_;
            timeUp_     = new std::atomic<bool>(false);
            ownsTimeUp_ = true;
        }

        deadline_ = Clock::now() + std::chrono::milliseconds(timeLimitMs_);

        Move bestCompleted = {-1, -1, -1, -1};

        for (int d = 1; d <= maxDepth_; ++d) {
            if (d > 1 && Clock::now() >= deadline_) break;
            bestMove_ = {-1, -1, -1, -1};
            timeUp_->store(false, std::memory_order_relaxed);

            YBWsearch(d, p, -10000, 10000);

            if (!timeUp_->load(std::memory_order_relaxed)) {
                bestCompleted = bestMove_;
                prevBestMove_ = bestCompleted;   // update for next iteration
            } else {
                break;
            }
        }

        return (bestCompleted.x1 != -1) ? bestCompleted : bestMove_;
    }

private:
    int          timeLimitMs_;
    int          maxDepth_;
    IncMoveState state_;
    Move         bestMove_;
    TimePoint           deadline_;
    std::atomic<bool>*  timeUp_;
    bool                ownsTimeUp_;

    Move    prevBestMove_;

    int64_t killers_     [MAX_DEPTH][MAX_KILLERS];
    int     killerCount_ [MAX_DEPTH];

    // ── Killer helpers ──────────────────────────────────────────────────────

    static inline int64_t encodeMove(const Move& m, int w) {
        return (1LL << (m.y1 * w + m.x1)) | (1LL << (m.y2 * w + m.x2));
    }

    inline bool isKiller(int depth, int64_t enc) const {
        const int cnt = killerCount_[depth];
        for (int i = 0; i < cnt; ++i)
            if (killers_[depth][i] == enc) return true;
        return false;
    }

    inline void storeKiller(int depth, int64_t enc) {
        if (isKiller(depth, enc)) return;
        const int cnt = killerCount_[depth];
        // shift right to make room at index 0
        const int end = std::min(cnt, MAX_KILLERS - 1);
        for (int i = end; i > 0; --i)
            killers_[depth][i] = killers_[depth][i-1];
        killers_[depth][0]  = enc;
        if (cnt < MAX_KILLERS) killerCount_[depth]++;
    }

    // ── YBWsearch : parallèle au-dessus du seuil ────────────────────────────

    inline int16_t YBWsearch(int depth, Player current,
                              int16_t alpha, int16_t beta)
    {
        if (timeUp_->load(std::memory_order_relaxed)) return 0;
        if (Clock::now() >= deadline_) {
            timeUp_->store(true, std::memory_order_relaxed);
            return 0;
        }

        const int pi = (current == Player::P1) ? 0 : 1;
        const int w  = state_.board.w;

        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());

        if (n == 0) { bestMove_ = {-1,-1,-1,-1}; return 0; }

        // Build order: heuristic sort first
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            BBState sa = state_.board, sb = state_.board;
            sa.applyMove(moves[a],  current);
            sb.applyMove(moves[b_], current);
            return sa.evalFor(current) > sb.evalFor(current);
        });

        // Best-move reordering: put prevBestMove_ at front if found
        if (prevBestMove_.x1 != -1) {
            for (int i = 1; i < n; ++i) {
                const Move& m = moves[order[i]];
                if (m.x1 == prevBestMove_.x1 && m.y1 == prevBestMove_.y1 &&
                    m.x2 == prevBestMove_.x2 && m.y2 == prevBestMove_.y2)
                {
                    // swap to front
                    std::swap(order[0], order[i]);
                    break;
                }
            }
        }

        // First move — sequential
        int16_t best;
        {
            int8_t blobMask = 0;
            state_.applyMove(moves[order[0]], current, blobMask);
            best = (depth - 1 <= PARALLEL_THRESHOLD)
                 ? -search    (depth-1, opponent(current), -beta, -alpha, depth-1)
                 : -YBWsearch (depth-1, opponent(current), -beta, -alpha);
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
                    if (timeUp_->load(std::memory_order_relaxed)) break;
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI_YBWsearchDYN worker = *this;
                    worker.state_.applyMove(moves[order[i]], current, blobMask);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search    (depth-1, opponent(current), -beta, -curAlpha, depth-1)
                        : -worker.YBWsearch (depth-1, opponent(current), -beta, -curAlpha);

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

        if (!timeUp_->load(std::memory_order_relaxed)) {
            const int64_t finalBest = atomicBest.load(std::memory_order_acquire);
            bestMove_ = moves[idx_of(finalBest)];
            return score_of(finalBest);
        }
        return score_of(atomicBest.load(std::memory_order_acquire));
    }

    // ── search : séquentiel avec killer heuristic ────────────────────────────

    inline int16_t search(int depth, Player current,
                          int16_t alpha, int16_t beta,
                          int killerDepth)
    {
        if (timeUp_->load(std::memory_order_relaxed)) return 0;

        if (state_.getStatus() != GameStatus::Ongoing || depth == 0)
            return state_.evalFor(current);

        const int pi = (current == Player::P1) ? 0 : 1;
        const int w  = state_.board.w;

        if (!state_.hasAnyMove(pi))
            return -search(depth-1, opponent(current), -beta, -alpha, killerDepth-1);

        // Collect and sort moves; killers first
        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());

        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);

        if (killerDepth >= 0 && killerDepth < MAX_DEPTH && killerCount_[killerDepth] > 0) {
            std::stable_sort(order.begin(), order.end(), [&](int a, int b_) {
                const bool ka = isKiller(killerDepth, encodeMove(moves[a],  w));
                const bool kb = isKiller(killerDepth, encodeMove(moves[b_], w));
                return (int)ka > (int)kb;   // killers first, otherwise stable
            });
        }

        int16_t best   = -10000;
        bool    cutoff = false;

        for (int i = 0; i < n && !cutoff; ++i) {
            const Move& m = moves[order[i]];
            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);
            const int16_t sc = -search(depth-1, opponent(current), -beta, -alpha, killerDepth-1);
            state_.removeMove(m, current, blobMask);

            if (sc > best)  best  = sc;
            if (sc > alpha) alpha = sc;
            if (beta <= alpha) {
                if (killerDepth >= 0 && killerDepth < MAX_DEPTH)
                    storeKiller(killerDepth, encodeMove(m, w));
                cutoff = true;
            }
        }

        return best;
    }

    // ── pack/unpack helpers ──────────────────────────────────────────────────

    static int64_t pack     (int16_t score, int idx) {
        return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx;
    }
    static int16_t score_of (int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of   (int64_t v) { return (int)(uint32_t)v; }
};
