#pragma once
#include <SFML/Graphics.hpp>
#include "Board.hpp"
#include "MoveGen.hpp"
#include <vector>

namespace Renderer {
    // Dessine les cases du plateau (VertexArray)
    void drawBoard(sf::RenderWindow& w, const Board& b,
                   int selX, int selY,
                   const std::vector<Move>& validMoves,
                   float cellSz, sf::Vector2f offset);

    // Dessine les pièces style Go (cercles)
    void drawPieces(sf::RenderWindow& w, const Board& b,
                    float cellSz, sf::Vector2f offset);
}
