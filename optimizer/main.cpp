#include "Optimizer.hpp"
#include "NegamaxConfig.hpp"
#include <csignal>
#include <iostream>
#include <string>
#include <atomic>
#include <stdexcept>

std::atomic<bool> g_interrupted{false};

static void sigintHandler(int) { g_interrupted = true; }

static void usage(const char* prog) {
    std::cerr << "Usage: " << prog
              << " [--state FILE] [--init FILE] [--games N] [--depth D] [--time T] [--passes P]\n"
              << "  --state  FILE  fichier checkpoint (defaut: optimizer_state.ini)\n"
              << "  --init   FILE  config initiale .ini si aucun checkpoint (ex: negamax_config.ini)\n"
              << "  --games  N     parties par evaluation (defaut: 6)\n"
              << "  --depth  D     inutilise (compatibilite CLI)\n"
              << "  --time   T     budget temps par coup en ms (defaut: 250)\n"
              << "  --passes P     passes max Phase 2 (defaut: 5)\n";
}

int main(int argc, char* argv[]) {
    std::signal(SIGINT, sigintHandler);

    std::string stateFile    = "optimizer_state.ini";
    std::string initFile     = "";
    int         gamesPerEval = 6;
    int         oppDepth     = 6;
    int         timeLimitMs  = 250;
    int         maxPasses    = 5;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if      ((a == "--state"  || a == "-s") && i + 1 < argc) stateFile    = argv[++i];
        else if ((a == "--init"   || a == "-i") && i + 1 < argc) initFile     = argv[++i];
        else if ((a == "--games"  || a == "-g") && i + 1 < argc) gamesPerEval = std::stoi(argv[++i]);
        else if ((a == "--depth"  || a == "-d") && i + 1 < argc) oppDepth     = std::stoi(argv[++i]);
        else if ((a == "--time"   || a == "-t") && i + 1 < argc) timeLimitMs  = std::stoi(argv[++i]);
        else if ((a == "--passes" || a == "-p") && i + 1 < argc) maxPasses    = std::stoi(argv[++i]);
        else if (a == "--help" || a == "-h") { usage(argv[0]); return 0; }
        else { std::cerr << "Option inconnue : " << a << "\n"; usage(argv[0]); return 1; }
    }

    // Chargement config initiale si fournie
    NegamaxConfig initCfg;
    const NegamaxConfig* pInitCfg = nullptr;
    if (!initFile.empty()) {
        try {
            initCfg  = NegamaxConfig::loadFromFile(initFile);
            pInitCfg = &initCfg;
            std::cout << "Config initiale chargee depuis : " << initFile << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Erreur --init : " << e.what() << "\n";
            return 1;
        }
    }

    std::cout << "=== NegamaxParIncAIDynStud_PMR — Optimiseur Phase1+Phase2 (pmrRatio/pmrMinDepth) ===\n"
              << "  Strategie       : Phase1 baseline vs YBW, Phase2 exponential backoff\n"
              << "  Etat/checkpoint : " << stateFile    << "\n"
              << "  Init params     : " << (initFile.empty() ? "(defauts)" : initFile) << "\n"
              << "  Parties/eval    : " << gamesPerEval << "\n"
              << "  Adversaire      : NegamaxYBWDyn(" << timeLimitMs << "ms)\n"
              << "  Budget/coup     : " << timeLimitMs  << " ms (iterative deepening)\n"
              << "  Passes max P2   : " << maxPasses    << "\n\n"
              << "  Ctrl+C pour interrompre proprement (checkpoint auto)\n\n";

    Optimizer opt(stateFile, gamesPerEval, oppDepth, timeLimitMs, pInitCfg);
    opt.run(maxPasses);

    return 0;
}
