#include "App.hpp"
#include "Renderer.hpp"
#include "InputHandler.hpp"
#include "AIProcess.hpp"
#include <algorithm>
#include <string>
#include <filesystem>

// ── Géométrie ────────────────────────────────────────────────────────────────

static constexpr float TOP_BAR = 70.f;
static constexpr float MARGIN  = 10.f;

float App::cellSize() const {
    auto ws = window_.getSize();
    float maxW = (ws.x - 2 * MARGIN) / board_.width();
    float maxH = (ws.y - TOP_BAR - 2 * MARGIN) / board_.height();
    return std::min(maxW, maxH);
}

sf::Vector2f App::boardOffset() const {
    auto ws = window_.getSize();
    float cs = cellSize();
    float bw = cs * board_.width();
    float bh = cs * board_.height();
    return { (ws.x - bw) / 2.f,
             TOP_BAR + (ws.y - TOP_BAR - bh) / 2.f };
}

// ── Construction ─────────────────────────────────────────────────────────────

App::App() : window_(sf::VideoMode({900u, 700u}), "Blobwar") {
    window_.setFramerateLimit(60);

    for (const char* p : {
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf" }) {
        if (font_.openFromFile(p)) { fontOk_ = true; break; }
    }
}

// ── Boucle principale ─────────────────────────────────────────────────────────

void App::run() {
    while (window_.isOpen()) {
        while (auto ev = window_.pollEvent()) {
            if (ev->is<sf::Event::Closed>()) {
                window_.close();
            } else if (auto* r = ev->getIf<sf::Event::Resized>()) {
                window_.setView(sf::View(sf::FloatRect(
                    {0.f, 0.f}, {(float)r->size.x, (float)r->size.y})));
            } else {
                onEvent(*ev);
            }
        }
        if (screen_ == AppScreen::Game) updateGame();

        window_.clear({20, 20, 20});
        switch (screen_) {
            case AppScreen::Menu:         renderMenu();         break;
            case AppScreen::BoardSelect:  renderBoardSelect();  break;
            case AppScreen::PlayerSelect: renderPlayerSelect(); break;
            case AppScreen::AISelect:     renderAISelect();     break;
            case AppScreen::Game:         renderGame();         break;
            case AppScreen::End:          renderEnd();          break;
        }
        window_.display();
    }
}

// ── Dispatch événements ───────────────────────────────────────────────────────

void App::onEvent(const sf::Event& e) {
    auto* mp = e.getIf<sf::Event::MouseButtonPressed>();
    if (!mp || mp->button != sf::Mouse::Button::Left) return;
    switch (screen_) {
        case AppScreen::Menu:         onMenuClick(mp->position);         break;
        case AppScreen::BoardSelect:  onBoardSelectClick(mp->position);  break;
        case AppScreen::PlayerSelect: onPlayerSelectClick(mp->position); break;
        case AppScreen::AISelect:     onAISelectClick(mp->position);     break;
        case AppScreen::Game:         onGameClick(mp->position);         break;
        case AppScreen::End:          onEndClick(mp->position);          break;
    }
}

// ── Helpers UI ────────────────────────────────────────────────────────────────

bool App::hit(sf::Vector2f pos, sf::Vector2f size, sf::Vector2i mouse) const {
    return mouse.x >= pos.x && mouse.x < pos.x + size.x
        && mouse.y >= pos.y && mouse.y < pos.y + size.y;
}

void App::drawButton(sf::Vector2f pos, sf::Vector2f size,
                     const std::string& label, sf::Color bg) {
    sf::RectangleShape rect(size);
    rect.setPosition(pos);
    rect.setFillColor(bg);
    rect.setOutlineColor({200, 200, 200});
    rect.setOutlineThickness(2.f);
    window_.draw(rect);
    drawText(label, 22, sf::Color::White,
             {pos.x + size.x / 2.f, pos.y + size.y / 2.f}, true);
}

void App::drawText(const std::string& s, unsigned sz, sf::Color col,
                   sf::Vector2f pos, bool centered) {
    if (!fontOk_) return;
    sf::Text t(font_, s, sz);
    t.setFillColor(col);
    if (centered) {
        auto b = t.getLocalBounds();
        t.setPosition({ pos.x - b.size.x / 2.f - b.position.x,
                        pos.y - b.size.y / 2.f - b.position.y });
    } else {
        t.setPosition(pos);
    }
    window_.draw(t);
}

// ── Menu ──────────────────────────────────────────────────────────────────────

static const sf::Vector2f BTN_SZ{300.f, 55.f};
static const float        BTN_GAP = 15.f;

static sf::Vector2f menuBtnPos(sf::Vector2u ws, int i) {
    float startY = ws.y / 2.f - (4 * BTN_SZ.y + 3 * BTN_GAP) / 2.f;
    return { ws.x / 2.f - BTN_SZ.x / 2.f,
             startY + i * (BTN_SZ.y + BTN_GAP) };
}

void App::renderMenu() {
    auto ws = window_.getSize();
    drawText("BLOBWAR", 58, {220, 170, 40},
             {ws.x / 2.f, menuBtnPos(ws, 0).y - 80.f}, true);
    drawButton(menuBtnPos(ws, 0), BTN_SZ, "1v1");
    drawButton(menuBtnPos(ws, 1), BTN_SZ, "Serveur");
    drawButton(menuBtnPos(ws, 2), BTN_SZ, "Joueur vs IA");
    drawButton(menuBtnPos(ws, 3), BTN_SZ, "Tournoi");
}

void App::onMenuClick(sf::Vector2i pos) {
    auto ws = window_.getSize();
    if      (hit(menuBtnPos(ws, 0), BTN_SZ, pos)) { mode_ = GameMode::OneVsOne;  screen_ = AppScreen::BoardSelect; }
    else if (hit(menuBtnPos(ws, 1), BTN_SZ, pos)) { mode_ = GameMode::Server;    screen_ = AppScreen::BoardSelect; }
    else if (hit(menuBtnPos(ws, 2), BTN_SZ, pos)) { mode_ = GameMode::VsAI;      screen_ = AppScreen::PlayerSelect;}
    else if (hit(menuBtnPos(ws, 3), BTN_SZ, pos)) { mode_ = GameMode::Tournament;screen_ = AppScreen::AISelect; }
}

// ── Sélection plateau ─────────────────────────────────────────────────────────

void App::renderBoardSelect() {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;
    drawText("Choisir un plateau", 32, sf::Color::White, {cx, cy - 130.f}, true);
    drawButton({cx - 150.f, cy - 60.f},  {300.f, 55.f}, "8x8 - Standard");
    drawButton({cx - 150.f, cy + 20.f},  {300.f, 55.f}, "10x10 - Standard");
    drawButton({cx - 150.f, cy + 100.f}, {300.f, 55.f}, "9x9 - Croix");
    drawButton({cx - 100.f, cy + 185.f}, {200.f, 40.f}, "Retour", {80, 50, 50});
}

void App::onBoardSelectClick(sf::Vector2i pos) {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;
    if (hit({cx - 150.f, cy - 60.f}, {300.f, 55.f}, pos)) {
        boardType_ = BoardType::Standard8x8;
        board_ = Board(8, 8); board_.setupDefault(); startGame();
    } else if (hit({cx - 150.f, cy + 20.f}, {300.f, 55.f}, pos)) {
        boardType_ = BoardType::Standard10x10;
        board_ = Board(10, 10); board_.setupDefault(); startGame();
    } else if (hit({cx - 150.f, cy + 100.f}, {300.f, 55.f}, pos)) {
        boardType_ = BoardType::Cross9x9;
        board_ = Board(9, 9); board_.setupCross(); startGame();
    } else if (hit({cx - 100.f, cy + 185.f}, {200.f, 40.f}, pos)) {
        screen_ = (mode_ == GameMode::VsAI) ? AppScreen::PlayerSelect
                                             : AppScreen::Menu;
    }
}

// ── Sélection joueur (VsAI) ───────────────────────────────────────────────────

void App::renderPlayerSelect() {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;
    drawText("Choisir votre couleur", 32, sf::Color::White, {cx, cy - 110.f}, true);
    drawButton({cx - 140.f, cy - 40.f}, {280.f, 55.f}, "Jouer Noir (J1)", {40, 40, 40});
    drawButton({cx - 140.f, cy + 30.f}, {280.f, 55.f}, "Jouer Blanc (J2)", {180, 180, 180});
    drawButton({cx - 100.f, cy + 120.f}, {200.f, 40.f}, "Retour", {80, 50, 50});
}

void App::onPlayerSelectClick(sf::Vector2i pos) {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;
    if      (hit({cx - 140.f, cy - 40.f},  {280.f, 55.f}, pos)) { humanP_ = Player::P1; screen_ = AppScreen::AISelect; }
    else if (hit({cx - 140.f, cy + 30.f},  {280.f, 55.f}, pos)) { humanP_ = Player::P2; screen_ = AppScreen::AISelect; }
    else if (hit({cx - 100.f, cy + 120.f}, {200.f, 40.f}, pos)) { screen_ = AppScreen::Menu; }
}

// ── Jeu ───────────────────────────────────────────────────────────────────────

static std::string stubPath() {
    auto exe = std::filesystem::weakly_canonical("/proc/self/exe");
    return (exe.parent_path() / "ai_stub").string();
}

void App::startGame() {
    current_ = Player::P1;
    deselect();
    ai_[0] = nullptr;
    ai_[1] = nullptr;

    std::string stub = stubPath();
    if (mode_ == GameMode::VsAI) {
        int aiIdx = (humanP_ == Player::P1) ? 1 : 0;
        ai_[aiIdx] = std::make_unique<AIProcess>(stub, aiAlgo_[aiIdx], aiDepth_[aiIdx]);
    } else if (mode_ == GameMode::Tournament) {
        ai_[0] = std::make_unique<AIProcess>(stub, aiAlgo_[0], aiDepth_[0]);
        ai_[1] = std::make_unique<AIProcess>(stub, aiAlgo_[1], aiDepth_[1]);
    }
    screen_ = AppScreen::Game;
}

void App::updateGame() {
    int idx = (current_ == Player::P1) ? 0 : 1;
    if (!ai_[idx]) return;   // tour du joueur humain

    Move m = ai_[idx]->chooseMove(board_, current_);
    if (!board_.inBounds(m.x1, m.y1) || !board_.inBounds(m.x2, m.y2)) return;

    Rules::applyMove(board_, m, current_);
    auto status = Rules::getStatus(board_);
    if (status != GameStatus::Ongoing) { endStatus_ = status; screen_ = AppScreen::End; return; }
    current_ = opponent(current_);
    if (!Rules::canMove(board_, current_)) current_ = opponent(current_);
}

void App::deselect() {
    selX_ = selY_ = -1;
    validMoves_.clear();
}

void App::renderGame() {
    auto ws  = window_.getSize();
    float cs = cellSize();
    auto  off = boardOffset();

    Renderer::drawBoard(window_, board_, selX_, selY_, validMoves_, cs, off);
    Renderer::drawPieces(window_, board_, cs, off);

    // Indicateur du joueur courant
    int idx = (current_ == Player::P1) ? 0 : 1;
    std::string who  = ai_[idx] ? "IA" : "Humain";
    std::string info = (current_ == Player::P1)
        ? "Tour : J1 Noir  [" + who + "]"
        : "Tour : J2 Blanc [" + who + "]";
    drawText(info, 24, sf::Color::White, {ws.x / 2.f, TOP_BAR / 2.f}, true);
}

void App::onGameClick(sf::Vector2i px) {
    // Ignorer les clics si c'est le tour de l'IA
    int idx = (current_ == Player::P1) ? 0 : 1;
    if (ai_[idx]) return;

    auto off = boardOffset();
    float cs = cellSize();
    auto  cell = InputHandler::pixelToCell(px, off, cs);
    int cx = cell.x, cy = cell.y;

    if (!board_.inBounds(cx, cy)) { deselect(); return; }

    if (selX_ != -1) {
        // Coup valide ?
        for (auto& m : validMoves_) {
            if (m.x2 == cx && m.y2 == cy) {
                Rules::applyMove(board_, m, current_);
                deselect();
                auto status = Rules::getStatus(board_);
                if (status != GameStatus::Ongoing) { endStatus_ = status; screen_ = AppScreen::End; return; }
                current_ = opponent(current_);
                if (!Rules::canMove(board_, current_)) current_ = opponent(current_);
                return;
            }
        }
        // Clic sur une bille → déselection
        deselect();
        return;
    }

    // Sélection d'une pièce du joueur courant
    if (board_.get(cx, cy) == playerCell(current_)) {
        selX_ = cx; selY_ = cy;
        for (auto& m : MoveGen::generateMoves(board_, current_))
            if (m.x1 == cx && m.y1 == cy)
                validMoves_.push_back(m);
    }
}

// ── Fin de partie ─────────────────────────────────────────────────────────────

void App::renderEnd() {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;

    std::string msg;
    sf::Color   col;
    if      (endStatus_ == GameStatus::P1Wins) { msg = "J1 gagne !"; col = {100,100,100}; }
    else if (endStatus_ == GameStatus::P2Wins) { msg = "J2 gagne !"; col = {220,220,220}; }
    else                                        { msg = "Match nul";  col = {200,180, 50}; }

    drawText(msg, 54, col, {cx, cy - 110.f}, true);

    std::string scores = "J1: " + std::to_string(board_.countPieces(Player::P1))
                       + "   J2: " + std::to_string(board_.countPieces(Player::P2));
    drawText(scores, 28, sf::Color::White, {cx, cy - 45.f}, true);

    drawButton({cx - 125.f, cy + 10.f}, {250.f, 55.f}, "Rejouer");
    drawButton({cx - 125.f, cy + 80.f}, {250.f, 55.f}, "Menu principal", {60, 40, 90});
}

// ── Sélection IA ─────────────────────────────────────────────────────────────

struct AlgoOption  { const char* algo; const char* label; };
struct DepthOption { int depth; const char* label; };

static const AlgoOption ALGO_OPTIONS[] = {
    {"random",      "Aleatoire"},
    {"ab",          "AlphaBeta"},
    {"negamax",     "Negamax"},
    {"negamax_par", "Negamax //"},
};
static const DepthOption DEPTH_OPTIONS[] = {
    {3, "d=3 (Facile)"},
    {4, "d=4 (Normal)"},
    {6, "d=6 (Difficile)"},
};
static constexpr int NB_ALGOS  = 4;
static constexpr int NB_DEPTHS = 3;

static sf::Vector2f algoPos(sf::Vector2u ws, int col, int row, int nbCols) {
    float colW = ws.x / (float)nbCols;
    return { col * colW + (colW - 240.f) / 2.f, ws.y * 0.25f + row * 58.f };
}
static sf::Vector2f depthPos(sf::Vector2u ws, int col, int row, int nbCols) {
    float colW = ws.x / (float)nbCols;
    return { col * colW + (colW - 240.f) / 2.f, ws.y * 0.58f + row * 52.f };
}

void App::renderAISelect() {
    auto ws = window_.getSize();
    bool isTournament = (mode_ == GameMode::Tournament);
    int  nbCols = isTournament ? 2 : 1;

    drawText(isTournament ? "Configuration du Tournoi"
                          : "Choisir l'IA adversaire",
             28, sf::Color::White, {ws.x / 2.f, ws.y * 0.09f}, true);

    for (int col = 0; col < nbCols; ++col) {
        int   pidx   = isTournament ? col : (humanP_ == Player::P1 ? 1 : 0);
        float colW   = ws.x / (float)nbCols;
        float cx     = col * colW + colW / 2.f;
        std::string hdr = isTournament ? (col == 0 ? "J1 (Noir)" : "J2 (Blanc)")
                                       : (humanP_ == Player::P1 ? "J2 (Blanc)" : "J1 (Noir)");
        drawText(hdr, 20, {180,180,180}, {cx, ws.y * 0.17f}, true);
        drawText("Algorithme", 17, {140,140,140}, {cx, ws.y * 0.21f}, true);

        for (int i = 0; i < NB_ALGOS; ++i) {
            bool sel = (aiAlgo_[pidx] == ALGO_OPTIONS[i].algo);
            drawButton(algoPos(ws, col, i, nbCols), {240.f, 46.f},
                       ALGO_OPTIONS[i].label,
                       sel ? sf::Color{50,110,50} : sf::Color{80,80,80});
        }

        // Profondeur (désactivée pour Random)
        bool isRandom = (aiAlgo_[pidx] == "random");
        drawText("Profondeur", 17, isRandom ? sf::Color{80,80,80} : sf::Color{140,140,140},
                 {cx, ws.y * 0.54f}, true);
        for (int i = 0; i < NB_DEPTHS; ++i) {
            bool sel = (!isRandom && aiDepth_[pidx] == DEPTH_OPTIONS[i].depth);
            sf::Color bg = isRandom ? sf::Color{50,50,50}
                         : (sel    ? sf::Color{50,110,50} : sf::Color{80,80,80});
            drawButton(depthPos(ws, col, i, nbCols), {240.f, 42.f},
                       DEPTH_OPTIONS[i].label, bg);
        }
    }

    drawButton({ws.x/2.f-120.f, ws.y*0.87f}, {240.f, 48.f}, "Continuer");
    drawButton({ws.x/2.f-120.f, ws.y*0.94f}, {240.f, 38.f}, "Retour", {80,50,50});
}

void App::onAISelectClick(sf::Vector2i pos) {
    auto ws = window_.getSize();
    bool isTournament = (mode_ == GameMode::Tournament);
    int  nbCols = isTournament ? 2 : 1;

    for (int col = 0; col < nbCols; ++col) {
        int pidx = isTournament ? col : (humanP_ == Player::P1 ? 1 : 0);
        for (int i = 0; i < NB_ALGOS; ++i)
            if (hit(algoPos(ws, col, i, nbCols), {240.f, 46.f}, pos))
                aiAlgo_[pidx] = ALGO_OPTIONS[i].algo;
        if (aiAlgo_[pidx] != "random")
            for (int i = 0; i < NB_DEPTHS; ++i)
                if (hit(depthPos(ws, col, i, nbCols), {240.f, 42.f}, pos))
                    aiDepth_[pidx] = DEPTH_OPTIONS[i].depth;
    }

    if (hit({ws.x/2.f-120.f, ws.y*0.87f}, {240.f, 48.f}, pos))
        screen_ = AppScreen::BoardSelect;
    else if (hit({ws.x/2.f-120.f, ws.y*0.94f}, {240.f, 38.f}, pos))
        screen_ = (mode_ == GameMode::VsAI) ? AppScreen::PlayerSelect : AppScreen::Menu;
}

// ─────────────────────────────────────────────────────────────────────────────

void App::onEndClick(sf::Vector2i pos) {
    auto ws = window_.getSize();
    float cx = ws.x / 2.f, cy = ws.y / 2.f;
    if      (hit({cx - 125.f, cy + 10.f}, {250.f, 55.f}, pos)) {
        switch (boardType_) {
            case BoardType::Standard10x10: board_ = Board(10,10); board_.setupDefault(); break;
            case BoardType::Cross9x9:      board_ = Board(9, 9);  board_.setupCross();   break;
            default:                       board_ = Board(8, 8);  board_.setupDefault(); break;
        }
        startGame();
    } else if (hit({cx - 125.f, cy + 80.f}, {250.f, 55.f}, pos)) {
        screen_ = AppScreen::Menu;
    }
}
