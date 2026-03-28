#pragma once
#include <SFML/Graphics.hpp>
#include "Board.hpp"
#include "Rules.hpp"
#include "MoveGen.hpp"
#include "AIBase.hpp"
#include <vector>
#include <memory>

enum class GameMode  { OneVsOne, Server, VsAI, Tournament };
enum class AppScreen { Menu, BoardSelect, PlayerSelect, AISelect, Game, End };
enum class BoardType { Classic8x8, Cross8x8, Standard10x10, Cross9x9 };

class App {
public:
    App();
    void run();

private:
    // Dispatch événements
    void onEvent(const sf::Event& e);
    void onMenuClick        (sf::Vector2i pos);
    void onBoardSelectClick (sf::Vector2i pos);
    void onPlayerSelectClick(sf::Vector2i pos);
    void onAISelectClick    (sf::Vector2i pos);
    void onGameClick        (sf::Vector2i pos);
    void onEndClick         (sf::Vector2i pos);

    // Rendu par écran
    void renderMenu();
    void renderBoardSelect();
    void renderPlayerSelect();
    void renderAISelect();
    void renderGame();
    void renderEnd();

    // Helpers géométrie
    float        cellSize()   const;
    sf::Vector2f boardOffset() const;

    // Helpers UI
    void drawButton(sf::Vector2f pos, sf::Vector2f size,
                    const std::string& label,
                    sf::Color bg = {80, 80, 80});
    bool hit(sf::Vector2f pos, sf::Vector2f size, sf::Vector2i mouse) const;
    void drawText(const std::string& s, unsigned sz, sf::Color col,
                  sf::Vector2f pos, bool centered = false);

    // Logique jeu
    void startGame();
    void updateGame();   // déclenche le coup IA si c'est son tour
    void deselect();

    // Fenêtre
    sf::RenderWindow window_;
    sf::Font         font_;
    bool             fontOk_ = false;

    AppScreen screen_    = AppScreen::Menu;
    GameMode  mode_      = GameMode::OneVsOne;
    BoardType boardType_ = BoardType::Classic8x8;

    // État de jeu
    Board              board_{8, 8};
    Player             current_   = Player::P1;
    Player             humanP_    = Player::P1;
    int                selX_ = -1, selY_ = -1;
    std::vector<Move>  validMoves_;
    GameStatus         endStatus_ = GameStatus::Draw;

    // IA : nullptr = joueur humain
    std::unique_ptr<AIBase> ai_[2];              // ai_[0]=P1, ai_[1]=P2
    std::string aiAlgo_[2]  = {"negamax_par_dyn", "negamax_par_dyn"};
    int         aiDepth_[2] = {4, 4};
};
