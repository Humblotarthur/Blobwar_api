=== BLOBWAR - README ===

--- Description ---
Jeu de plateau Blobwar 2 joueurs, plateau variable, avec interface SFML 3.0 et IA externe.

--- Règles résumées ---
- Plateau NxM, cases : Vide / Joueur1 / Joueur2 / Hole
- Clone (distance Chebyshev = 1) : pion original reste, nouveau pion créé
- Saut   (distance Chebyshev = 2) : pion original disparaît, nouveau pion créé
- Après déplacement : pions adverses adjacents (dist=1) convertis
- Fin : plateau plein (hors holes) OU aucun coup valide
- Victoire : joueur avec le plus de pions. Égalité = nul.
- Note : règle de répétition NON implémentée (hors scope)

--- Architecture ---
src/
  engine/         Moteur pur (aucune dépendance SFML)
    Board          Plateau 1D contigu, types Cell/Player/Move
    Rules          Validation des coups, détection fin de partie, score
    MoveGen        Génération de tous les coups légaux d'un joueur

  ui/             Interface graphique SFML 3.0 (tâche 4)
    Renderer       Rendu via VertexArray
    InputHandler   Gestion clics souris (sélection départ → arrivée)

  ai/             Gestion IA
    AIBase         Interface abstraite (chooseMove / name)
    AIProcess      Pilote un binaire IA externe via stdin/stdout
    Random/        IA aléatoire (référence)
    Minimax/       IA Minimax
    AlphaBeta/     IA Alpha-Beta
    MCTS/          IA Monte Carlo Tree Search
    Tournament/    Orchestration de matchs entre plusieurs IA

  main.cpp        Point d'entrée principal

ai_stub/          Binaire IA externe séparé (communique via stdin/stdout)
                  Format : ((x1,y1),(x2,y2))

--- Build ---
Dépendances : C++17, CMake >= 3.16, SFML 3.0 (pour ui/ uniquement)

  mkdir build && cd build
  cmake ..
  make

