#pragma once
#include "AIBase.hpp"
#include "IncMoveState.hpp"
#include "../Eval/Zobrist.hpp"
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <atomic>
#include <vector>
#include <numeric>
#include <algorithm>
#include <string>

// Globaux (définis dans le .cpp qui implémente l'init du plateau)
extern uint64_t neighborMask[64];
extern uint64_t reachAll[64];

// ─────────────────────────────────────────────────────────────────────────────
// NegamaxParIncAI
//
// Negamax alpha-beta parallèle (YBW) avec état incrémental apply/undo.
//
// Représentation des coups :
//   Plus de vecteurs moves_P1 / moves_P2.
//   Les coups sont générés à la demande via forEachMove() sur moveTable[64].
//
// Zobrist :
//   ztt_  : raw pointer partagé entre tous les workers (copie de *this).
//           Alloué dans chooseMove, jamais delete dans les copies.
//   hash_ : hash courant maintenu incrémentalement dans les variantes Zobrist.
//           Copié par valeur dans chaque worker → indépendant par thread.
// ─────────────────────────────────────────────────────────────────────────────
class NegamaxParIncAI : public AIBase {
public:
    explicit NegamaxParIncAI(int depth = 4)
        : depth_(depth), ztt_(nullptr), hash_(0), ownsZtt_(false) {}

    // Copie : ztt_ partagé (pointeur copié), pas de transfert de propriété
    NegamaxParIncAI(const NegamaxParIncAI& o)
        : depth_(o.depth_), state_(o.state_), bestMove_(o.bestMove_),
          ztt_(o.ztt_), hash_(o.hash_), ownsZtt_(false) {}

    NegamaxParIncAI& operator=(const NegamaxParIncAI& o) {
        depth_    = o.depth_;
        state_    = o.state_;
        bestMove_ = o.bestMove_;
        ztt_      = o.ztt_;
        hash_     = o.hash_;
        ownsZtt_  = false;
        return *this;
    }

    ~NegamaxParIncAI() { if (ownsZtt_) delete ztt_; }

    std::string name() const override {
        return "NegamaxParInc(d=" + std::to_string(depth_) + ")";
    }

    Move chooseMove(const Board& b, Player p) override {
        initNeighborMasks(b.width(), b.height());
        initReachAll(b.width(), b.height());

        state_    = IncMoveState::fromBoard(b);
        bestMove_ = Move{-1, -1, -1, -1};

        // // ── Init Zobrist ─────────────────────────────────────────────────────
        // // Alloué sur le heap (≈16 MB) — reset entre deux appels
        if (!ownsZtt_) {
            if (ztt_) delete ztt_;
            ztt_     = new ZobristTT();
            ownsZtt_ = true;
        } else {
            ztt_->init();   // reset la table (memset) entre deux coups
        }

        const int pi = (p == Player::P1) ? 0 : 1;
        hash_ = ztt_->computeHash(state_.board.bb[0], state_.board.bb[1], pi);
        // ────────────────────────────────────────────────────────────────────
        YBWsearchZobristPMR(depth_, p, -10000, 10000);
        return bestMove_;
    }

private:
    int          depth_;
    IncMoveState state_;
    Move         bestMove_;

    ZobristTT*  ztt_;      // partagé entre workers via copie du pointeur
    uint64_t    hash_;     // hash courant — copié par valeur dans chaque worker
    bool        ownsZtt_;  // true uniquement pour l'instance racine

    static constexpr int PARALLEL_THRESHOLD = 2;

    /*__________________________________________________________________________*/

    // ── YBWsearch : parallèle au-dessus du seuil  ───────────────────────────

    /*__________________________________________________________________________*/

