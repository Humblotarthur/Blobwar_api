#pragma once
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
#include <cmath>
#include <string>
#include <chrono>
#include <cstring>

extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

// ─────────────────────────────────────────────────────────────────────────────
// NegamaxParIncAIStud
//
// Version configurable de NegamaxParIncAI.
// Tous les paramètres de recherche sont runtime via NegamaxConfig.
// L'original (NegamaxParIncAI) reste inchangé comme référence de performance.
// ─────────────────────────────────────────────────────────────────────────────
class NegamaxParIncAIStud : public AIBase {
    using Clock     = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    static constexpr int MAX_DEPTH   = 32;
    static constexpr int MAX_KILLERS = 10;

public:
    explicit NegamaxParIncAIStud()
        : cfg_(), ztt_(nullptr), hash_(0), ownsZtt_(false),
          timeUp_(nullptr), ownsTimeUp_(false),
          prevBestMove_{-1,-1,-1,-1} {}

    explicit NegamaxParIncAIStud(NegamaxConfig cfg)
        : cfg_(cfg), ztt_(nullptr), hash_(0), ownsZtt_(false),
          timeUp_(nullptr), ownsTimeUp_(false),
          prevBestMove_{-1,-1,-1,-1} {}

    static NegamaxParIncAIStud fromConfigFile(const std::string& path) {
        return NegamaxParIncAIStud(NegamaxConfig::loadFromFile(path));
    }

    NegamaxParIncAIStud(const NegamaxParIncAIStud& o)
        : cfg_(o.cfg_), state_(o.state_), bestMove_(o.bestMove_),
          ztt_(o.ztt_), hash_(o.hash_), ownsZtt_(false),
          deadline_(o.deadline_), timeUp_(o.timeUp_), ownsTimeUp_(false),
          prevBestMove_(o.prevBestMove_)
    {
        memcpy(killers_,     o.killers_,     sizeof(killers_));
        memcpy(killerCount_, o.killerCount_, sizeof(killerCount_));
    }

    NegamaxParIncAIStud& operator=(const NegamaxParIncAIStud& o) {
        cfg_         = o.cfg_;
        state_       = o.state_;
        bestMove_    = o.bestMove_;
        ztt_         = o.ztt_;
        hash_        = o.hash_;
        ownsZtt_     = false;
        deadline_    = o.deadline_;
        timeUp_      = o.timeUp_;
        ownsTimeUp_  = false;
        prevBestMove_ = o.prevBestMove_;
        memcpy(killers_,     o.killers_,     sizeof(killers_));
        memcpy(killerCount_, o.killerCount_, sizeof(killerCount_));
        return *this;
    }

    ~NegamaxParIncAIStud() {
        if (ownsZtt_)    delete ztt_;
        if (ownsTimeUp_) delete timeUp_;
    }

    std::string name() const override {
        return "NegamaxParIncStud(d=" + std::to_string(cfg_.maxDepth) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());

        state_    = IncMoveState::fromBoard(b);
        bestMove_ = Move{-1, -1, -1, -1};

        if (!ownsZtt_) {
            if (ztt_) delete ztt_;
            ztt_     = new ZobristTT();
            ownsZtt_ = true;
        } else {
            ztt_->init();
        }

        if (!ownsTimeUp_) {
            if (timeUp_) delete timeUp_;
            timeUp_     = new std::atomic<bool>(false);
            ownsTimeUp_ = true;
        }

        deadline_ = Clock::now() + std::chrono::milliseconds(cfg_.timeLimitMs);

        memset(killers_,     0, sizeof(killers_));
        memset(killerCount_, 0, sizeof(killerCount_));

        const int pi = (p == Player::P1) ? 0 : 1;
        Move bestCompleted = Move{-1, -1, -1, -1};
        prevBestMove_ = Move{-1, -1, -1, -1};

