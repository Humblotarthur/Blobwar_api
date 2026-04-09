#include "BenchAPI.hpp"
#include "AlphaBetaAI.hpp"
#include "NegamaxAI.hpp"
#include "NegamaxParallelAI.hpp"
#include "NegamaxYBWAI.hpp"
#include "NegamaxParIncAI.hpp"
#include "YBWsearchZobrist.hpp"
#include "YBWsearchZobristDYN.hpp"
#include "NegamaxParIncAIStud.hpp"
#include "NegamaxParIncAIDynStud.hpp"
#include "NegamaxParIncAIDynStudPMR.hpp"
#include "NegamaxParIncAIDynStudPMR_LMR2.hpp"
#include "NegamaxParIncAIDynStudRandom.hpp"
#include "NegamaxYBWDynAI.hpp"
#include <chrono>
#include <stdexcept>
#include <cmath>
#include <map>
#include <iomanip>
#include <iostream>
#include <sstream>

using Clock = std::chrono::high_resolution_clock;

// ── Fabrique d'IA ─────────────────────────────────────────────────────────────

std::unique_ptr<AIBase> makeAI(const PlayerConfig& cfg) {
    if (cfg.algo == "ab")              return std::make_unique<AlphaBetaAI>(cfg.depth);
    if (cfg.algo == "negamax")         return std::make_unique<NegamaxAI>(cfg.depth);
    if (cfg.algo == "negamax_par")     return std::make_unique<NegamaxParallelAI>(cfg.depth);
    if (cfg.algo == "negamax_par_dyn") return std::make_unique<NegamaxYBWAI>(cfg.depth);
    if (cfg.algo == "negamax_par_inc") return std::make_unique<NegamaxParIncAI>(cfg.depth);
    if (cfg.algo == "ybwz")           return std::make_unique<NegamaxParIncAI_YBWsearchZobrist>(cfg.depth);
    if (cfg.algo == "stud") {
        NegamaxConfig c;
        try { c = NegamaxConfig::loadFromFile("../negamax_config.ini"); }
        catch (...) {}
        c.maxDepth = cfg.depth;
        return std::make_unique<NegamaxParIncAIStud>(c);
    }
    // ── Algos dynamiques (depth = time limit en ms) ───────────────────────────
    // ybw_dyn  : NegamaxYBWDynAI avec iterative deepening
    if (cfg.algo == "ybw_dyn")
        return std::make_unique<NegamaxYBWDynAI>(cfg.depth);
    // ybwz_dyn : YBWsearchZobrist + iterative deepening (version de référence propre)
    if (cfg.algo == "ybwz_dyn")
        return std::make_unique<NegamaxParIncAI_YBWsearchZobristDYN>(cfg.depth);
    // stud_dyn : NegamaxParIncAIDynStud chargeant ../negamax_config.ini
    if (cfg.algo == "stud_dyn") {
        NegamaxConfig c;
        try { c = NegamaxConfig::loadFromFile("../negamax_config.ini"); }
        catch (...) {}
        c.timeLimitMs = cfg.depth;
        return std::make_unique<NegamaxParIncAIDynStud>(c);
    }
    // stud_zero : NegamaxParIncAIDynStud sans aucun élagage (LMR/LMP=0), sort=eval
    if (cfg.algo == "stud_zero") {
        NegamaxConfig c;
        c.timeLimitMs            = cfg.depth;
        c.lmrAlpha               = 0.0;
        c.lmpEnabled             = false;
        c.lmpPruneRatio          = 0.0;
        c.lmrMinDepth            = 99;   // désactive LMR en pratique
        c.sortDepth              = 1;    // sort par eval statique (même niveau que YBWDyn)
        return std::make_unique<NegamaxParIncAIDynStud>(c);
    }
    // pmr_dyn : NegamaxParIncAIDynStud_PMR avec paramètres PMR activés (pmrRatio=0.2)
    if (cfg.algo == "pmr_dyn") {
        NegamaxConfig c;
        c.timeLimitMs = cfg.depth;
        c.pmrRatio    = 0.2;
        c.pmrMinDepth = 3;
        return std::make_unique<NegamaxParIncAIDynStud_PMR>(c);
    }
    // pmr_zero : NegamaxParIncAIDynStud_PMR avec pmrRatio=0 (base logic, no pruning)
    if (cfg.algo == "pmr_zero") {
        NegamaxConfig c;
        c.timeLimitMs = cfg.depth;
        c.pmrRatio    = 0.0;
        return std::make_unique<NegamaxParIncAIDynStud_PMR>(c);
    }
    // lmr2_zero : NegamaxParIncAIDynStud_PMR_LMR2 avec PMR=0, LMR désactivé
    if (cfg.algo == "lmr2_zero") {
        NegamaxConfig c;
        c.timeLimitMs = cfg.depth;
        c.pmrRatio    = 0.0;
        c.lmrEnabled  = false;
        return std::make_unique<NegamaxParIncAIDynStud_PMR_LMR2>(c);
    }
    // stud_random : NegamaxParIncAIDynStud epsilon-greedy (entraînement CNN)
    //   epsilon = cfg.randomEpsilon (defaut 0.1, configurable dans negamax_config.ini)
    if (cfg.algo == "stud_random") {
        NegamaxConfig c;
        try { c = NegamaxConfig::loadFromFile("../negamax_config.ini"); }
        catch (...) {}
        c.timeLimitMs = cfg.depth;
        return std::make_unique<NegamaxParIncAIDynStudRandom>(c);
    }
    // dyn_zero : NegamaxParIncAIDynStud avec PMR=0, LMR désactivé
    if (cfg.algo == "dyn_zero") {
        NegamaxConfig c;
        c.timeLimitMs = cfg.depth;
        c.pmrRatio    = 0.0;
        c.lmrEnabled  = false;
        return std::make_unique<NegamaxParIncAIDynStud>(c);
    }
    throw std::invalid_argument("Algo inconnu : " + cfg.algo);
}

