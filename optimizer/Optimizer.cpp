#include "Optimizer.hpp"
#include "NegamaxParIncAIDynStudPMR.hpp"
#include "NegamaxYBWDynAI.hpp"
#include "Board.hpp"
#include "Rules.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <cstdio>   // std::rename
#include <algorithm>
#include <chrono>

using Clock     = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

static std::string fmtDuration(double sec) {
    int h = static_cast<int>(sec) / 3600;
    int m = (static_cast<int>(sec) % 3600) / 60;
    int s = static_cast<int>(sec) % 60;
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << h << "h"
        << std::setw(2) << m << "m"
        << std::setw(2) << s << "s";
    return oss.str();
}

static std::string boolStr(bool b) { return b ? "true" : "false"; }

static bool parseBool(const std::string& s) {
    return s == "1" || s == "true" || s == "yes";
}

static std::string trimStr(std::string s) {
    const char* ws = " \t\r\n";
    s.erase(0, s.find_first_not_of(ws));
    auto last = s.find_last_not_of(ws);
    if (last != std::string::npos) s.erase(last + 1); else s.clear();
    return s;
}

// ── MatchResult::print ────────────────────────────────────────────────────────

void MatchResult::print(std::ostream& out) const {
    out << "W=" << wins << " L=" << losses << " D=" << draws
        << " wr=" << std::fixed << std::setprecision(3) << winRate();
}

// ── State save / load ─────────────────────────────────────────────────────────

void Optimizer::State::save(const std::string& path) const {
    std::string tmp = path + ".tmp";
    {
        std::ofstream f(tmp);
        if (!f) throw std::runtime_error("Cannot write " + tmp);

        f << std::fixed << std::setprecision(6);
        f << "total_games="         << totalGames          << "\n"
          << "phase1_done="         << boolStr(phase1Done) << "\n"
          << "p2_param_idx="        << p2ParamIdx          << "\n"
          << "p2_wins="             << p2Wins              << "\n"
          << "p2_increment="        << p2Increment         << "\n"
          << "p2_passes="           << p2Passes            << "\n"
          // best config
          << "time_limit_ms="       << bestCfg.timeLimitMs << "\n"
          << "pmr_ratio="           << bestCfg.pmrRatio    << "\n"
          << "pmr_min_depth="       << bestCfg.pmrMinDepth << "\n";
    }
    std::rename(tmp.c_str(), path.c_str());
}

Optimizer::State Optimizer::State::load(const std::string& path) {
    State st;
    std::ifstream f(path);
    if (!f) return st;

    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trimStr(line.substr(0, eq));
        std::string v = trimStr(line.substr(eq + 1));
        if (k.empty() || v.empty()) continue;

        if      (k == "total_games")   st.totalGames          = std::stoi(v);
        else if (k == "phase1_done")   st.phase1Done          = parseBool(v);
        else if (k == "p2_param_idx")  st.p2ParamIdx          = std::stoi(v);
        else if (k == "p2_wins")       st.p2Wins              = std::stoi(v);
        else if (k == "p2_increment")  st.p2Increment         = std::stod(v);
        else if (k == "p2_passes")     st.p2Passes            = std::stoi(v);
        else if (k == "time_limit_ms") st.bestCfg.timeLimitMs = std::stoi(v);
        else if (k == "pmr_ratio")     st.bestCfg.pmrRatio    = std::stod(v);
        else if (k == "pmr_min_depth") st.bestCfg.pmrMinDepth = std::stoi(v);
    }
    return st;
}

// ── Optimizer constructor ─────────────────────────────────────────────────────

Optimizer::Optimizer(std::string stateFile, int gamesPerEval,
                     int /*opponentDepth*/, int timeLimitMs,
                     const NegamaxConfig* initCfg)
    : stateFile_(std::move(stateFile)),
      gamesPerEval_(gamesPerEval),
      timeLimitMs_(timeLimitMs)
{
    std::ifstream probe(stateFile_);
    bool hasCheckpoint = probe.good();
    probe.close();

    state_ = State::load(stateFile_);

    // Si pas de checkpoint ET config initiale fournie → l'utiliser comme point de depart
    if (!hasCheckpoint && initCfg) {
        state_.bestCfg = *initCfg;
    } else if (!hasCheckpoint) {
        // Defaults for PMR parameters
        state_.bestCfg.pmrRatio    = 0.2;
        state_.bestCfg.pmrMinDepth = 3;
    }

    state_.bestCfg.timeLimitMs = timeLimitMs_;
}

// ── Game runner ───────────────────────────────────────────────────────────────

GameStatus Optimizer::runGame(AIBase* p1, AIBase* p2) {
    Board board(8, 8);
    board.setupCross8x8();
    Player current = Player::P1;

    while (true) {
        GameStatus st = Rules::getStatus(board);
        if (st != GameStatus::Ongoing) return st;

        if (!Rules::canMove(board, current)) {
            current = opponent(current);
            if (!Rules::canMove(board, current))
                return Rules::getStatus(board);
            continue;
        }

        AIBase* ai = (current == Player::P1) ? p1 : p2;
        Move m = ai->chooseMove(board, current);

        if (!board.inBounds(m.x1, m.y1) || !board.inBounds(m.x2, m.y2))
            return Rules::getStatus(board);

        Rules::applyMove(board, m, current);
        current = opponent(current);
    }
}

