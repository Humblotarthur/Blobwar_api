#pragma once
#include "AIBase.hpp"
#include "BBState.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <algorithm>
#include <string>

// Negamax alpha-beta parallèle (YBW).
// Fix : search() est un alpha-beta séquentiel exact.
//       La sélection du meilleur coup utilise un atomic<int64_t>
//       packant (score, index) pour éviter les fail-high et les races.
class NegamaxParDynAI : public AIBase {
public:
    explicit NegamaxParDynAI(int depth = 4) : depth_(depth) {}

    std::string name() const override {
        return "NegamaxParDyn(d=" + std::to_string(depth_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {

        BBState root_s = BBState::fromBoard(b);
        Move buf[BBState::MAX_MOVES];
        int n = root_s.genMoves(p, buf);
        if (n == 0) return {-1, -1, -1, -1};
       
        std::sort(buf, buf + n, [&](const Move& a, const Move& b_) {
            BBState sa = root_s, sb = root_s;
            sa.applyMove(a,  p);
            sb.applyMove(b_, p);
            return sa.evalFor(p) > sb.evalFor(p);
        });

        // best = (score << 32) | index  — mis à jour atomiquement ensemble
        std::atomic<int64_t> best{ pack(-10000, 0) };
        std::atomic<int16_t> globalAlpha{ (int16_t)-10000 };

        // YBW : 1er coup séquentiel, fenêtre pleine → score exact
        {
            BBState ns = root_s;
            ns.applyMove(buf[0], p);
            int16_t s0 = -search(ns, depth_ - 1, opponent(p), -10000, 10000);
            best.store(pack(s0, 0), std::memory_order_release);
            globalAlpha.store(s0, std::memory_order_release);
        }
        if (n == 1) return buf[0];

        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    BBState ns = root_s;
                    ns.applyMove(buf[i], p);
                    // Fenêtre étroite : seuls les coups > curAlpha sont exacts
                    int16_t score = -search(ns, depth_ - 1, opponent(p),
                                            -10000, -curAlpha);

                    // Mise à jour atomique (score, index) — pas de race possible
                    int64_t oldBest = best.load(std::memory_order_relaxed);
                    int64_t newBest = pack(score, i);
                    while (score > score_of(oldBest))
                        if (best.compare_exchange_weak(oldBest, newBest,
                                std::memory_order_release,
                                std::memory_order_relaxed))
                            break;

                    // globalAlpha séparé pour la fenêtre des prochains appels
                    int16_t old = globalAlpha.load(std::memory_order_relaxed);
                    while (score > old)
                        if (globalAlpha.compare_exchange_weak(old, score,
                                std::memory_order_release,
                                std::memory_order_relaxed))
                            break;
                }
            });

        return buf[idx_of(best.load(std::memory_order_acquire))];
    }

private:
    int depth_;

    // Alpha-beta séquentiel exact — plus de gAlpha inter-threads
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

    static int64_t pack(int16_t score, int idx) {
        return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx;
    }
    static int16_t score_of(int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of  (int64_t v) { return (int)(uint32_t)v; }
};