// ── GameResult::byTranche ─────────────────────────────────────────────────────

std::vector<TrancheStats> GameResult::byTranche() const {
    // Regroupe par tranche de 10 coups
    std::map<int, TrancheStats> tmp;
    for (const auto& r : moves) {
        int t = r.moveNum / 10;
        auto& ts = tmp[t];
        ts.tranche = t;
        if (r.player == Player::P1) { ts.countP1++; ts.avgMsP1 += r.timeMs; }
        else                        { ts.countP2++; ts.avgMsP2 += r.timeMs; }
    }
    std::vector<TrancheStats> result;
    for (auto& [t, ts] : tmp) {
        if (ts.countP1 > 0) ts.avgMsP1 /= ts.countP1;
        if (ts.countP2 > 0) ts.avgMsP2 /= ts.countP2;
        int total = ts.countP1 + ts.countP2;
        ts.avgMsAll = total > 0
            ? (ts.avgMsP1 * ts.countP1 + ts.avgMsP2 * ts.countP2) / total
            : 0.0;
        result.push_back(ts);
    }
    return result;
}

// ── GameResult::print ─────────────────────────────────────────────────────────

void GameResult::print(std::ostream& out) const {
    std::string winner;
    if      (status == GameStatus::P1Wins) winner = nameP1 + " (P1)";
    else if (status == GameStatus::P2Wins) winner = nameP2 + " (P2)";
    else                                   winner = "Match nul";

    out << "\n=== Résultat : " << winner << " | " << scoreP1 << " - " << scoreP2 << " ===\n";
    out << "  P1 : " << nameP1 << "\n";
    out << "  P2 : " << nameP2 << "\n";
    out << "  Coups joués : " << moves.size() << "\n\n";

    auto tranches = byTranche();
    out << std::left
        << std::setw(10) << "Tranche"
        << std::setw(8)  << "Nb P1"
        << std::setw(14) << "Moy P1 (ms)"
        << std::setw(8)  << "Nb P2"
        << std::setw(14) << "Moy P2 (ms)"
        << std::setw(14) << "Moy tout (ms)"
        << "\n";
    out << std::string(68, '-') << "\n";

    for (const auto& ts : tranches) {
        int lo = ts.tranche * 10, hi = lo + 9;
        std::ostringstream label;
        label << lo << "-" << hi;
        out << std::left
            << std::setw(10) << label.str()
            << std::setw(8)  << ts.countP1
            << std::setw(14) << std::fixed << std::setprecision(2) << ts.avgMsP1
            << std::setw(8)  << ts.countP2
            << std::setw(14) << ts.avgMsP2
            << std::setw(14) << ts.avgMsAll
            << "\n";
    }
}

