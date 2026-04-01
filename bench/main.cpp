#include "BenchAPI.hpp"
#include "Board.hpp"
#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <filesystem>

// Usage :
//   bench <algo1> <depth1> <algo2> <depth2> [board] [num_games]
//
//   algo  : ab | negamax | negamax_par | negamax_par_dyn | negamax_par_inc | stud
//   board : classic8 (défaut) | cross8 | standard10 | cross9
//   num_games : nombre de parties (défaut : 1)

static Board makeBoard(const std::string& type) {
    if (type == "cross8") {
        Board b(8, 8); b.setupCross8x8(); return b;
    }
    if (type == "standard10") {
        Board b(10, 10); b.setupDefault(); return b;
    }
    if (type == "cross9") {
        Board b(9, 9); b.setupCross(); return b;
    }
    // défaut : classic 8x8
    Board b(8, 8); b.setupDefault(); return b;
}

int main(int argc, char* argv[]) {
    if (argc < 5) {
        std::cerr << "Usage: bench <algo1> <depth1> <algo2> <depth2> [board] [num_games]\n";
        std::cerr << "  algos : ab | negamax | negamax_par | negamax_par_dyn | negamax_par_inc | stud\n";
        std::cerr << "  board : classic8 | cross8 | standard10 | cross9\n";
        return 1;
    }

    PlayerConfig p1{ argv[1], std::stoi(argv[2]) };
    PlayerConfig p2{ argv[3], std::stoi(argv[4]) };
    std::string  boardType  = (argc >= 6) ? argv[5] : "classic8";
    int          numGames   = (argc >= 7) ? std::stoi(argv[6]) : 1;

    Board board = makeBoard(boardType);

    // Création du dossier log/ et ouverture du fichier de sortie
    std::filesystem::create_directories("log");
    std::ofstream out("log/output.txt", std::ios::app);
    if (!out) {
        std::cerr << "Erreur : impossible d'ouvrir log/output.txt\n";
        return 1;
    }

    // En-tête dans le fichier
    out << "Plateau    : " << boardType << "\n";
    out << "P1         : " << p1.algo << " (d=" << p1.depth << ")\n";
    out << "P2         : " << p2.algo << " (d=" << p2.depth << ")\n";
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
    return 0;
}
