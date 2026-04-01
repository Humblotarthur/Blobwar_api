# Architecture du projet Blob_war

## Structure complète

```
Blob_war/
├── CMakeLists.txt              ← build principal (blobwar, ai_stub, bench, optimizer)
├── negamax_config.ini          ← config runtime de NegamaxParIncAI
├── src/
│   ├── main.cpp                ← entrée principale (UI + IA)
│   ├── engine/                 ← moteur de jeu (package indépendant)
│   │   ├── Board.hpp/.cpp      ← plateau, Cell, Player, Move
│   │   ├── Rules.hpp/.cpp      ← règles du jeu, GameStatus
│   │   └── MoveGen.hpp/.cpp    ← génération de coups (Board-based)
│   ├── ai/                     ← package IA
│   │   ├── AIBase.hpp          ← interface commune (chooseMove)
│   │   ├── AIProcess.hpp/.cpp  ← orchestrateur processus IA (fork/pipe)
│   │   ├── BBState.hpp/.cpp    ← représentation bitboard (bb[0], bb[1], holes, mask)
│   │   │                         fromBoard(), applyMove(), genMoves(), evalFor()
│   │   ├── Eval/               ← fonctions d'évaluation (package indépendant)
│   │   │   ├── Eval.hpp/.cpp   ← heuristique classique (comptage + poids positionnels)
│   │   │   ├── Zobrist.hpp     ← table de transposition Zobrist
│   │   │   ├── CNNEval.hpp/.cpp← CNN forward pass (3→16→32→FC64→1, tanh×64)
│   │   │   ├── CNNTrainer.hpp/.cpp ← backprop + Adam optimizer (entraînement CNN)
│   │   │   └── GameRecord.hpp  ← TrainSample, TrainBatch (collecte données RL)
│   │   ├── Random/             ← IA aléatoire (baseline)
│   │   ├── AlphaBeta/          ← alpha-beta séquentiel classique
│   │   ├── Negamax/            ← negamax séquentiel simple
│   │   ├── NegamaxParallel/    ← negamax parallèle basique (tbb)
│   │   ├── NegamaxYBW/         ← YBW root-parallel (1 niveau de parallélisme)
│   │   │   ├── NegamaxYBWAI.hpp← YBW à la racine + alpha-beta séquentiel en dessous
│   │   │   └── NegamaxYBWAI.cpp│  pas de TT, pas de LMR, état copié (BBState)
│   │   ├── NegamaxParInc/      ← IA principale (la plus avancée)
│   │   │   ├── NegamaxParIncAI.hpp  ← YBWsearchZobristPMR : YBW + Zobrist TT +
│   │   │   │                          PMR pruning + LMR + état incrémental apply/undo
│   │   │   ├── NegamaxParIncAI.cpp
│   │   │   ├── NegamaxParIncAIStud.hpp ← variante étudiant configurable (NegamaxConfig)
│   │   │   │                             + iterative deepening + killer heuristic (task1)
│   │   │   ├── NegamaxParIncAIDynStud.hpp ← variante étudiant dynamique (time limit)
│   │   │   ├── NegamaxParIncAIDYN.hpp  ← NEW (task1): NegamaxParIncAI + iterative
│   │   │   │                              deepening + killer heuristic, non-configurable
│   │   │   ├── NegamaxYBWDynAI.hpp  ← YBW + Zobrist + iterative deepening
│   │   │   ├── NegamaxConfig.hpp    ← paramètres runtime (depth, LMR, PMR, use_cnn_eval)
│   │   │   ├── IncState.hpp         ← état incrémental du plateau
│   │   │   └── IncMoveState.hpp     ← état incrémental + moves
│   │   └── NegamaxVariants/    ← archive des variantes inactives (référence uniquement)
│   │       ├── YBWsearch/          ← YBW + tri statique
│   │       ├── YBWsearchDYN/       ← NEW (task1): YBW + iterative deepening + killer
│   │       ├── YBWsearchnotri/     ← YBW sans tri
│   │       ├── YBWsearchtrinoYBW/  ← tri sans parallélisme YBW
│   │       ├── YBWsearchnoatomic/  ← parallel_reduce sans atomics
│   │       ├── Parallelsearch/     ← parallel_reduce sans tri
│   │       └── YBWsearchZobrist/   ← YBW + Zobrist sans PMR/LMR
│   └── ui/                     ← rendu SFML (package indépendant)
│       ├── App.hpp/.cpp        ← boucle principale SFML
│       ├── Renderer.hpp/.cpp   ← rendu plateau + pièces
│       └── InputHandler.hpp    ← gestion souris/clavier
├── ai_stub/
│   └── main.cpp                ← binaire fils IA (sans SFML, lancé par AIProcess)
├── bench/
│   ├── main.cpp
│   └── BenchAPI.hpp/.cpp       ← benchmark des algorithmes (win rate, temps, nodes)
└── optimizer/
    ├── main.cpp                ← CLI : --state/--games/--time/--passes, défaut 250ms
    ├── Optimizer.hpp/.cpp      ← Phase1 (grille vs YBWDyn 250ms) + Phase2 (exponential
    │                              backoff : lmrMinDepth→lmrAlpha→lmrK→lmpPruneRatio)
```

