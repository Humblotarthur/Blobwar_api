#include "Renderer.hpp"
#include <set>
#include <utility>

static sf::Color cellColor(const Board& b, int x, int y,
                            int selX, int selY,
                            const std::set<std::pair<int,int>>& dests) {
    Cell c = b.get(x, y);
    if (c == Cell::Hole)        return {30,  30,  30};
    if (x == selX && y == selY) return {220, 180, 50};   // sélectionné : or
    if (dests.count({x, y}))    return {140, 50,  200};  // coup valide : violet
    return ((x + y) % 2 == 0)  ? sf::Color{240, 217, 181}
                                : sf::Color{181, 136, 99};
}

void Renderer::drawBoard(sf::RenderWindow& w, const Board& b,
                          int selX, int selY,
                          const std::vector<Move>& validMoves,
                          float cs, sf::Vector2f off) {
    std::set<std::pair<int,int>> dests;
    for (auto& m : validMoves) dests.insert({m.x2, m.y2});

    int bw = b.width(), bh = b.height();
    sf::VertexArray va(sf::PrimitiveType::Triangles, bw * bh * 6);

    for (int y = 0; y < bh; ++y)
    for (int x = 0; x < bw; ++x) {
        int idx = (y * bw + x) * 6;
        sf::Color col = cellColor(b, x, y, selX, selY, dests);
        float px = off.x + x * cs, py = off.y + y * cs;

        auto setV = [&](int i, float vx, float vy) {
            va[idx + i].position = {vx, vy};
            va[idx + i].color    = col;
        };
        setV(0, px,    py);    setV(1, px+cs, py);    setV(2, px,    py+cs);
        setV(3, px+cs, py);    setV(4, px+cs, py+cs); setV(5, px,    py+cs);
    }
    w.draw(va);
}

void Renderer::drawPieces(sf::RenderWindow& w, const Board& b,
                           float cs, sf::Vector2f off) {
    float r = cs * 0.4f;
    for (int y = 0; y < b.height(); ++y)
    for (int x = 0; x < b.width();  ++x) {
        Cell c = b.get(x, y);
        if (c != Cell::P1 && c != Cell::P2) continue;

        float cx = off.x + x * cs + cs * 0.5f;
        float cy = off.y + y * cs + cs * 0.5f;

        sf::CircleShape piece(r);
        piece.setOrigin({r, r});
        piece.setPosition({cx, cy});
        if (c == Cell::P1) {
            piece.setFillColor({30,  30,  30});
            piece.setOutlineColor({90, 90, 90});
        } else {
            piece.setFillColor({230, 230, 230});
            piece.setOutlineColor({50,  50,  50});
        }
        piece.setOutlineThickness(cs * 0.04f);
        w.draw(piece);

        // Reflet style Go
        float hr = r * 0.28f;
        sf::CircleShape hi(hr);
        hi.setOrigin({hr, hr});
        hi.setPosition({cx - r * 0.3f, cy - r * 0.3f});
        hi.setFillColor(c == Cell::P1 ? sf::Color{90, 90, 90, 160}
                                      : sf::Color{255,255,255,200});
        w.draw(hi);
    }
}
