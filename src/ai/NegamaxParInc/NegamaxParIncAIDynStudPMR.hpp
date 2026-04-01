#pragma once
// NegamaxParIncAIDynStud_PMR — YBWsearchZobristDYN + PMR (Pruning Move Ratio).
// Base propre paramétrable via NegamaxConfig.
// Rebuild from scratch depuis YBWsearchZobristDYN — sans killer/sortDepth/LMR.

#include "AIBase.hpp"
#include "IncMoveState.hpp"
#include "NegamaxConfig.hpp"
#include "../Eval/Zobrist.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <vector>
#include <numeric>
#include <algorithm>
#include <string>
#include <chrono>

extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

class NegamaxParIncAIDynStud_PMR : public AIBase {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

public:
    explicit NegamaxParIncAIDynStud_PMR(NegamaxConfig cfg = {})
        : cfg_(cfg), ztt_(nullptr), hash_(0), ownsZtt_(false),
          timeUp_(nullptr), ownsTimeUp_(false) {}

    static NegamaxParIncAIDynStud_PMR fromConfigFile(const std::string& path) {
        return NegamaxParIncAIDynStud_PMR(NegamaxConfig::loadFromFile(path));
    }

    NegamaxParIncAIDynStud_PMR(const NegamaxParIncAIDynStud_PMR& o)
        : cfg_(o.cfg_), state_(o.state_), bestMove_(o.bestMove_),
          ztt_(o.ztt_), hash_(o.hash_), ownsZtt_(false),
          deadline_(o.deadline_), timeUp_(o.timeUp_), ownsTimeUp_(false) {}

    NegamaxParIncAIDynStud_PMR& operator=(const NegamaxParIncAIDynStud_PMR& o) {
        cfg_        = o.cfg_;
        state_      = o.state_;
        bestMove_   = o.bestMove_;
        ztt_        = o.ztt_;
        hash_       = o.hash_;
        ownsZtt_    = false;
        deadline_   = o.deadline_;
        timeUp_     = o.timeUp_;
        ownsTimeUp_ = false;
        return *this;
    }

    ~NegamaxParIncAIDynStud_PMR() {
        if (ownsZtt_)    delete ztt_;
        if (ownsTimeUp_) delete timeUp_;
    }

    std::string name() const override {
        return "NegamaxParIncAIDynStud_PMR(t=" + std::to_string(cfg_.timeLimitMs)
             + "ms,pmr=" + std::to_string(cfg_.pmrRatio) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());
        state_ = IncMoveState::fromBoard(b);

        if (!ownsZtt_) { if (ztt_) delete ztt_; ztt_ = new ZobristTT(); ownsZtt_ = true; }
        else            { ztt_->init(); }

        if (!ownsTimeUp_) { if (timeUp_) delete timeUp_; timeUp_ = new std::atomic<bool>(false); ownsTimeUp_ = true; }

        deadline_ = Clock::now() + std::chrono::milliseconds(cfg_.timeLimitMs);

        const int pi = (p == Player::P1) ? 0 : 1;
        Move bestCompleted = Move{-1, -1, -1, -1};

        for (int d = 1; d <= cfg_.maxDepth; ++d) {
            if (d > 1 && Clock::now() >= deadline_) break;
            bestMove_ = Move{-1, -1, -1, -1};
            timeUp_->store(false, std::memory_order_relaxed);
            hash_ = ztt_->computeHash(state_.board.bb[0], state_.board.bb[1], pi);
            YBWsearch(d, p, -10000, 10000);
            if (!timeUp_->load(std::memory_order_relaxed))
                bestCompleted = bestMove_;
            else
                break;
        }
        return (bestCompleted.x1 != -1) ? bestCompleted : bestMove_;
    }

