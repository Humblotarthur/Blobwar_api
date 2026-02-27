#include "NegamaxAI.hpp"
#include "Eval.hpp"
#include "Rules.hpp"
#include "MoveGen.hpp"
#include <algorithm>
#include <string>

NegamaxAI::NegamaxAI(int depth) : depth_(depth) {}

std::string NegamaxAI::name() const {
    return "Negamax(d=" + std::to_string(depth_) + ")";
}

int16_t NegamaxAI::search(Board b, int depth, Player current,
                           int16_t alpha, int16_t beta) const {
    if (Rules::getStatus(b) != GameStatus::Ongoing || depth == 0)
        return Eval::evaluate(b, current);

    auto moves = MoveGen::generateMoves(b, current);
    if (moves.empty())
        return -search(b, depth - 1, opponent(current), -beta, -alpha);

    int16_t best = -10000;
    for (const auto& m : moves) {
        Board nb = b;
        Rules::applyMove(nb, m, current);
        int16_t score = -search(nb, depth - 1, opponent(current), -beta, -alpha);
        best  = std::max(best, score);
        alpha = std::max(alpha, best);
        if (beta <= alpha) break;
    }
    return best;
}

Move NegamaxAI::chooseMove(const Board& b, Player p) {
    auto moves = MoveGen::generateMoves(b, p);
    if (moves.empty()) return {-1, -1, -1, -1};

    Move    best      = moves[0];
    int16_t bestScore = -10000;
    for (const auto& m : moves) {
        Board nb = b;
        Rules::applyMove(nb, m, p);
        int16_t score = -search(nb, depth_ - 1, opponent(p), -10000, 10000);
        if (score > bestScore) { bestScore = score; best = m; }
    }
    return best;
}
