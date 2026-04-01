#pragma once
#include "NegamaxConfig.hpp"
#include <string>
#include <atomic>
#include <iosfwd>

class AIBase;
enum class GameStatus;

extern std::atomic<bool> g_interrupted;

// ── Résultat d'un match (du point de vue du Stud) ────────────────────────────

struct MatchResult {
    int wins   = 0;
    int losses = 0;
    int draws  = 0;

    double winRate() const {
        int n = wins + losses + draws;
        return n > 0 ? (wins + 0.5 * draws) / n : 0.0;
    }
    void print(std::ostream& out) const;
};

// ── Optimizer : Phase1 (baseline vs YBW) + Phase2 (exponential backoff) ──────

class Optimizer {
public:
    // stateFile    : fichier de checkpoint (créé s'il n'existe pas)
    // gamesPerEval : parties par évaluation (alternance P1/P2)
    // opponentDepth: inutilisé (gardé pour compatibilité CLI)
    // timeLimitMs  : budget temps NegamaxYBWDynAI(timeLimitMs) adversaire
    // maxPasses    : passes max Phase 2
    // initCfg : config initiale si aucun checkpoint n'existe (ignoré si state file présent)
    Optimizer(std::string stateFile,
              int gamesPerEval  = 6,
              int opponentDepth = 6,
              int timeLimitMs   = 250,
              const NegamaxConfig* initCfg = nullptr);

    // Lance Phase1 puis Phase2.
    // maxPasses : nombre max d'itérations Phase 2.
    void run(int maxPasses = 5);

private:
    // ── État persistant ───────────────────────────────────────────────────────
    struct State {
        NegamaxConfig bestCfg;
        int    totalGames    = 0;
        bool   phase1Done    = false;
        // Phase 2 : curseur exponential backoff
        int    p2ParamIdx    = 0;   // paramètre courant (0..3)
        int    p2Wins        = 0;   // victoires consécutives avec l'incrément courant
        double p2Increment   = 0.0; // incrément courant (encodé comme double; entiers castés)
        int    p2Passes      = 0;   // passes Phase2 effectuées

        void        save(const std::string& path) const;
        static State load(const std::string& path);
    };

    // ── Helpers ───────────────────────────────────────────────────────────────
    GameStatus  runGame(AIBase* p1, AIBase* p2);
    MatchResult evalVsYBW(const NegamaxConfig& cfg);
    MatchResult evalVsRef(const NegamaxConfig& challenger, const NegamaxConfig& ref);
    void        saveState() const;
    void        log(const std::string& msg) const;
    void        logConfig(const NegamaxConfig& cfg) const;

    // ── Phase 1 : cherche une config baseline qui bat YBW ────────────────────
    void runPhase1();

    // ── Phase 2 : exponential backoff sur chaque paramètre ───────────────────
    void runPhase2(int maxPasses);

    // Applique un delta sur le paramètre d'index idx dans cfg.
    // Retourne false si la valeur résultante est hors bornes raisonnables.
    bool applyDelta(NegamaxConfig& cfg, int paramIdx, double delta) const;

    // Incrément de départ pour chaque paramètre.
    double startIncrement(int paramIdx) const;

    State       state_;
    std::string stateFile_;
    int         gamesPerEval_;
    int         timeLimitMs_;
};
