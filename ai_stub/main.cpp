#include "Board.hpp"
#include "Rules.hpp"
#include "MoveGen.hpp"
#include "RandomAI.hpp"
#include "AlphaBetaAI.hpp"
#include "NegamaxAI.hpp"
#include "NegamaxParallelAI.hpp"
#include <iostream>
#include <memory>
#include <string>

// Usage : ai_stub <algo> <depth>
//   algo  : random | ab | ab_dyn | negamax   (défaut: negamax)
//   depth : entier                            (défaut: 4, ignoré pour random)
int main(int argc, char* argv[]) {
    std::string algo  = (argc >= 2) ? argv[1] : "negamax";
    int         depth = (argc >= 3) ? std::stoi(argv[2]) : 4;

    std::unique_ptr<AIBase> ai;
    if      (algo == "random")      ai = std::make_unique<RandomAI>();
    else if (algo == "ab")          ai = std::make_unique<AlphaBetaAI>(depth);
    else if (algo == "negamax_par") ai = std::make_unique<NegamaxParallelAI>(depth);
    else                            ai = std::make_unique<NegamaxAI>(depth);

    int W, H, playerInt;
    while (std::cin >> W >> H >> playerInt) {
        Board board(W, H);
        for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int c; std::cin >> c;
            board.set(x, y, static_cast<Cell>(c));
        }
        Player p = static_cast<Player>(playerInt);
        Move   m = ai->chooseMove(board, p);
        std::cout << "((" << (int)m.x1 << "," << (int)m.y1 << "),"
                  << "("  << (int)m.x2 << "," << (int)m.y2 << "))\n";
        std::cout.flush();
    }
    return 0;
}