private:
    NegamaxConfig       cfg_;
    IncMoveState        state_;
    Move                bestMove_;
    ZobristTT*          ztt_;
    uint64_t            hash_;
    bool                ownsZtt_;
    TimePoint           deadline_;
    std::atomic<bool>*  timeUp_;
    bool                ownsTimeUp_;

    // ─── YBW root-parallel + Zobrist TT + PMR (null-window verification) ───────
    inline int16_t YBWsearch(int depth, Player current,
                              int16_t alpha, int16_t beta)
    {
        if (timeUp_->load(std::memory_order_relaxed)) return 0;
        if (Clock::now() >= deadline_) { timeUp_->store(true, std::memory_order_relaxed); return 0; }

        const int pi = (current == Player::P1) ? 0 : 1;
        const int oi = 1 - pi;
        const int w  = state_.board.w;

        {
            const TTEntry* e = ztt_->probe(hash_);
            if (e && e->depth >= depth) {
                if (e->flag == TT_EXACT)                        return e->score;
                if (e->flag == TT_LOWER && e->score > alpha)    alpha = e->score;
                if (e->flag == TT_UPPER && e->score < beta)     beta  = e->score;
                if (alpha >= beta)                               return e->score;
            }
        }

        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());
        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }

        // Tri par eval statique
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            BBState sa = state_.board, sb = state_.board;
            sa.applyMove(moves[a],  current);
            sb.applyMove(moves[b_], current);
            return sa.evalFor(current) > sb.evalFor(current);
        });

        // Coup 0 : séquentiel, PV node
        const int16_t origAlpha = alpha;
        int16_t best;
        {
            const Move&    m0        = moves[order[0]];
            const int      dst       = m0.y2 * w + m0.x2;
            int8_t         blobMask  = 0;
            const uint64_t savedHash = hash_;
            state_.applyMove(m0, current, blobMask);
            hash_ = ztt_->applyHash(hash_, m0, blobMask, pi, oi, w, neighborMask[dst]);
            best = (depth-1 <= cfg_.parallelThreshold)
                 ? -searchZobrist(depth-1, opponent(current), -beta, -alpha, hash_)
                 : -YBWsearch    (depth-1, opponent(current), -beta, -alpha);
            hash_ = savedHash;
            state_.removeMove(m0, current, blobMask);
        }

        bestMove_ = moves[order[0]];
        if (best >= beta) {
            ztt_->store(hash_, best, static_cast<int8_t>(depth), TT_LOWER);
            return best;
        }
        alpha = std::max(alpha, best);

        // PMR : null-window verification pour filtrer les pires coups
        const double pmrRatio = (depth >= cfg_.pmrMinDepth) ? cfg_.pmrRatio : 0.0;
        const int    nSafe    = std::max(1, n - static_cast<int>(n * pmrRatio));

        std::vector<int> filteredOrder;
        filteredOrder.reserve(n);
        filteredOrder.push_back(order[0]);

        // Coups 1..nSafe-1 : toujours conservés
        for (int i = 1; i < nSafe && i < n; ++i)
            filteredOrder.push_back(order[i]);

        // Coups nSafe..n-1 : vérification null-window à depth 1
        const Player opp = opponent(current);
        for (int i = nSafe; i < n; ++i) {
            if (timeUp_->load(std::memory_order_relaxed)) break;
            const Move& mv  = moves[order[i]];
            const int   dst = mv.y2 * w + mv.x2;
            int8_t      blobMask  = 0;
            const uint64_t savedHash = hash_;
            state_.applyMove(mv, current, blobMask);
            hash_ = ztt_->applyHash(hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);
            const int16_t sc = -searchZobrist(1, opp, -(alpha+1), -alpha, hash_);
            hash_ = savedHash;
            state_.removeMove(mv, current, blobMask);
            if (sc > alpha)
                filteredOrder.push_back(order[i]);
        }
        if (timeUp_->load(std::memory_order_relaxed)) return 0;

        int nExplored = static_cast<int>(filteredOrder.size());
        if (nExplored == 1) {
            ztt_->store(hash_, best, static_cast<int8_t>(depth), TT_EXACT);
            return best;
        }

        std::atomic<int64_t> atomicBest  { pack(best, filteredOrder[0]) };
        std::atomic<int16_t> globalAlpha { alpha };

        tbb::parallel_for(tbb::blocked_range<int>(1, nExplored),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    if (timeUp_->load(std::memory_order_relaxed)) break;
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAIDynStud_PMR worker = *this;
                    const int   moveIdx = filteredOrder[i];
                    const Move& mv      = moves[moveIdx];
                    const int   dst     = mv.y2 * w + mv.x2;
                    worker.state_.applyMove(mv, current, blobMask);
                    worker.hash_ = worker.ztt_->applyHash(
                        worker.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

                    const int16_t score = (depth-1 <= cfg_.parallelThreshold)
                        ? -worker.searchZobrist(depth-1, opponent(current), -beta, -curAlpha, worker.hash_)
                        : -worker.YBWsearch    (depth-1, opponent(current), -beta, -curAlpha);

                    int64_t oldBest = atomicBest.load(std::memory_order_relaxed);
                    while (score > score_of(oldBest))
                        if (atomicBest.compare_exchange_weak(oldBest, pack(score, moveIdx),
                                std::memory_order_release, std::memory_order_relaxed)) break;
                    int16_t oldAlpha = globalAlpha.load(std::memory_order_relaxed);
                    while (score > oldAlpha)
                        if (globalAlpha.compare_exchange_weak(oldAlpha, score,
                                std::memory_order_release, std::memory_order_relaxed)) break;
                }
            });

        const int64_t finalBest  = atomicBest.load(std::memory_order_acquire);
        const int16_t finalScore = score_of(finalBest);
        if (!timeUp_->load(std::memory_order_relaxed)) {
            const uint8_t flag = (finalScore <= origAlpha) ? TT_UPPER
                               : (finalScore >= beta)       ? TT_LOWER
                                                            : TT_EXACT;
            ztt_->store(hash_, finalScore, static_cast<int8_t>(depth), flag);
            bestMove_ = moves[idx_of(finalBest)];
        }
        return finalScore;
    }

    // ─── Recherche séquentielle profonde (avec Zobrist TT) ───────────────────
    inline int16_t searchZobrist(int depth, Player current,
                                 int16_t alpha, int16_t beta, uint64_t hash)
    {
        if (timeUp_->load(std::memory_order_relaxed)) return 0;

        const int pi = (current == Player::P1) ? 0 : 1;
        const int oi = 1 - pi;
        const int w  = state_.board.w;

        {
            const TTEntry* e = ztt_->probe(hash);
            if (e && e->depth >= depth) {
                if (e->flag == TT_EXACT)                        return e->score;
                if (e->flag == TT_LOWER && e->score > alpha)    alpha = e->score;
                if (e->flag == TT_UPPER && e->score < beta)     beta  = e->score;
                if (alpha >= beta)                               return e->score;
            }
        }

        if (state_.getStatus() != GameStatus::Ongoing || depth == 0)
            return state_.evalFor(current);

        if (!state_.hasAnyMove(pi))
            return -searchZobrist(depth-1, opponent(current), -beta, -alpha,
                                  hash ^ ztt_->sideToMove);

        const int16_t origAlpha = alpha;
        int16_t best   = -10000;
        bool    cutoff = false;

        state_.forEachMove(pi, [&](const Move& m) {
            if (cutoff) return;
            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);
            const int      dst     = m.y2 * w + m.x2;
            const uint64_t newHash = ztt_->applyHash(hash, m, blobMask, pi, oi, w, neighborMask[dst]);
            const int16_t  sc      = -searchZobrist(depth-1, opponent(current), -beta, -alpha, newHash);
            state_.removeMove(m, current, blobMask);
            if (sc > best)     best  = sc;
            if (sc > alpha)    alpha = sc;
            if (beta <= alpha) cutoff = true;
        });

        const uint8_t flag = (best <= origAlpha) ? TT_UPPER
                           : (best >= beta)       ? TT_LOWER
                                                  : TT_EXACT;
        ztt_->store(hash, best, static_cast<int8_t>(depth), flag);
        return best;
    }

    static int64_t pack     (int16_t score, int idx) { return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx; }
    static int16_t score_of (int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of   (int64_t v) { return (int)(uint32_t)v; }
};
