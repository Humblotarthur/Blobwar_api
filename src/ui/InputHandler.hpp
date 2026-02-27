#pragma once
#include <SFML/Graphics.hpp>

namespace InputHandler {
    inline sf::Vector2i pixelToCell(sf::Vector2i px, sf::Vector2f offset, float cellSz) {
        return {
            static_cast<int>((px.x - offset.x) / cellSz),
            static_cast<int>((px.y - offset.y) / cellSz)
        };
    }
}