// ── Arena::run ────────────────────────────────────────────────────────────────

GameResult Arena::run(const Board& b,
                      const PlayerConfig& p1cfg,
                      const PlayerConfig& p2cfg) {
    auto ai1 = makeAI(p1cfg);
    auto ai2 = makeAI(p2cfg);

    GameResult result;
    result.nameP1 = ai1->name();
    result.nameP2 = ai2->name();

    Board board = b;
    Player current = Player::P1;
    int moveNum = 0;

    while (true) {
        GameStatus st = Rules::getStatus(board);
        if (st != GameStatus::Ongoing) { result.status = st; break; }

        // Passer le tour si aucun coup possible
        if (!Rules::canMove(board, current)) {
            current = opponent(current);
            if (!Rules::canMove(board, current)) {
                result.status = Rules::getStatus(board);
                break;
            }
            continue;
        }

        AIBase* ai = (current == Player::P1) ? ai1.get() : ai2.get();

        auto t0 = Clock::now();
        Move m = ai->chooseMove(board, current);
        double ms = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();

        if (!board.inBounds(m.x1, m.y1) || !board.inBounds(m.x2, m.y2)) {
            result.status = Rules::getStatus(board);
            break;
        }

        result.moves.push_back({moveNum++, current, ms});
        Rules::applyMove(board, m, current);
        current = opponent(current);
    }

    result.scoreP1 = board.countPieces(Player::P1);
    result.scoreP2 = board.countPieces(Player::P2);
    return result;
}

// ── Arena::runSeries ──────────────────────────────────────────────────────────

Arena::SeriesResult Arena::runSeries(const Board& b,
                                     const PlayerConfig& p1cfg,
                                     const PlayerConfig& p2cfg,
                                     int numGames) {
    SeriesResult sr;
    for (int i = 0; i < numGames; ++i) {
        auto gr = run(b, p1cfg, p2cfg);
        if      (gr.status == GameStatus::P1Wins) ++sr.p1Wins;
        else if (gr.status == GameStatus::P2Wins) ++sr.p2Wins;
        else                                       ++sr.draws;
        sr.games.push_back(std::move(gr));
    }
    return sr;
}

void Arena::SeriesResult::print(std::ostream& out) const {
    out << "\n=== Série : " << games.size() << " partie(s) ===\n";
    if (!games.empty()) {
        out << "  P1 : " << games[0].nameP1 << "\n";
        out << "  P2 : " << games[0].nameP2 << "\n";
    }
    out << "  Victoires P1 : " << p1Wins
        << "  |  Victoires P2 : " << p2Wins
        << "  |  Nuls : " << draws << "\n";

    // Tranches agrégées sur toutes les parties
    std::map<int, TrancheStats> agg;
    for (const auto& g : games) {
        for (const auto& ts : g.byTranche()) {
            auto& a = agg[ts.tranche];
            a.tranche  = ts.tranche;
            a.countP1  += ts.countP1;
            a.countP2  += ts.countP2;
            a.avgMsP1  += ts.avgMsP1 * ts.countP1;
            a.avgMsP2  += ts.avgMsP2 * ts.countP2;
        }
    }

    out << "\n--- Temps moyen par tranche (toutes parties) ---\n";
    out << std::left
        << std::setw(10) << "Tranche"
        << std::setw(8)  << "Nb P1"
        << std::setw(14) << "Moy P1 (ms)"
        << std::setw(8)  << "Nb P2"
        << std::setw(14) << "Moy P2 (ms)"
        << "\n";
    out << std::string(54, '-') << "\n";

    for (auto& [t, ts] : agg) {
        if (ts.countP1 > 0) ts.avgMsP1 /= ts.countP1;
        if (ts.countP2 > 0) ts.avgMsP2 /= ts.countP2;
        int lo = t * 10, hi = lo + 9;
        std::ostringstream label;
        label << lo << "-" << hi;
        out << std::left
            << std::setw(10) << label.str()
            << std::setw(8)  << ts.countP1
            << std::setw(14) << std::fixed << std::setprecision(2) << ts.avgMsP1
            << std::setw(8)  << ts.countP2
            << std::setw(14) << ts.avgMsP2
            << "\n";
    }
}
