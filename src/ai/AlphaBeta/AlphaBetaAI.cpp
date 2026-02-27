#include "AlphaBetaAI.hpp"
#include "Eval.hpp"
#include "Rules.hpp"
#include "MoveGen.hpp"
#include <algorithm>
#include <string>

AlphaBetaAI::AlphaBetaAI(int depth) : depth_(depth) {}

std::string AlphaBetaAI::name() const {
    return "AlphaBeta(d=" + std::to_string(depth_) + ")";
}

int16_t AlphaBetaAI::search(Board b, int depth, bool isMax, Player us,
                              int16_t alpha, int16_t beta) const {
    if (Rules::getStatus(b) != GameStatus::Ongoing || depth == 0)
        return Eval::evaluate(b, us);

    Player current = isMax ? us : opponent(us);
    auto   moves   = MoveGen::generateMoves(b, current);
    if (moves.empty())
        return search(b, depth - 1, !isMax, us, alpha, beta);

    if (isMax) {
        int16_t best = -10000;
        for (const auto& m : moves) {
            Board nb = b;
            Rules::applyMove(nb, m, current);
            best  = std::max(best, search(nb, depth - 1, false, us, alpha, beta));
            alpha = std::max(alpha, best);
            if (beta <= alpha) break;
        }
        return best;
    } else {
        int16_t best = 10000;
        for (const auto& m : moves) {
            Board nb = b;
            Rules::applyMove(nb, m, current);
            best = std::min(best, search(nb, depth - 1, true, us, alpha, beta));
            beta = std::min(beta, best);
            if (beta <= alpha) break;
        }
        return best;
    }
}

Move AlphaBetaAI::chooseMove(const Board& b, Player p) {
    auto moves = MoveGen::generateMoves(b, p);
    if (moves.empty()) return {-1, -1, -1, -1};

    Move    best      = moves[0];
    int16_t bestScore = -10000;
    for (const auto& m : moves) {
        Board nb = b;
        Rules::applyMove(nb, m, p);
        int16_t score = search(nb, depth_ - 1, false, p, -10000, 10000);
        if (score > bestScore) { bestScore = score; best = m; }
    }
    return best;
}