    inline int16_t YBWsearch(int depth, Player current,
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
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            BBState sa = state_.board, sb = state_.board;
            sa.applyMove(moves[a],  current);
            sb.applyMove(moves[b_], current);
            return sa.evalFor(current) > sb.evalFor(current);
        });

        int16_t best;
        {
            int8_t blobMask = 0;
            state_.applyMove(moves[order[0]], current, blobMask);
            best = (depth - 1 <= PARALLEL_THRESHOLD)
                 ? -search    (depth-1, opponent(current), -beta, -alpha)
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
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI worker = *this;
                    worker.state_.applyMove(moves[order[i]], current, blobMask);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search    (depth-1, opponent(current), -beta, -curAlpha)
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

        const int64_t finalBest = atomicBest.load(std::memory_order_acquire);
        bestMove_ = moves[idx_of(finalBest)];
        return score_of(finalBest);
    }


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
                 ? -search    (depth-1, opponent(current), -beta, -alpha)
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
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI worker = *this;
                    worker.state_.applyMove(moves[order[i]], current, blobMask);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search    (depth-1, opponent(current), -beta, -curAlpha)
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

        const int64_t finalBest = atomicBest.load(std::memory_order_acquire);
        bestMove_ = moves[idx_of(finalBest)];
        return score_of(finalBest);
    }


    /*__________________________________________________________________________*/

    // ── YBWsearchtrinoYBW ────────────────────────────────────────────────────

    /*__________________________________________________________________________*/

    inline int16_t YBWsearchtrinoYBW(int depth, Player current,
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

        int best = -10000;
        bestMove_ = moves[order[0]];
        if (n == 1 || best >= beta) return best;

        std::atomic<int64_t> atomicBest  { pack(best, order[0]) };
        std::atomic<int16_t> globalAlpha { alpha };

        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI worker = *this;
                    worker.state_.applyMove(moves[order[i]], current, blobMask);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search    (depth-1, opponent(current), -beta, -curAlpha)
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

        const int64_t finalBest = atomicBest.load(std::memory_order_acquire);
        bestMove_ = moves[idx_of(finalBest)];
        return score_of(finalBest);
    }


    /*__________________________________________________________________________*/

    // ── YBWsearchnoatomic ────────────────────────────────────────────────────

    /*__________________________________________________________________________*/

    struct Result { int16_t score; int index; };

    inline int16_t YBWsearchnoatomic(int depth, Player current,
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
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            BBState sa = state_.board, sb = state_.board;
            sa.applyMove(moves[a],  current);
            sb.applyMove(moves[b_], current);
            return sa.evalFor(current) > sb.evalFor(current);
        });

        int16_t best;
        {
            int8_t blobMask = 0;
            state_.applyMove(moves[order[0]], current, blobMask);
            best = (depth-1 <= PARALLEL_THRESHOLD)
                 ? -search    (depth-1, opponent(current), -beta, -alpha)
                 : -YBWsearch (depth-1, opponent(current), -beta, -alpha);
            state_.removeMove(moves[order[0]], current, blobMask);
        }

        bestMove_ = moves[order[0]];
        if (n == 1 || best >= beta) return best;
        alpha = std::max(alpha, best);

        Result result = tbb::parallel_reduce(
            tbb::blocked_range<int>(1, n),
            Result{ -10000, -1 },
            [&](const tbb::blocked_range<int>& r, Result local) -> Result {
                for (int i = r.begin(); i != r.end(); ++i) {
                    int8_t blobMask = 0;
                    NegamaxParIncAI worker = *this;
                    const int moveIdx = order[i];
                    worker.state_.applyMove(moves[moveIdx], current, blobMask);

                    int16_t localAlpha = alpha;
                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search   (depth-1, opponent(current), -beta, -localAlpha)
                        : -worker.YBWsearch(depth-1, opponent(current), -beta, -localAlpha);

                    if (score > local.score) { local.score = score; local.index = moveIdx; }
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
                    NegamaxParIncAI worker = *this;
                    worker.state_.applyMove(moves[i], current, blobMask);

                    int16_t localAlpha = alpha;
                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.search   (depth-1, opponent(current), -beta, -localAlpha)
                        : -worker.YBWsearch(depth-1, opponent(current), -beta, -localAlpha);

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

    // ── search : séquentiel, apply/undo en place, zéro copie ─────────────────

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

    // ── searchZobrist : séquentiel avec table de transposition ───────────────
    //
    // Variante de search() enrichie d'un probe/store TT.
    // Le hash est passé par paramètre et mis à jour incrémentalement :
    //   XOR après applyMove → nouveau hash pour la récursion
    //   Pas de undo hash nécessaire (chaque frame a son propre hash local).
    //
    // ztt_ partagé → écriture lock-free, races acceptées pour les perfs.

    /*__________________________________________________________________________*/

    inline int16_t searchZobrist(int depth, Player current,
                                 int16_t alpha, int16_t beta,
                                 uint64_t hash)
    {
        const int pi = (current == Player::P1) ? 0 : 1;
        const int oi = 1 - pi;
        const int w  = state_.board.w;

        // ── Probe TT ─────────────────────────────────────────────────────────
        {
            const TTEntry* e = ztt_->probe(hash);
            if (e && e->depth >= depth) {
                if (e->flag == TT_EXACT)                        return e->score;
                if (e->flag == TT_LOWER && e->score > alpha)    alpha = e->score;
                if (e->flag == TT_UPPER && e->score < beta)     beta  = e->score;
                if (alpha >= beta)                               return e->score;
            }
        }

        // ── Cas de base ───────────────────────────────────────────────────────
        if (state_.getStatus() != GameStatus::Ongoing || depth == 0)
            return state_.evalFor(current);

        if (!state_.hasAnyMove(pi)) {
            // Passe le tour : bascule uniquement le bit joueur
            return -searchZobrist(depth-1, opponent(current),
                                  -beta, -alpha, hash ^ ztt_->sideToMove);
        }

        // ── Alpha-beta ────────────────────────────────────────────────────────
        const int16_t origAlpha = alpha;
        int16_t best   = -10000;
        bool    cutoff = false;

        state_.forEachMove(pi, [&](const Move& m) {
            if (cutoff) return;

            int8_t blobMask = 0;
            state_.applyMove(m, current, blobMask);

            // Hash incrémental : XOR après applyMove (blobMask connu)
            const int      dst     = m.y2 * w + m.x2;
            const uint64_t newHash = ztt_->applyHash(
                hash, m, blobMask, pi, oi, w, neighborMask[dst]);

            const int16_t sc = -searchZobrist(
                depth-1, opponent(current), -beta, -alpha, newHash);

            state_.removeMove(m, current, blobMask);
            // Pas de undo hash : newHash est local à cette frame

            if (sc > best)     best  = sc;
            if (sc > alpha)    alpha = sc;
            if (beta <= alpha) cutoff = true;
        });

        // ── Store TT ─────────────────────────────────────────────────────────
        const uint8_t flag = (best <= origAlpha) ? TT_UPPER
                           : (best >= beta)       ? TT_LOWER
                                                  : TT_EXACT;
        ztt_->store(hash, best, static_cast<int8_t>(depth), flag);

        return best;
    }


    /*__________________________________________________________________________*/

    // ── YBWsearchZobrist : parallèle avec table de transposition ────────────
    //
    // Variante de YBWsearch() avec probe/store TT.
    //
    // hash_ est un membre copié dans chaque worker (indépendant par thread).
    // ztt_  est un raw pointer partagé → TT commune à tous les workers.
    //        Les écritures concurrentes sont acceptées (last-write-wins).

    /*__________________________________________________________________________*/

    inline int16_t YBWsearchZobrist(int depth, Player current,
                                    int16_t alpha, int16_t beta)
    {
        const int pi = (current == Player::P1) ? 0 : 1;
        const int oi = 1 - pi;
        const int w  = state_.board.w;

        // Probe TT
        {
            const TTEntry* e = ztt_->probe(hash_);
            if (e && e->depth >= depth) {
                if (e->flag == TT_EXACT)                        return e->score;
                if (e->flag == TT_LOWER && e->score > alpha)    alpha = e->score;
                if (e->flag == TT_UPPER && e->score < beta)     beta  = e->score;
                if (alpha >= beta)                               return e->score;
            }
        }

        // Collecte des coups
        std::vector<Move> moves;
        moves.reserve(64);
        state_.forEachMove(pi, [&](const Move& m) { moves.push_back(m); });
        const int n = static_cast<int>(moves.size());

        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }

        // Tri heuristique
        std::vector<int> order(n);
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            BBState sa = state_.board, sb = state_.board;
            sa.applyMove(moves[a],  current);
            sb.applyMove(moves[b_], current);
            return sa.evalFor(current) > sb.evalFor(current);
        });

        // Premier coup séquentiel
        const int16_t origAlpha = alpha;
        int16_t best;
        {
            int8_t blobMask = 0;
            const Move& m0  = moves[order[0]];
            const int   dst = m0.y2 * w + m0.x2;

            state_.applyMove(m0, current, blobMask);

            // Hash incrémental pour le premier coup (XOR est son propre inverse)
            const uint64_t savedHash = hash_;
            hash_ = ztt_->applyHash(hash_, m0, blobMask, pi, oi, w, neighborMask[dst]);

            best = (depth-1 <= PARALLEL_THRESHOLD)
                 ? -search   (depth-1, opponent(current), -beta, -alpha)
                 : -YBWsearchZobrist(depth-1, opponent(current), -beta, -alpha);

            hash_ = savedHash; // restaure hash_ (XOR inverse ou sauvegarde)
            state_.removeMove(m0, current, blobMask);
        }

        bestMove_ = moves[order[0]];
        if (n == 1 || best >= beta) {
            ztt_->store(hash_, best, static_cast<int8_t>(depth),
                        (best >= beta) ? TT_LOWER : TT_EXACT);
            return best;
        }

        alpha = std::max(alpha, best);

        std::atomic<int64_t> atomicBest  { pack(best, order[0]) };
        std::atomic<int16_t> globalAlpha { alpha };

        //Coups restants en parallèle
        // Chaque worker = copie de *this.
        //   - state_ : copié (indépendant)
        //   - hash_  : copié par valeur (indépendant par thread)
        //   - ztt_   : pointeur copié (partagé → TT commune)
        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
                    if (curAlpha >= beta) break;

                    int8_t blobMask = 0;
                    NegamaxParIncAI worker = *this;   // copie complète

                    const Move& mv  = moves[order[i]];
                    const int   dst = mv.y2 * w + mv.x2;

                    worker.state_.applyMove(mv, current, blobMask);
                    // Mise à jour hash_ dans le worker (indépendant du parent)
                    worker.hash_ = worker.ztt_->applyHash(
                        worker.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);

                    const int16_t score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -worker.searchZobrist   (depth-1, opponent(current),
                                                   -beta, -curAlpha, worker.hash_)
                        : -worker.YBWsearchZobrist(depth-1, opponent(current),
                                                   -beta, -curAlpha);

                    // Mise à jour atomique atomicBest
                    int64_t oldBest = atomicBest.load(std::memory_order_relaxed);
                    while (score > score_of(oldBest)) {
                        if (atomicBest.compare_exchange_weak(oldBest, pack(score, order[i]),
                                std::memory_order_release, std::memory_order_relaxed)) break;
                    }
                    // Mise à jour atomique globalAlpha
                    int16_t oldAlpha = globalAlpha.load(std::memory_order_relaxed);
                    while (score > oldAlpha) {
                        if (globalAlpha.compare_exchange_weak(oldAlpha, score,
                                std::memory_order_release, std::memory_order_relaxed)) break;
                    }
                }
            });

        // Store TT et résultat final
        const int64_t finalBest  = atomicBest.load(std::memory_order_acquire);
        const int16_t finalScore = score_of(finalBest);
        const uint8_t flag = (finalScore <= origAlpha) ? TT_UPPER
                           : (finalScore >= beta)       ? TT_LOWER
                                                        : TT_EXACT;
        ztt_->store(hash_, finalScore, static_cast<int8_t>(depth), flag);

        bestMove_ = moves[idx_of(finalBest)];
        return finalScore;
    }
 
    /*__________________________________________________________________________*/
 
    // ── YBWsearchZobristPMR : parallèle avec TT, tri parallèle, LMR dynamique ──
    //
    // Réduction dynamique R(d, i) : chaque coup reçoit une réduction
    // individuelle croissante selon son rang i dans l'ordre heuristique.
    // Adapté aux espaces larges (20 → 150 coups) : pas de découpage fixe.
    //
    // Deux formules disponibles via USE_LOG_REDUCTION :
    //
    //   LOG  : R(d,i) = floor(LMR_ALPHA * log(d) * log(i+1))
    //          → réduction douce, croît lentement, bonne pour grands espaces
    //          → ajuster LMR_ALPHA (typiquement 0.3 à 0.7)
    //
    //   LIN  : R(d,i) = min(d-1, 1 + floor(i / LMR_K))
    //          → réduction linéaire par paliers, plus prévisible
    //          → ajuster LMR_K (typiquement 8 à 16)
    //
    // Profondeur effective du coup i : d' = d - 1 - R(d,i)
    // Clampée dans [1, d-1] pour ne jamais dépasser ou annuler la recherche.
    //
    // Conditions d'exemption (R = 0 forcé) :
    //   - coup i == 0 (toujours à profondeur pleine)
    //   - depth < LMR_MIN_DEPTH (trop près des feuilles)
    //
    // Re-search : si un coup réduit revient avec score > alpha, on le
    // re-cherche à profondeur pleine pour confirmer (null-window en option).
    //
    // LMP : décommenter le bloc ci-dessous pour élaguer les N derniers coups.
    //
    // ── Paramètres à ajuster empiriquement ──────────────────────────────────
    //
    //   SORT_DEPTH              profondeur du tri heuristique
    //                             1 = eval statique (rapide)
    //                             2 = mini-search   (meilleur ordre, 2× plus cher)
    //
    //   LMR_MIN_DEPTH           profondeur minimale pour activer la réduction
    //                             en dessous : tous les coups à profondeur pleine
    //
    //   USE_LOG_REDUCTION       true  = formule log (douce, grands espaces)
    //                           false = formule linéaire (paliers, plus simple)
    //
    //   LMR_ALPHA               coefficient de la formule log  [0.3 – 0.7]
    //
    //   LMR_K                   granularité de la formule lin  [8 – 16]
    //                             8  = réduction rapide (agressif)
    //                             16 = réduction lente  (conservateur)
    //
    //   LMR_NULLWINDOW_BEFORE_FULL
    //                           true  = re-search null-window puis full si passe
    //                                   (économique, recommandé)
    //                           false = re-search full direct si score > alpha
    //                                   (plus coûteux, plus précis)
    //
    //   LMP_PRUNE_RATIO         fraction des coups tail à élaguer [0.0 – 0.5]
    //                           ex: 0.25 = retire 25% des coups les plus faibles
    //                           Décommenter le bloc LMP pour activer.
 
    /*__________________________________________________________________________*/
 
    static constexpr int    SORT_DEPTH                 = 2;
    static constexpr int    LMR_MIN_DEPTH              = 3;
    static constexpr bool   USE_LOG_REDUCTION          = true;
    static constexpr double LMR_ALPHA                  = 0.5;  // formule log
    static constexpr int    LMR_K                      = 12;   // formule linéaire
    static constexpr bool   LMR_NULLWINDOW_BEFORE_FULL = true;
    // static constexpr double LMP_PRUNE_RATIO          = 0.25;
 
    // ── Calcul de R(d, i) ────────────────────────────────────────────────────
    // Retourne 0 si la réduction doit être désactivée (i=0 ou depth trop faible).
    static inline int computeReduction(int depth, int i) {
        if (i == 0 || depth < LMR_MIN_DEPTH) return 0;
 
        int R;
        if constexpr (USE_LOG_REDUCTION) {
            // R(d,i) = floor(LMR_ALPHA * log(d) * log(i+1))
            R = static_cast<int>(LMR_ALPHA
                                 * std::log(static_cast<double>(depth))
                                 * std::log(static_cast<double>(i + 1)));
        } else {
            // R(d,i) = min(d-1, 1 + floor(i / LMR_K))
            R = std::min(depth - 1, 1 + i / LMR_K);
        }
        return R; // clamp final dans l'appelant : d' = max(1, d-1-R)
    }
 
    inline int16_t YBWsearchZobristPMR(int depth, Player current,
                                    int16_t alpha, int16_t beta)
    {
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
        const int n = static_cast<int>(moves.size());
 
        if (n == 0) { bestMove_ = Move{-1,-1,-1,-1}; return 0; }
 
        // ── Tri heuristique à SORT_DEPTH (parallèle) ─────────────────────────
        // Calcul indépendant par coup → aucune dépendance entre threads.
        std::vector<int>     order(n);
        std::vector<int16_t> sortScores(n);
        std::iota(order.begin(), order.end(), 0);
 
        tbb::parallel_for(tbb::blocked_range<int>(0, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i) {
                    NegamaxParIncAI sorter = *this;
                    int8_t blobMask = 0;
                    sorter.state_.applyMove(moves[i], current, blobMask);
 
                    if constexpr (SORT_DEPTH <= 1) {
                        sortScores[i] = sorter.state_.evalFor(current);
                    } else {
                        // Mini-search sans élagage : fenêtre large intentionnelle
                        // pour ne pas biaiser l'ordre des coups
                        sortScores[i] = -sorter.search(SORT_DEPTH - 1,
                                                       opponent(current),
                                                       -10000, 10000);
                    }
                }
            });
 
        std::sort(order.begin(), order.end(), [&](int a, int b_) {
            return sortScores[a] > sortScores[b_];
        });
 
        // ── LMP (Late Move Pruning) ───────────────────────────────────────────
        // Retire une fraction des coups tail avant la recherche.
        // Décommenter et ajuster LMP_PRUNE_RATIO pour activer.
        //
        // if (depth <= 4) {
        //     const int toPrune = static_cast<int>(n * LMP_PRUNE_RATIO);
        //     // Les coups sont triés : les plus faibles sont en fin de order[]
        //     // On réduit n_effective plutôt que de modifier order[]
        //     n_effective = n - toPrune;  // utiliser n_effective à la place de n
        // }                              // dans les parallel_for ci-dessous
 
        // ── Premier coup : séquentiel (i=0, R=0, toujours à profondeur pleine)
        const int16_t origAlpha = alpha;
        int16_t best;
        {
            const Move&    m0        = moves[order[0]];
            const int      dst       = m0.y2 * w + m0.x2;
            int8_t         blobMask  = 0;
            const uint64_t savedHash = hash_;
 
            state_.applyMove(m0, current, blobMask);
            hash_ = ztt_->applyHash(hash_, m0, blobMask, pi, oi, w, neighborMask[dst]);
 
            best = (depth-1 <= PARALLEL_THRESHOLD)
                 ? -searchZobrist   (depth-1, opponent(current), -beta, -alpha, hash_)
                 : -YBWsearchZobristPMR(depth-1, opponent(current), -beta, -alpha);
 
            hash_ = savedHash;
            state_.removeMove(m0, current, blobMask);
        }
 
        bestMove_ = moves[order[0]];
        if (best >= beta) {
            ztt_->store(hash_, best, static_cast<int8_t>(depth), TT_LOWER);
            return best;
        }
        alpha = std::max(alpha, best);
 
        std::atomic<int64_t> atomicBest  { pack(best, order[0]) };
        std::atomic<int16_t> globalAlpha { alpha };
 
        // ── Lambda worker : coup d'index i avec réduction dynamique R(d,i) ───
        //
        // searchDepth = max(1, depth - 1 - R(depth, i))
        //
        // Si R > 0 et score > alpha → re-search à profondeur pleine :
        //   NULLWINDOW = true  : null-window puis full si passe  (recommandé)
        //   NULLWINDOW = false : full direct                     (agressif)
        auto runMove = [&](int rankIdx) {
 
            const int16_t curAlpha = globalAlpha.load(std::memory_order_acquire);
            if (curAlpha >= beta) return;
 
            const int   idx      = order[rankIdx];
            const Move& mv       = moves[idx];
            const int   dst      = mv.y2 * w + mv.x2;
            int8_t      blobMask = 0;
 
            // Réduction pour ce rang
            const int R           = computeReduction(depth, rankIdx);
            const int searchDepth = std::max(1, depth - 1 - R);
            const bool isReduced  = (R > 0);
 
            // Worker indépendant (copie state_ + hash_, ztt_ partagé)
            NegamaxParIncAI worker = *this;
            worker.state_.applyMove(mv, current, blobMask);
            worker.hash_ = worker.ztt_->applyHash(
                worker.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);
 
            // ── Recherche à profondeur réduite ────────────────────────────────
            int16_t score = (searchDepth - 1 <= PARALLEL_THRESHOLD)
                ? -worker.searchZobrist   (searchDepth-1, opponent(current),
                                           -beta, -curAlpha, worker.hash_)
                : -worker.YBWsearchZobristPMR(searchDepth-1, opponent(current),
                                           -beta, -curAlpha);
 
            // ── Re-search si le coup réduit semble bon ────────────────────────
            // Condition : réduction active ET score > alpha
            // → le coup est potentiellement meilleur que prévu
            if (isReduced && score > curAlpha) {
 
                if constexpr (LMR_NULLWINDOW_BEFORE_FULL) {
 
                    // Étape 1 : null-window à profondeur pleine ───────────────
                    // Coût minimal — confirme juste si le coup bat alpha.
                    // Si elle échoue : on garde le score réduit.
                    // Si elle passe : le coup est vraiment fort → full re-search.
                    NegamaxParIncAI verif = *this;
                    verif.state_.applyMove(mv, current, blobMask);
                    verif.hash_ = verif.ztt_->applyHash(
                        verif.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);
 
                    const int16_t nullScore = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -verif.searchZobrist   (depth-1, opponent(current),
                                                  -(curAlpha+1), -curAlpha,
                                                  verif.hash_)
                        : -verif.YBWsearchZobristPMR(depth-1, opponent(current),
                                                  -(curAlpha+1), -curAlpha);
 
                    if (nullScore > curAlpha) {
                        // Étape 2 : full re-search (score exact)
                        NegamaxParIncAI full = *this;
                        full.state_.applyMove(mv, current, blobMask);
                        full.hash_ = full.ztt_->applyHash(
                            full.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);
 
                        score = (depth-1 <= PARALLEL_THRESHOLD)
                            ? -full.searchZobrist   (depth-1, opponent(current),
                                                     -beta, -curAlpha, full.hash_)
                            : -full.YBWsearchZobristPMR(depth-1, opponent(current),
                                                     -beta, -curAlpha);
                    } else {
                        score = nullScore;
                    }
 
                } else {
                    // Re-search full direct sans null-window
                    NegamaxParIncAI full = *this;
                    full.state_.applyMove(mv, current, blobMask);
                    full.hash_ = full.ztt_->applyHash(
                        full.hash_, mv, blobMask, pi, oi, w, neighborMask[dst]);
 
                    score = (depth-1 <= PARALLEL_THRESHOLD)
                        ? -full.searchZobrist   (depth-1, opponent(current),
                                                 -beta, -curAlpha, full.hash_)
                        : -full.YBWsearchZobristPMR(depth-1, opponent(current),
                                                 -beta, -curAlpha);
                }
            }
 
            // ── Mise à jour atomique ──────────────────────────────────────────
            int64_t oldBest = atomicBest.load(std::memory_order_relaxed);
            while (score > score_of(oldBest)) {
                if (atomicBest.compare_exchange_weak(oldBest, pack(score, idx),
                        std::memory_order_release, std::memory_order_relaxed)) break;
            }
            int16_t oldAlpha = globalAlpha.load(std::memory_order_relaxed);
            while (score > oldAlpha) {
                if (globalAlpha.compare_exchange_weak(oldAlpha, score,
                        std::memory_order_release, std::memory_order_relaxed)) break;
            }
        };
 
        // ── Tous les coups restants en parallèle (i = 1 → n-1) ───────────────
        // Chaque coup reçoit sa propre réduction R(depth, i) via computeReduction.
        // R=0 pour les premiers → profondeur pleine automatiquement.
        // R croît progressivement → réduction lisse sur tout l'espace de coups.
        tbb::parallel_for(tbb::blocked_range<int>(1, n),
            [&](const tbb::blocked_range<int>& r) {
                for (int i = r.begin(); i != r.end(); ++i)
                    runMove(i);
            });
 
        // ── Store TT et résultat final ────────────────────────────────────────
        const int64_t finalBest  = atomicBest.load(std::memory_order_acquire);
        const int16_t finalScore = score_of(finalBest);
        const uint8_t flag = (finalScore <= origAlpha) ? TT_UPPER
                           : (finalScore >= beta)       ? TT_LOWER
                                                        : TT_EXACT;
        ztt_->store(hash_, finalScore, static_cast<int8_t>(depth), flag);
 
        bestMove_ = moves[idx_of(finalBest)];
        return finalScore;
    }
 

    /*__________________________________________________________________________*/

    // ── Utilitaires de packing atomique (score + index dans int64_t) ─────────

    /*__________________________________________________________________________*/

    static int64_t pack     (int16_t score, int idx) {
        return ((int64_t)(int32_t)score << 32) | (int64_t)(uint32_t)idx;
    }
    static int16_t score_of (int64_t v) { return (int16_t)(int32_t)(v >> 32); }
    static int     idx_of   (int64_t v) { return (int)(uint32_t)v; }
};
