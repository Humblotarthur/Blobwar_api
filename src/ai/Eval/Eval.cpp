#include "Eval.hpp"

// Poids positionnels pour plateau 8x8
static const int8_t WEIGHTS_8[8][8] = {
    {18, 14, 13, 12, 12, 13, 14, 18},
    {14, 16, 17, 15, 15, 17, 16, 14},
    {13, 17, 19, 18, 18, 19, 17, 13},
    {12, 15, 18, 10, 10, 18, 15, 12},
    {12, 15, 18, 10, 10, 18, 15, 12},
    {13, 17, 19, 18, 18, 19, 17, 13},
    {14, 16, 17, 15, 15, 17, 16, 14},
    {18, 14, 13, 12, 12, 13, 14, 18}
};

int16_t Eval::evaluate(const Board& b, Player us) {
    int mine   = b.countPieces(us);
    int theirs = b.countPieces(opponent(us));
    if (mine   == 0) return -10000;
    if (theirs == 0) return  10000;

    int16_t score = (int16_t)(mine - theirs);

    if (b.width() == 8 && b.height() == 8) {
        Cell mc = playerCell(us), tc = playerCell(opponent(us));
        for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            Cell c = b.get(x, y);
            if      (c == mc) score += 1;
            else if (c == tc) score -= 1;
        }
    }
    return score;
}
