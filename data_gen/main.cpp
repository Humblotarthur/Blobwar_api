/*
 * data_gen — générateur de datasets d'entraînement pour BlobWarCNN.
 *
 * Modes :
 *   bootstrap_shallow  N parties depth fixe, 10% coup random, 2 premiers coups globaux random
 *   bootstrap_deep     N parties depth fixe, 20% top-K (K∈[2,15]), 2 premiers coups globaux random
 *   selfplay           N parties 200ms/coup, 15% top-K (K∈[2,10]), target = résultat final
 *
 * Format binaire : N × { bb0:u64, bb1:u64, holes:u64, target:f32 } = 28 octets/position
 * (compatible TrainSample / BlobWarDataset Python)
 *
 * Usage :
 *   data_gen bootstrap_shallow --games 10000 --depth 4 --out dataset_bootstrap_d4.bin
 *   data_gen bootstrap_deep    --games 5000  --depth 6 --out dataset_bootstrap_d6.bin
 *   data_gen selfplay          --games 2000  --time-ms 200 --out dataset_selfplay.bin
 *   data_gen --all             [génère les 3 datasets par défaut]
 */

#include "Board.hpp"
#include "Rules.hpp"
#include "BBState.hpp"
#include "YBWsearchZobrist.hpp"
#include "YBWsearchZobristDYN.hpp"
#include "GameRecord.hpp"

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <vector>
#include <string>
#include <random>
#include <algorithm>
#include <utility>

// ── Plateau ───────────────────────────────────────────────────────────────────

static Board makeBoard(const std::string& type) {
    if (type == "cross8")    { Board b(8,  8); b.setupCross8x8(); return b; }
    if (type == "standard10"){ Board b(10,10); b.setupDefault();  return b; }
    if (type == "cross9")    { Board b(9,  9); b.setupCross();    return b; }
    Board b(8, 8); b.setupDefault(); return b;
}

// ── Ranking de coups ─────────────────────────────────────────────────────────
//
// Applique chaque coup sur une copie du BBState et trie par evalFor(P1).
// Sens du tri : meilleur pour le joueur courant en premier.

static std::vector<Move> rankMoves(
    const BBState& base, Player p, const Move* buf, int n)
{
    std::vector<std::pair<int16_t, Move>> scored;
    scored.reserve(n);
    for (int i = 0; i < n; ++i) {
        BBState s = base;
        s.applyMove(buf[i], p);
        // evalFor(P1) : perspective constante pour le tri (score élevé = bon pour P1)
        scored.push_back({s.evalFor(Player::P1), buf[i]});
    }
    // Tri : si P1, on veut score élevé ; si P2, on veut score bas
    if (p == Player::P1)
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){ return a.first > b.first; });
    else
        std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b){ return a.first < b.first; });

    std::vector<Move> result;
    result.reserve(n);
    for (auto& [sc, m] : scored) result.push_back(m);
    return result;
}

// ── Sélection de coup avec exploration ───────────────────────────────────────
//
// fullRandom=true  : avec prob exploreProb → coup aléatoire parmi tous les légaux
// fullRandom=false : avec prob exploreProb → coup dans top-K (K aléatoire ∈ [minK, maxK])

static Move pickMoveWithExploration(
    const Move& bestMove,
    const BBState& state, Player p,
    std::mt19937& rng,
    float exploreProb, int minK, int maxK, bool fullRandom)
{
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    if (prob(rng) >= exploreProb) return bestMove;

    Move moveBuf[BBState::MAX_MOVES];
    int n = state.genMoves(p, moveBuf);
    if (n <= 1) return (n == 1) ? moveBuf[0] : bestMove;

    if (fullRandom) {
        std::uniform_int_distribution<int> idx(0, n - 1);
        return moveBuf[idx(rng)];
    }

    // Top-K : rank puis tirage dans les K premiers
    auto ranked = rankMoves(state, p, moveBuf, n);
    std::uniform_int_distribution<int> kDist(minK, maxK);
    int K = std::min(kDist(rng), n);
    std::uniform_int_distribution<int> idx(0, K - 1);
    return ranked[idx(rng)];
}

