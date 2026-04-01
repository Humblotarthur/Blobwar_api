#pragma once
#include "Board.hpp"
#include "Rules.hpp"
#include "AIBase.hpp"
#include <vector>
#include <string>
#include <memory>
#include <ostream>

// ── Structures de résultat ────────────────────────────────────────────────────

struct MoveRecord {
    int    moveNum;   // numéro de coup global (0-based)
    Player player;
    double timeMs;    // temps de réflexion en ms
};

// Statistiques agrégées par tranche de 10 coups
struct TrancheStats {
    int    tranche;   // 0 = coups 0–9, 1 = coups 10–19, …
    int    countP1, countP2;
    double avgMsP1, avgMsP2;   // temps moyen de réflexion par joueur
    double avgMsAll;           // moyenne tous joueurs confondus
};

struct GameResult {
    GameStatus              status;
    std::string             nameP1, nameP2;
    int                     scoreP1 = 0, scoreP2 = 0;  // pièces finales
    std::vector<MoveRecord> moves;

    std::vector<TrancheStats> byTranche() const;
    void print(std::ostream& out) const;
};

// ── Configuration d'un joueur ─────────────────────────────────────────────────

struct PlayerConfig {
    std::string algo;   // "ab" | "negamax" | "negamax_par" | "negamax_par_dyn" | "negamax_par_inc"
    int         depth;
};

std::unique_ptr<AIBase> makeAI(const PlayerConfig& cfg);

// ── Arena : lance des parties et collecte les métriques ──────────────────────

class Arena {
public:
    // Lance une partie et retourne le résultat complet
    GameResult run(const Board& b,
                   const PlayerConfig& p1cfg,
                   const PlayerConfig& p2cfg);

    struct SeriesResult {
        int p1Wins = 0, p2Wins = 0, draws = 0;
        std::vector<GameResult> games;
        void print(std::ostream& out) const;
    };

    // Lance n parties (utile pour moyenner les résultats)
    SeriesResult runSeries(const Board& b,
                           const PlayerConfig& p1cfg,
                           const PlayerConfig& p2cfg,
                           int numGames = 1);
};