        for (int d = 1; d <= cfg_.maxDepth; ++d) {
            if (d > 1 && Clock::now() >= deadline_) break;

            bestMove_ = Move{-1, -1, -1, -1};
            timeUp_->store(false, std::memory_order_relaxed);
            hash_ = ztt_->computeHash(state_.board.bb[0], state_.board.bb[1], pi);

            YBWsearchZobristPMR(d, p, -10000, 10000);

            if (!timeUp_->load(std::memory_order_relaxed)) {
                bestCompleted = bestMove_;
                prevBestMove_ = bestMove_;
            } else {
                break;
            }
        }

        return (bestCompleted.x1 != -1) ? bestCompleted : bestMove_;
    }

private:
    NegamaxConfig cfg_;
    IncMoveState  state_;
    Move          bestMove_;

    ZobristTT*         ztt_;
    uint64_t           hash_;
    bool               ownsZtt_;
    TimePoint          deadline_;
    std::atomic<bool>* timeUp_;
    bool               ownsTimeUp_;

    Move    prevBestMove_;
    int64_t killers_[MAX_DEPTH][MAX_KILLERS];
    int     killerCount_[MAX_DEPTH];

    /*__________________________________________________________________________*/

    static int64_t encodeMove(const Move& m, int w) {
        return (1LL << (m.y1 * w + m.x1)) | (1LL << (m.y2 * w + m.x2));
    }

    void storeKiller(int depth, int64_t enc) {
        if (depth >= MAX_DEPTH) return;
        for (int i = 0; i < killerCount_[depth]; ++i)
            if (killers_[depth][i] == enc) return;
        if (killerCount_[depth] < MAX_KILLERS) ++killerCount_[depth];
        for (int i = killerCount_[depth]-1; i > 0; --i)
            killers_[depth][i] = killers_[depth][i-1];
        killers_[depth][0] = enc;
    }

    bool isKiller(int depth, int64_t enc) const {
        if (depth >= MAX_DEPTH) return false;
        for (int i = 0; i < killerCount_[depth]; ++i)
            if (killers_[depth][i] == enc) return true;
        return false;
    }

    /*__________________________________________________________________________*/

    inline int16_t search(int depth, Player current,
                          int16_t alpha, int16_t beta)
    {
        if (timeUp_ && timeUp_->load(std::memory_order_relaxed)) return 0;

        if (state_.getStatus() != GameStatus::Ongoing || depth == 0)
            return state_.evalFor(current);

        const int pi = (current == Player::P1) ? 0 : 1;
        const int w  = state_.board.w;

        if (!state_.hasAnyMove(pi))
            return -search(depth-1, opponent(current), -beta, -alpha);

        // Collect and sort: killers first
        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });

        std::stable_sort(moves.begin(), moves.end(), [&](const Move& a, const Move& b_) {
            return isKiller(depth, encodeMove(a, w)) > isKiller(depth, encodeMove(b_, w));
        });

        int16_t best   = -10000;
        bool    cutoff = false;

        for (const Move& m : moves) {
            if (cutoff) break;
            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);
            const int16_t sc = -search(depth-1, opponent(current), -beta, -alpha);
            state_.removeMove(m, current, blobMask);
            if (sc > best)  best  = sc;
            if (sc > alpha) alpha = sc;
            if (beta <= alpha) {
                storeKiller(depth, encodeMove(m, w));
                cutoff = true;
            }
        }

        return best;
    }

    /*__________________________________________________________________________*/

    inline int16_t searchZobrist(int depth, Player current,
                                 int16_t alpha, int16_t beta,
                                 uint64_t hash)
    {
        if (timeUp_ && timeUp_->load(std::memory_order_relaxed)) return 0;

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

        if (!state_.hasAnyMove(pi)) {
            return -searchZobrist(depth-1, opponent(current),
                                  -beta, -alpha, hash ^ ztt_->sideToMove);
        }

        const int16_t origAlpha = alpha;
        int16_t best   = -10000;
        bool    cutoff = false;

        state_.forEachMove(pi, [&](const Move& m) {
            if (cutoff) return;

            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);

            const int      dst     = m.y2 * w + m.x2;
            const uint64_t newHash = ztt_->applyHash(
                hash, m, blobMask, pi, oi, w, neighborMask[dst]);

            const int16_t sc = -searchZobrist(
                depth-1, opponent(current), -beta, -alpha, newHash);

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

    /*__________________________________________________________________________*/

    // ── Calcul de R(d, i) — runtime (cfg_ au lieu de static constexpr) ───────
    inline int computeReduction(int depth, int i) const {
        if (i == 0 || depth < cfg_.lmrMinDepth) return 0;

        int R;
        if (cfg_.useLogReduction) {
            R = static_cast<int>(cfg_.lmrAlpha
                                 * std::log(static_cast<double>(depth))
                                 * std::log(static_cast<double>(i + 1)));
        } else {
            R = std::min(depth - 1, 1 + i / cfg_.lmrK);
        }
        return R;
    }

    inline int16_t YBWsearchZobristPMR(int depth, Player current,
                                       int16_t alpha, int16_t beta)
    {
        if (timeUp_->load(std::memory_order_relaxed)) return 0;
        if (Clock::now() >= deadline_) {
            timeUp_->store(true, std::memory_order_relaxed);
            return 0;
        }

        const int pi = (current == Player::P1) ? 0 : 1;
        const int oi = 1 - pi;
        const int w  = state_.board.w;

        // ── Probe TT ─────────────────────────────────────────────────────────
        {
            const TTEntry* e = ztt_->probe(hash_);
            if (e && e->depth >= depth) {
                if (e->flag == TT_EXACT)                     return e->score;
                if (e->flag == TT_LOWER && e->score > alpha) alpha = e->score;
                if (e->flag == TT_UPPER && e->score < beta)  beta  = e->score;
                if (alpha >= beta)                            return e->score;
            }
        }

        // ── Collecte des coups ────────────────────────────────────────────────
        std::vector<Move> moves;
        moves.reserve(128);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        int n = static_cast<int>(moves.size());

        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }

        // ── Tri heuristique à sortDepth (parallèle) ───────────────────────────
        std::vector<int>     order(n);
        std::vector<int16_t> sortScores(n);
        std::iota(order.begin(), order.end(), 0);

        tbb::parallel_for(tbb::blocked_range<int>(0, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    NegamaxParIncAIStud sorter = *this;
                    int8_t blobMask = 0;
                    sorter.state_.applyMove(moves[i], current, blobMask);

                    if (cfg_.sortDepth <= 1) {
                        sortScores[i] = sorter.state_.evalFor(current);
                    } else {
                        sortScores[i] = -sorter.search(cfg_.sortDepth - 1,
                                                       opponent(current),
                                                       -10000, 10000);
                    }
                }
            });

        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            return sortScores[a] > sortScores[b_];
        });

        // ── Best move reordering (previous iteration) ────────────────────────
        if (prevBestMove_.x1 != -1) {
            for (int i = 0; i < n; ++i) {
                const Move& mi = moves[order[i]];
                if (mi.x1 == prevBestMove_.x1 && mi.y1 == prevBestMove_.y1 &&
                    mi.x2 == prevBestMove_.x2 && mi.y2 == prevBestMove_.y2) {
                    std::swap(order[0], order[i]);
                    break;
                }
            }
        }

        // ── Killer reordering : killers en tête (après order[0]) ─────────────
        {
            int insertPos = (prevBestMove_.x1 != -1) ? 1 : 0;
            for (int i = insertPos + 1; i < n; ++i) {
                if (isKiller(depth, encodeMove(moves[order[i]], w))) {
                    // bubble it up to insertPos
                    for (int j = i; j > insertPos; --j)
                        std::swap(order[j], order[j-1]);
                    ++insertPos;
                }
            }
        }

        // ── LMP (Late Move Pruning) ───────────────────────────────────────────
        if (cfg_.lmpEnabled && depth <= 4) {
            const int toPrune = static_cast<int>(n * cfg_.lmpPruneRatio);
            n = std::max(1, n - toPrune);
        }

        // ── Premier coup : séquentiel (i=0, R=0) ─────────────────────────────
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
                 ? -search              (depth-1, opponent(current), -beta, -alpha)
                 : -YBWsearchZobristPMR (depth-1, opponent(current), -beta, -alpha);

            hash_ = savedHash;
            state_.removeMove(m0, current, blobMask);
        }

        bestMove_ = moves[order[0]];
        if (best >= beta) {
            storeKiller(depth, encodeMove(moves[order[0]], w));
            ztt_->store(hash_, best, static_cast<int8_t>(depth), TT_LOWER);
            return best;
        }
        alpha = std::max(alpha, best);

        std::atomic<int64_t> atomicBest  { pack(best, order[0]) };
        std::atomic<int16_t> globalAlpha { alpha };

        auto runMove = [&](int rankIdx) {
            if (timeUp_->load(std::memory_order_relaxed)) return;
            const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
            if (curAlpha >= beta) return;

            const int   idx      = order[rankIdx];
            const Move& mv       = moves[idx];
            const int   dst      = mv.y2 * w + mv.x2;
            int8_t      blobMask = 0;

            const int R           = computeReduction(depth, rankIdx);
            const int searchDepth = std::max(1, depth - 1 - R);
            const bool isReduced  = (R > 0);

            NegamaxParIncAIStud worker = *this;
            worker.state_.applyMove(mv, current, blobMask);
            worker.hash_ = worker.ztt_->applyHash(
                worker.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

            int16_t score = (searchDepth - 1 <= cfg_.parallelThreshold)
                ? -worker.search              (searchDepth-1, opponent(current),
                                               -beta, -curAlpha)
                : -worker.YBWsearchZobristPMR (searchDepth-1, opponent(current),
                                               -beta, -curAlpha);

            if (isReduced && score > curAlpha) {
                if (cfg_.lmrNullWindowBeforeFull) {
                    NegamaxParIncAIStud verif = *this;
                    verif.state_.applyMove(mv, current, blobMask);
                    verif.hash_ = verif.ztt_->applyHash(
                        verif.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

                    const int16_t nullScore = (depth-1 <= cfg_.parallelThreshold)
                        ? -verif.search              (depth-1, opponent(current),
                                                      -(curAlpha+1), -curAlpha)
                        : -verif.YBWsearchZobristPMR (depth-1, opponent(current),
                                                      -(curAlpha+1), -curAlpha);

                    if (nullScore > curAlpha) {
                        NegamaxParIncAIStud full = *this;
                        full.state_.applyMove(mv, current, blobMask);
                        full.hash_ = full.ztt_->applyHash(
                            full.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

                        score = (depth-1 <= cfg_.parallelThreshold)
                            ? -full.search              (depth-1, opponent(current),
                                                         -beta, -curAlpha)
                            : -full.YBWsearchZobristPMR (depth-1, opponent(current),
                                                         -beta, -curAlpha);
                    } else {
                        score = nullScore;
                    }
                } else {
                    NegamaxParIncAIStud full = *this;
                    full.state_.applyMove(mv, current, blobMask);
                    full.hash_ = full.ztt_->applyHash(
                        full.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

                    score = (depth-1 <= cfg_.parallelThreshold)
                        ? -full.search              (depth-1, opponent(current),
                                                     -beta, -curAlpha)
                        : -full.YBWsearchZobristPMR (depth-1, opponent(current),
                                                     -beta, -curAlpha);
                }
            }

            int64_t oldBest = atomicBest.load(std::memory_order_relaxed);
            while (score > score_of(oldBest)) {
                if (atomicBest.compare_exchange_weak(oldBest, pack(score, idx),
                        std::memory_order_release, std::memory_order_relaxed)) {
                    if (score >= beta)
                        storeKiller(depth, encodeMove(mv, w));
                    break;
                }
            }
            int16_t oldAlpha = globalAlpha.load(std::memory_order_relaxed);
            while (score > oldAlpha) {
                if (globalAlpha.compare_exchange_weak(oldAlpha, score,
                        std::memory_order_release, std::memory_order_relaxed)) break;
            }
        };

        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i)
                    runMove(i);
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

    /*__________________________________________________________________________*/

    static int64_t pack     (int16_t score, int idx) {
        return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx;
    }
    static int16_t score_of (int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of   (int64_t v) { return (int)(uint32_t)v; }
};