// ── Écriture du fichier binaire ───────────────────────────────────────────────

static bool writeDataset(const std::vector<TrainSample>& samples, const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "Erreur : impossible d'ouvrir %s\n", path.c_str()); return false; }
    size_t written = std::fwrite(samples.data(), sizeof(TrainSample), samples.size(), f);
    std::fclose(f);
    if (written != samples.size()) {
        std::fprintf(stderr, "Erreur d'écriture (%zu/%zu)\n", written, samples.size());
        return false;
    }
    std::fprintf(stderr,
        "Terminé : %zu positions → %s  (%.2f MB)\n",
        samples.size(), path.c_str(),
        static_cast<double>(samples.size() * sizeof(TrainSample)) / (1024.0 * 1024.0));
    return true;
}

// ── Mode bootstrap ────────────────────────────────────────────────────────────
//
// Target = tanh(evalFor(P1) / 100.0)  enregistrée AVANT le coup.
// fullRandom=true  → mode shallow (10% random)
// fullRandom=false → mode deep    (20% top-K [2,15])

// filterEndgame=true : exclut les 30 derniers coups globaux (15 par joueur)
static void generateBootstrap(
    int numGames, int depth,
    float exploreProb, int minK, int maxK, bool fullRandom,
    bool filterEndgame,
    const std::string& outPath, const std::string& boardType)
{
    std::fprintf(stderr, "[bootstrap] %d parties depth=%d explore=%.0f%%  → %s\n",
                 numGames, depth, exploreProb * 100.0f, outPath.c_str());

    std::mt19937 rng(std::random_device{}());
    std::vector<TrainSample> samples;
    samples.reserve(static_cast<size_t>(numGames) * 25);  // ~70 positions/partie ÷ 3

    const Board boardTemplate = makeBoard(boardType);

    for (int g = 0; g < numGames; ++g) {
        NegamaxParIncAI_YBWsearchZobrist ai1(depth);
        NegamaxParIncAI_YBWsearchZobrist ai2(depth);

        Board board = boardTemplate;
        Player current = Player::P1;
        int globalMove = 0;

        std::vector<TrainSample> gameSamples;
        gameSamples.reserve(80);

        while (true) {
            GameStatus st = Rules::getStatus(board);
            if (st != GameStatus::Ongoing) break;

            if (!Rules::canMove(board, current)) {
                current = opponent(current);
                if (!Rules::canMove(board, current)) break;
                continue;
            }

            BBState bs = BBState::fromBoard(board);

            TrainSample s;
            s.bb0    = bs.bb[0];
            s.bb1    = bs.bb[1];
            s.holes  = bs.holes;
            s.target = std::tanh(static_cast<float>(bs.evalFor(Player::P1)) / 100.0f);
            gameSamples.push_back(s);

            Move m;
            if (globalMove < 2) {
                Move moveBuf[BBState::MAX_MOVES];
                int n = bs.genMoves(current, moveBuf);
                if (n == 0) break;
                std::uniform_int_distribution<int> idx(0, n - 1);
                m = moveBuf[idx(rng)];
            } else {
                AIBase* ai = (current == Player::P1) ? &ai1 : &ai2;
                Move best = ai->chooseMove(board, current);
                m = pickMoveWithExploration(best, bs, current, rng,
                                            exploreProb, minK, maxK, fullRandom);
            }

            if (!board.inBounds(m.x1, m.y1) || !board.inBounds(m.x2, m.y2)) break;
            Rules::applyMove(board, m, current);
            current = opponent(current);
            ++globalMove;
        }

        // Supprimer les 30 derniers coups (15 par joueur) si demandé
        if (filterEndgame && gameSamples.size() > 30)
            gameSamples.resize(gameSamples.size() - 30);

        // 1-in-3 : réduire la corrélation entre positions consécutives
        for (size_t i = 0; i < gameSamples.size(); i += 3)
            samples.push_back(gameSamples[i]);

        if ((g + 1) % 200 == 0 || g + 1 == numGames)
            std::fprintf(stderr, "\rPartie %d/%d  —  %zu positions",
                         g + 1, numGames, samples.size());
    }
    std::fprintf(stderr, "\n");
    writeDataset(samples, outPath);
}