// ── Config evaluators ─────────────────────────────────────────────────────────

MatchResult Optimizer::evalVsYBW(const NegamaxConfig& cfg) {
    MatchResult mr;
    for (int i = 0; i < gamesPerEval_; ++i) {
        NegamaxParIncAIDynStud_PMR stud(cfg);
        NegamaxYBWDynAI            opp(timeLimitMs_);

        GameStatus st = (i % 2 == 0)
            ? runGame(&stud, &opp)
            : runGame(&opp,  &stud);

        bool studIsP1 = (i % 2 == 0);
        if      (st == GameStatus::Draw)                                  ++mr.draws;
        else if ((studIsP1  && st == GameStatus::P1Wins) ||
                 (!studIsP1 && st == GameStatus::P2Wins))                 ++mr.wins;
        else                                                              ++mr.losses;
    }
    state_.totalGames += gamesPerEval_;
    return mr;
}

MatchResult Optimizer::evalVsRef(const NegamaxConfig& challenger,
                                  const NegamaxConfig& ref) {
    MatchResult mr;
    for (int i = 0; i < gamesPerEval_; ++i) {
        NegamaxParIncAIDynStud_PMR chall(challenger);
        NegamaxParIncAIDynStud_PMR refAI(ref);

        GameStatus st = (i % 2 == 0)
            ? runGame(&chall, &refAI)
            : runGame(&refAI, &chall);

        bool challIsP1 = (i % 2 == 0);
        if      (st == GameStatus::Draw)                                   ++mr.draws;
        else if ((challIsP1  && st == GameStatus::P1Wins) ||
                 (!challIsP1 && st == GameStatus::P2Wins))                 ++mr.wins;
        else                                                               ++mr.losses;
    }
    state_.totalGames += gamesPerEval_;
    return mr;
}

// ── Logging ───────────────────────────────────────────────────────────────────

void Optimizer::log(const std::string& msg) const {
    std::cout << msg << std::flush;
}

void Optimizer::saveState() const {
    state_.save(stateFile_);
}

void Optimizer::logConfig(const NegamaxConfig& cfg) const {
    std::ostringstream oss;
    oss << "  pmrRatio=" << std::fixed << std::setprecision(3) << cfg.pmrRatio
        << " pmrMinDepth=" << cfg.pmrMinDepth
        << "\n";
    log(oss.str());
}

// ── applyDelta & startIncrement ───────────────────────────────────────────────

// Parametre 0: pmrRatio (double), Parametre 1: pmrMinDepth (int)
double Optimizer::startIncrement(int paramIdx) const {
    switch (paramIdx) {
        case 0: return 0.05;  // pmrRatio    (double)
        case 1: return 1.0;   // pmrMinDepth (int)
        default: return 1.0;
    }
}

bool Optimizer::applyDelta(NegamaxConfig& cfg, int paramIdx, double delta) const {
    switch (paramIdx) {
        case 0: {
            double v = cfg.pmrRatio + delta;
            if (v < 0.05 || v > 0.6) return false;
            cfg.pmrRatio = v;
            return true;
        }
        case 1: {
            int v = cfg.pmrMinDepth + static_cast<int>(delta);
            if (v < 2 || v > 6) return false;
            cfg.pmrMinDepth = v;
            return true;
        }
        default: return false;
    }
}

// ── Phase 1 ───────────────────────────────────────────────────────────────────

void Optimizer::runPhase1() {
    log("━━━ Phase 1 : recherche config baseline qui bat YBW(" +
        std::to_string(timeLimitMs_) + "ms) ━━━\n");

    // Grilles de valeurs candidates
    const double pmrRatios[]    = {0.10, 0.15, 0.20, 0.25, 0.30, 0.40};
    const int    pmrMinDepths[] = {2, 3, 4};

    NegamaxConfig bestFound   = state_.bestCfg;
    double        bestWinRate = -1.0;
    bool          foundWinner = false;

    for (double r : pmrRatios) {
        for (int d : pmrMinDepths) {
            if (g_interrupted) goto phase1_done;

            NegamaxConfig cfg  = state_.bestCfg;
            cfg.pmrRatio       = r;
            cfg.pmrMinDepth    = d;

            std::ostringstream lbl;
            lbl << "[pmr=" << std::fixed << std::setprecision(3) << r
                << " pmd=" << d << "] ";
            log(lbl.str());

            MatchResult mr = evalVsYBW(cfg);
            mr.print(std::cout);

            double wr = mr.winRate();
            if (wr > bestWinRate) {
                bestWinRate = wr;
                bestFound   = cfg;
                log("  ★ NOUVEAU BEST");
                if (wr > 0.5) foundWinner = true;
            }
            log("\n");

            saveState();

            if (foundWinner) goto phase1_done;
        }
    }

phase1_done:
    state_.bestCfg    = bestFound;
    state_.phase1Done = true;
    saveState();

    log("\nPhase 1 terminee | winRate=" +
        std::to_string(bestWinRate) +
        (foundWinner ? " — bat YBW" : " — meilleure config retenue") + "\n");
    logConfig(state_.bestCfg);
}

