#pragma once
#include "Board.hpp"
#include <cstdint>

// Fonction d'évaluation partagée par tous les algorithmes.
// Retourne un score du point de vue du joueur `us` (positif = avantage).
namespace Eval {
    int16_t evaluate(const Board& b, Player us);
}