// ── Mode self-play ────────────────────────────────────────────────────────────
//
// Les deux IA jouent avec un budget temps de timeLimitMs ms.
// Target = résultat final de la partie (depuis P1) propagé à toutes les positions.
// Enregistrement : position AVANT chaque coup.

static void generateSelfPlay(
    int numGames, int timeLimitMs,
    float exploreProb, int minK, int maxK,
    const std::string& outPath, const std::string& boardType)
{
    std::fprintf(stderr, "[selfplay] %d parties t=%dms explore=%.0f%%  → %s\n",
                 numGames, timeLimitMs, exploreProb * 100.0f, outPath.c_str());

    std::mt19937 rng(std::random_device{}());
    std::vector<TrainSample> samples;
    samples.reserve(static_cast<size_t>(numGames) * 25);  // ~70 positions/partie ÷ 3

    const Board boardTemplate = makeBoard(boardType);

    struct PosRecord { uint64_t bb0, bb1, holes; };

    for (int g = 0; g < numGames; ++g) {
        NegamaxParIncAI_YBWsearchZobristDYN ai1(timeLimitMs);
        NegamaxParIncAI_YBWsearchZobristDYN ai2(timeLimitMs);

        Board board = boardTemplate;
        Player current = Player::P1;

        std::vector<PosRecord> gamePositions;
        gamePositions.reserve(80);

        while (true) {
            GameStatus st = Rules::getStatus(board);
            if (st != GameStatus::Ongoing) break;

            if (!Rules::canMove(board, current)) {
                current = opponent(current);
                if (!Rules::canMove(board, current)) break;
                continue;
            }

            BBState bs = BBState::fromBoard(board);
            gamePositions.push_back({bs.bb[0], bs.bb[1], bs.holes});

            AIBase* ai = (current == Player::P1) ? &ai1 : &ai2;
            Move best = ai->chooseMove(board, current);
            Move m = pickMoveWithExploration(best, bs, current, rng,
                                             exploreProb, minK, maxK, false);

            if (!board.inBounds(m.x1, m.y1) || !board.inBounds(m.x2, m.y2)) break;
            Rules::applyMove(board, m, current);
            current = opponent(current);
        }

        // Résultat final depuis P1
        GameStatus final = Rules::getStatus(board);
        float resultP1;
        if      (final == GameStatus::P1Wins) resultP1 =  1.0f;
        else if (final == GameStatus::P2Wins) resultP1 = -1.0f;
        else                                  resultP1 =  0.0f;

        // Exclure les 30 derniers coups (15 par joueur) — finales triviales
        if (gamePositions.size() > 30)
            gamePositions.resize(gamePositions.size() - 30);

        // 1-in-3 + propagation du résultat
        for (size_t i = 0; i < gamePositions.size(); i += 3) {
            const auto& pos = gamePositions[i];
            TrainSample s;
            s.bb0    = pos.bb0;
            s.bb1    = pos.bb1;
            s.holes  = pos.holes;
            s.target = resultP1;
            samples.push_back(s);
        }

        if ((g + 1) % 50 == 0 || g + 1 == numGames)
            std::fprintf(stderr, "\rPartie %d/%d  —  %zu positions",
                         g + 1, numGames, samples.size());
    }
    std::fprintf(stderr, "\n");
    writeDataset(samples, outPath);
}

// ── Parsing des arguments ─────────────────────────────────────────────────────

static const char* getArg(int argc, char* argv[], const char* flag, const char* def = nullptr) {
    for (int i = 2; i < argc - 1; ++i)
        if (std::strcmp(argv[i], flag) == 0) return argv[i + 1];
    return def;
}

