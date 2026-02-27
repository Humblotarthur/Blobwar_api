#include "NegamaxParallelAI.hpp"
#include "Eval.hpp"
#include "Rules.hpp"
#include "MoveGen.hpp"
#include <tbb/parallel_reduce.h>
#include <tbb/blocked_range.h>
#include <algorithm>
#include <string>

NegamaxParallelAI::NegamaxParallelAI(int depth) : depth_(depth) {}

std::string NegamaxParallelAI::name() const {
    return "NegamaxPar(d=" + std::to_string(depth_) + ")";
}

int16_t NegamaxParallelAI::search(Board b, int depth, Player current,
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

Move NegamaxParallelAI::chooseMove(const Board& b, Player p) {
    auto moves = MoveGen::generateMoves(b, p);
    if (moves.empty()) return {-1, -1, -1, -1};

    using Range = tbb::blocked_range<size_t>;

    struct Best { Move move; int16_t score; };

    Best result = tbb::parallel_reduce(
        Range(0, moves.size()),
        Best{moves[0], -10000},
        [&](const Range& r, Best cur) -> Best {
            for (size_t i = r.begin(); i != r.end(); ++i) {
                Board nb = b;
                Rules::applyMove(nb, moves[i], p);
                int16_t score = -search(nb, depth_ - 1, opponent(p), -10000, 10000);
                if (score > cur.score) { cur.score = score; cur.move = moves[i]; }
            }
            return cur;
        },
        [](const Best& a, const Best& b) -> Best {
            return a.score >= b.score ? a : b;
        }
    );

    return result.move;
}
