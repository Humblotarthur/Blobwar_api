#include "BenchAPI.hpp"
#include "Board.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <set>
#include <stdexcept>

// ── Aide ──────────────────────────────────────────────────────────────────────

static void printHelp(const char* prog) {
    std::cerr <<
        "Usage: " << prog << " <algo1> [-d] <val1> <algo2> [-d] <val2> [board] [num_games]\n"
        "\n"
        "  -d       : le paramètre suivant est un temps limite en ms (modèles dynamiques)\n"
        "             sans -d : le paramètre est une profondeur fixe\n"
        "\n"
        "Modèles à profondeur fixe (val = profondeur) :\n"
        "  ab              AlphaBeta classique séquentiel\n"
        "  negamax         Negamax alpha-beta séquentiel\n"
        "  negamax_par     Negamax parallèle YBW (root-parallel TBB)\n"
        "  negamax_par_dyn YBW + élagage dynamique (globalAlpha atomique)\n"
        "  negamax_par_inc YBW + état incrémental (moves+score incrémentaux)\n"
        "  ybwz            YBW + Zobrist hashing (profondeur fixe)\n"
        "  stud            Parametrique, charge negamax_config.ini\n"
        "\n"
        "Modèles temporels — utiliser avec -d <ms> :\n"
        "  ybw_dyn         YBW + iterative deepening\n"
        "  ybwz_dyn        YBW + Zobrist + iterative deepening  (référence)\n"
        "  stud_dyn        Parametrique + iterative deepening\n"
        "  stud_zero       stud_dyn sans aucun élagage (baseline pure)\n"
        "  pmr_dyn         PMR activé (pmrRatio=0.2, pmrMinDepth=3)\n"
        "  pmr_zero        PMR désactivé — base PMR (baseline)\n"
        "  lmr2_zero       LMR v2 désactivé — base LMR2 (baseline)\n"
        "  dyn_zero        Dynamique sans PMR/LMR (baseline)\n"
        "  stud_random     stud_dyn epsilon-greedy (random_epsilon dans negamax_config.ini)\n"
        "\n"
        "Plateaux : classic8 (défaut) | cross8 | standard10 | cross9\n"
        "\n"
        "Exemples :\n"
        "  " << prog << " ybwz_dyn -d 250 ybwz_dyn -d 250\n"
        "  " << prog << " ybwz_dyn -d 250 ab 5\n"
        "  " << prog << " pmr_dyn -d 250 ybwz_dyn -d 250 classic8 5\n"
        "  " << prog << " ab 6 negamax_par 6 cross8 3\n"
        "  " << prog << " ybwz_dyn -d 250 negamax_par_inc 6 classic8 3\n";
}

// ── Plateau ───────────────────────────────────────────────────────────────────

static const std::set<std::string> kBoards = {
    "classic8", "cross8", "standard10", "cross9"
};

static Board makeBoard(const std::string& type) {
    if (type == "cross8")    { Board b(8,  8);  b.setupCross8x8(); return b; }
    if (type == "standard10"){ Board b(10, 10); b.setupDefault();  return b; }
    if (type == "cross9")    { Board b(9,  9);  b.setupCross();    return b; }
    Board b(8, 8); b.setupDefault(); return b;  // classic8 par défaut
}

// ── Formatage pour le log ─────────────────────────────────────────────────────

static std::string fmtPlayer(const PlayerConfig& p) {
    if (p.isDynamic)
        return p.algo + " (t=" + std::to_string(p.depth) + "ms)";
    return p.algo + " (d=" + std::to_string(p.depth) + ")";
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2 ||
        std::string(argv[1]) == "-h" ||
        std::string(argv[1]) == "--help") {
        printHelp(argv[0]);
        return (argc < 2) ? 1 : 0;
    }

    try {
        int pos = 1;

        // Lecture d'un joueur : <algo> [-d] <val>
        auto parsePlayer = [&](PlayerConfig& cfg) {
            if (pos >= argc) throw std::runtime_error("algo manquant");
            cfg.algo = argv[pos++];
            if (pos < argc && std::string(argv[pos]) == "-d") {
                cfg.isDynamic = true;
                ++pos;
            }
            if (pos >= argc) throw std::runtime_error("valeur manquante pour " + cfg.algo);
            cfg.depth = std::stoi(argv[pos++]);
        };

        PlayerConfig p1, p2;
        parsePlayer(p1);
        parsePlayer(p2);

        // Board (optionnel — reconnu par nom)
        std::string boardType = "classic8";
        if (pos < argc && kBoards.count(argv[pos]))
            boardType = argv[pos++];

        // num_games (optionnel)
        int numGames = 1;
        if (pos < argc)
            numGames = std::stoi(argv[pos++]);

        Board board = makeBoard(boardType);

        // Création du dossier log/ et ouverture du fichier de sortie
        std::filesystem::create_directories("log");
        std::ofstream out("log/output.txt", std::ios::app);
        if (!out) {
            std::cerr << "Erreur : impossible d'ouvrir log/output.txt\n";
            return 1;
        }

        out << "Plateau    : " << boardType << "\n";
        out << "P1         : " << fmtPlayer(p1) << "\n";
        out << "P2         : " << fmtPlayer(p2) << "\n";
        out << "Parties    : " << numGames << "\n";

        std::cerr << "Benchmark en cours... résultats dans log/output.txt\n";

        Arena arena;

        if (numGames == 1) {
            auto result = arena.run(board, p1, p2);
            result.print(out);
        } else {
            auto series = arena.runSeries(board, p1, p2, numGames);
            series.print(out);
            for (size_t i = 0; i < series.games.size(); ++i) {
                out << "\n--- Partie " << (i + 1) << " ---";
                series.games[i].print(out);
            }
        }

        out << "\n" << std::string(70, '=') << "\n";
        std::cerr << "Terminé.\n";

    } catch (const std::exception& e) {
        std::cerr << "Erreur : " << e.what() << "\n\n";
        printHelp(argv[0]);
        return 1;
    }

    return 0;
}