static void printHelp(const char* prog) {
    std::fprintf(stderr,
        "Usage: %s <mode> [options]\n\n"
        "Modes:\n"
        "  bootstrap_shallow  Depth fixe, 10%%  coup aléatoire\n"
        "  bootstrap_deep     Depth fixe, 20%%  top-K (K∈[2,15])\n"
        "  selfplay           Budget temps, 15%% top-K (K∈[2,10]), target=résultat\n"
        "  --all              Génère les 3 datasets avec paramètres par défaut\n\n"
        "Options:\n"
        "  --games   N        Nombre de parties (défaut: mode-dépendant)\n"
        "  --depth   D        Profondeur fixe (bootstrap uniquement)\n"
        "  --time-ms T        Budget temps par coup en ms (selfplay, défaut: 200)\n"
        "  --out     FILE     Fichier de sortie .bin\n"
        "  --board   TYPE     classic8 | cross8 | standard10 | cross9  (défaut: classic8)\n\n"
        "Exemples:\n"
        "  %s bootstrap_shallow --games 10000 --depth 4 --out dataset_bootstrap_d4.bin\n"
        "  %s bootstrap_deep    --games 5000  --depth 6 --out dataset_bootstrap_d6.bin\n"
        "  %s selfplay          --games 2000  --time-ms 200 --out dataset_selfplay.bin\n"
        "  %s --all\n",
        prog, prog, prog, prog, prog);
}

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    if (argc < 2) { printHelp(argv[0]); return 1; }

    const std::string mode = argv[1];

    // Mode --all : génère les 3 datasets par défaut
    if (mode == "--all") {
        generateBootstrap(10000, 4, 0.10f, 2, 15, /*fullRandom=*/true,  /*filterEndgame=*/false,
                          "dataset_bootstrap_d4.bin", "classic8");
        generateBootstrap(5000,  6, 0.20f, 2, 15, /*fullRandom=*/false, /*filterEndgame=*/true,
                          "dataset_bootstrap_d6.bin", "classic8");
        generateSelfPlay(2000, 200, 0.15f, 2, 10,
                          "dataset_selfplay.bin", "classic8");
        return 0;
    }

    if (mode == "-h" || mode == "--help") { printHelp(argv[0]); return 0; }

    const char* board   = getArg(argc, argv, "--board",   "classic8");
    const char* outFile = getArg(argc, argv, "--out",     nullptr);

    if (!outFile) {
        std::fprintf(stderr, "Erreur : --out requis\n");
        printHelp(argv[0]);
        return 1;
    }

    if (mode == "bootstrap_shallow") {
        const char* gs = getArg(argc, argv, "--games", "10000");
        const char* ds = getArg(argc, argv, "--depth", "4");
        int numGames = std::atoi(gs);
        int depth    = std::atoi(ds);
        generateBootstrap(numGames, depth, 0.10f, 2, 15, /*fullRandom=*/true, /*filterEndgame=*/false, outFile, board);

    } else if (mode == "bootstrap_deep") {
        const char* gs = getArg(argc, argv, "--games", "5000");
        const char* ds = getArg(argc, argv, "--depth", "6");
        int numGames = std::atoi(gs);
        int depth    = std::atoi(ds);
        generateBootstrap(numGames, depth, 0.20f, 2, 15, /*fullRandom=*/false, /*filterEndgame=*/true, outFile, board);

    } else if (mode == "selfplay") {
        const char* gs = getArg(argc, argv, "--games",   "2000");
        const char* ts = getArg(argc, argv, "--time-ms", "200");
        int numGames   = std::atoi(gs);
        int timeLimitMs = std::atoi(ts);
        generateSelfPlay(numGames, timeLimitMs, 0.15f, 2, 10, outFile, board);

    } else {
        std::fprintf(stderr, "Mode inconnu : %s\n", mode.c_str());
        printHelp(argv[0]);
        return 1;
    }

    return 0;
}