// ── Phase 2 ───────────────────────────────────────────────────────────────────
// Parametres optimises dans l'ordre : pmrRatio, pmrMinDepth
// Pour chaque parametre : exponential backoff jusqu'a 3 victoires consecutives.

void Optimizer::runPhase2(int maxPasses) {
    // Nouvelle logique :
    // - Incrément fixe par paramètre (pas d'exponentiel)
    // - 5 parties décident le gagnant (winRate > 0.5)
    // - Si challenger gagne → nouveau best, on continue
    // - Si challenger perd → échec total++
    // - Après 3 échecs TOTAUX (pas consécutifs) → stop Phase 2
    const int NUM_PARAMS   = 2;
    const int MAX_FAILURES = 3;
    const char* paramNames[] = {"pmrRatio", "pmrMinDepth"};

    // Reprise : p2Wins stocke le compteur d'échecs totaux
    int totalFailures = state_.p2Wins;

    log("\n━━━ Phase 2 : incrémental, arrêt après " +
        std::to_string(MAX_FAILURES) + " échecs totaux (" +
        std::to_string(maxPasses) + " passes max) ━━━\n");
    logConfig(state_.bestCfg);

    for (; state_.p2Passes < maxPasses && !g_interrupted && totalFailures < MAX_FAILURES;
           ++state_.p2Passes) {

        log("\n--- Phase 2 passe " + std::to_string(state_.p2Passes + 1) +
            "/" + std::to_string(maxPasses) +
            " | échecs=" + std::to_string(totalFailures) + "/" +
            std::to_string(MAX_FAILURES) + " ---\n");

        for (int pidx = 0; pidx < NUM_PARAMS && !g_interrupted && totalFailures < MAX_FAILURES; ++pidx) {
            const double increment = startIncrement(pidx);

            NegamaxConfig challenger = state_.bestCfg;
            if (!applyDelta(challenger, pidx, increment)) {
                log("  [" + std::string(paramNames[pidx]) + "] borne atteinte, skip.\n");
                continue;
            }

            {
                std::ostringstream oss;
                oss << "  [" << paramNames[pidx] << "] +"
                    << std::fixed << std::setprecision(4) << increment << " -> ";
                log(oss.str());
                logConfig(challenger);
                log("  ");
            }

            MatchResult mr = evalVsRef(challenger, state_.bestCfg);
            mr.print(std::cout);

            if (mr.winRate() > 0.5 + 1e-6) {
                state_.bestCfg = challenger;
                log(" ★ validé\n");

                log("  [verif YBW] ");
                MatchResult ybwMr = evalVsYBW(state_.bestCfg);
                ybwMr.print(std::cout);
                log(ybwMr.winRate() > 0.5 ? " ✓\n" : " ⚠ ne bat plus YBW\n");
            } else {
                ++totalFailures;
                log(" ✗ échec (" + std::to_string(totalFailures) + "/" +
                    std::to_string(MAX_FAILURES) + ")\n");
            }

            state_.p2ParamIdx = pidx;
            state_.p2Wins     = totalFailures;  // réutilisé comme compteur d'échecs
            saveState();
        }

        log("\n  Fin passe | parties=" + std::to_string(state_.totalGames) + "\n");
        logConfig(state_.bestCfg);
    }

    log("\nPhase 2 terminée | échecs=" + std::to_string(totalFailures) + "/" +
        std::to_string(MAX_FAILURES) + "\n");
}

// ── run ───────────────────────────────────────────────────────────────────────

void Optimizer::run(int maxPasses) {
    const TimePoint startTime = Clock::now();

    log("Reprise depuis : phase1_done=" + boolStr(state_.phase1Done) +
        " p2_passes=" + std::to_string(state_.p2Passes) +
        " parties=" + std::to_string(state_.totalGames) + "\n\n");

    if (!state_.phase1Done && !g_interrupted)
        runPhase1();

    if (!g_interrupted)
        runPhase2(maxPasses);

    // Resume final
    double elapsed = std::chrono::duration<double>(Clock::now() - startTime).count();
    log("\n=== MEILLEURE CONFIGURATION TROUVEE (" + fmtDuration(elapsed) + ") ===\n");
    log("  parties totales : " + std::to_string(state_.totalGames) + "\n");
    log("  time_limit_ms   = " + std::to_string(state_.bestCfg.timeLimitMs) + "\n");
    log("  pmr_ratio       = " + std::to_string(state_.bestCfg.pmrRatio)    + "\n");
    log("  pmr_min_depth   = " + std::to_string(state_.bestCfg.pmrMinDepth) + "\n");

    if (g_interrupted) {
        log("\n[Interrompu] Etat sauvegarde -> " + stateFile_ + "\n");
    }

    state_.bestCfg.save(stateFile_ + ".best.ini");
    log("\nConfig exportee -> " + stateFile_ + ".best.ini\n");
}