## Algorithmes IA (du plus simple au plus avancé)

| Classe | Dossier | Description |
|---|---|---|
| `RandomAI` | `Random/` | Coup aléatoire, baseline |
| `AlphaBetaAI` | `AlphaBeta/` | Alpha-beta séquentiel |
| `NegamaxAI` | `Negamax/` | Negamax séquentiel |
| `NegamaxParallelAI` | `NegamaxParallel/` | Negamax parallèle basique |
| `NegamaxYBWAI` | `NegamaxYBW/` | YBW root-parallel, alpha-beta séquentiel sous la racine, état copié |
| `NegamaxParIncAI` | `NegamaxParInc/` | **IA principale** : YBW multi-niveaux + Zobrist TT + PMR + LMR + état incrémental + CNN optionnel |
| `NegamaxParIncAIDYN` | `NegamaxParInc/` | **NEW (task1)** : NegamaxParIncAI + iterative deepening (time limit) + killer heuristic + prev-iter reordering |
| `NegamaxParIncAIStud` (modifié) | `NegamaxParInc/` | Configurable via NegamaxConfig + iterative deepening + killer heuristic |
| `NegamaxParIncAI_YBWsearchDYN` | `NegamaxVariants/YBWsearchDYN/` | **NEW (task1)** : YBWsearch + iterative deepening + killer heuristic + prev-iter reordering |

## Package Eval

| Fichier | Rôle |
|---|---|
| `Eval.hpp/.cpp` | Heuristique classique : `(pièces_moi - pièces_eux) + poids_positionnels` |
| `CNNEval.hpp/.cpp` | Forward pass CNN : `3×8×8 → Conv16 → Conv32 → FC64 → tanh×64` |
| `CNNTrainer.hpp/.cpp` | Backprop + Adam (β1=0.9, β2=0.999), loss MSE |
| `GameRecord.hpp` | `TrainSample{bb0, bb1, holes, target}` + `TrainBatch` |
| `Zobrist.hpp` | Table de transposition (~16 MB), utilisée par NegamaxParInc |

## Dépendances inter-packages

```
ui          → engine
ai_stub     → ai, engine
bench       → ai, engine
optimizer   → ai, engine
ai/*/       → engine (Board, Rules)
ai/*/       → ai/BBState
ai/NegamaxParInc → ai/Eval (Zobrist, CNNEval)
ai/NegamaxYBW    → ai/BBState uniquement
CNNTrainer       → CNNEval (friend)
```

## Binaires produits

| Binaire | Description |
|---|---|
| `blobwar` | Application principale avec UI SFML |
| `ai_stub` | Processus fils IA (sans UI), communique via pipe |
| `bench` | Benchmark des algos, sortie JSON/table |
| `optimizer` | Optimise automatiquement `negamax_config.ini` |
