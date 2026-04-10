# Blobwar

Jeu de plateau 2 joueurs avec interface graphique SFML 3.0, plusieurs IA et un outil de benchmark.

---

## Règles

| Action | Distance Chebyshev | Effet |
|--------|--------------------|-------|
| Clone  | 1                  | Pion original reste, nouveau pion créé à la destination |
| Saut   | 2                  | Pion original disparaît, nouveau pion créé à la destination |

Après chaque coup : les pions adverses adjacents (dist = 1) sont convertis.
Fin de partie : plateau plein (hors holes) **ou** aucun coup valide pour les deux joueurs.
Victoire : joueur avec le plus de pions. Égalité possible.

---

## Architecture

```
src/
  engine/               Moteur pur (sans dépendance externe)
    Board               Plateau 1D, types Cell / Player / Move
    Rules               Validation, applyMove, détection fin de partie
    MoveGen             Génération de tous les coups légaux

  ui/                   Interface graphique SFML 3.0
    App                 Boucle principale, écrans, sélection plateau/IA
    Renderer            Rendu du plateau et des pièces
    InputHandler        Gestion clics souris

  ai/
    AIBase              Interface abstraite (chooseMove / name)
    AIProcess           Pilote ai_stub via stdin/stdout
    Eval/               Fonction d'évaluation partagée
      Eval              Heuristique classique (matériel + position)
      CNNEval           Forward pass CNN (3→16→32→FC64→1, tanh×64)
      CNNTrainer        Backprop + Adam (entraînement CNN en C++)
      GameRecord        TrainSample / TrainBatch (collecte données)
      Zobrist           Table de transposition (~16 MB)
    AlphaBeta/          Alpha-Beta classique
    Negamax/            Negamax alpha-beta séquentiel
    NegamaxParallel/    Negamax parallèle TBB (root-parallel)
    NegamaxYBW/         YBW + globalAlpha atomique (élagage dynamique inter-threads)
    NegamaxParInc/      YBW + état incrémental + Zobrist TT + PMR/LMR + iterative deepening

ai_stub/                Binaire IA séparé, communique via stdin/stdout
bench/                  Outil de benchmark (BenchAPI + exécutable)
data_gen/               Générateur de datasets CNN (bootstrap + self-play)
training/               Pipeline d'entraînement Python (voir training/README.md)
log/                    Fichiers de résultats générés par le benchmark
```

---

## Dépendances

- C++17
- CMake ≥ 3.16
- SFML 3.0 (fourni dans `lib/`)
- Intel TBB (pour les IA parallèles)

---

## Build

```bash
mkdir build && cd build
cmake ..
make          # compile tout : blobwar, ai_stub, bench
```

Cibles individuelles :
```bash
make blobwar     # interface graphique uniquement
make ai_stub     # binaire IA
make bench       # outil de benchmark
```

---

## Lancer le jeu

```bash
./blobwar
```

### Plateaux disponibles

| Nom             | Taille | Description                        |
|-----------------|--------|------------------------------------|
| 8×8 - Classique | 8×8    | Cases vides, pions aux 4 coins     |
| 8×8 - Croix     | 8×8    | Trous en diamant au centre         |
| 10×10 - Standard| 10×10  | Cases vides, pions aux 4 coins     |
| 9×9 - Croix     | 9×9    | Trous 2×2 dans chaque angle        |

### Algorithmes IA disponibles (en jeu)

| Label UI        | Clé interne        | Description                                      |
|-----------------|--------------------|--------------------------------------------------|
| AlphaBeta       | `ab`               | Alpha-Beta classique séquentiel                  |
| Negamax //      | `negamax_par`      | Negamax parallèle TBB (root-parallel)            |
| Negamax // Dyn  | `negamax_par_dyn`  | + élagage dynamique inter-threads (YBW + atomic) |

---

## Benchmark

L'outil `bench` lance des parties entre deux IA, mesure le temps de réflexion par coup et produit un rapport par **tranche de 10 coups**.

Les résultats sont écrits dans **`log/output.txt`** (append, jamais dans le terminal).  
Voir `bench/README.md` pour la documentation complète.

### Usage

```bash
./bench <algo1> [-d] <val1> <algo2> [-d] <val2> [board] [num_games]
./bench -h   # aide complète
```

**`-d`** : le paramètre suivant est un **temps limite en ms** (modèles dynamiques avec iterative deepening).  
Sans `-d` : le paramètre est une **profondeur fixe**.

### Modèles disponibles

**Profondeur fixe :**

| Nom | Description |
|-----|-------------|
| `ab` | Alpha-Beta classique |
| `negamax` | Negamax séquentiel |
| `negamax_par` | Negamax parallèle YBW |
| `negamax_par_dyn` | YBW + élagage dynamique |
| `negamax_par_inc` | YBW + état incrémental |
| `ybwz` | YBW + Zobrist hashing |
| `stud` | Parametrique (negamax_config.ini) |

**Dynamiques — utiliser avec `-d <ms>` :**

| Nom | Description |
|-----|-------------|
| `ybwz_dyn` | YBW + Zobrist + iterative deepening **(référence)** |
| `ybw_dyn` | YBW + iterative deepening |
| `stud_dyn` | Parametrique + iterative deepening |
| `stud_zero` | stud_dyn sans élagage (baseline) |
| `pmr_dyn` | PMR activé (ratio=0.2) |
| `pmr_zero` | PMR désactivé (baseline) |
| `lmr2_zero` | LMR v2 désactivé (baseline) |
| `dyn_zero` | Sans PMR/LMR (baseline pure) |

### Exemples

```bash
# Deux modèles dynamiques (250ms chacun)
./bench ybwz_dyn -d 250 ybwz_dyn -d 250

# Dynamique vs profondeur fixe
./bench ybwz_dyn -d 250 ab 5

# Comparaison PMR vs référence, 5 parties
./bench pmr_dyn -d 250 ybwz_dyn -d 250 classic8 5

# Modèles classiques
./bench ab 5 negamax_par 5 cross8 3
```

### Lire les résultats

```bash
cat log/output.txt   # résultats
rm  log/output.txt   # repartir à zéro
```

---

## Entraînement CNN

Le réseau de neurones évalue les positions pour guider la recherche.  
Voir **`training/README.md`** pour la documentation complète.

### Générer les datasets (depuis `build/`)

```bash
# Bootstrap supervisé (deux profondeurs)
./data_gen bootstrap_shallow --games 10000 --depth 4 --out dataset_bootstrap_d4.bin
./data_gen bootstrap_deep    --games 5000  --depth 6 --out dataset_bootstrap_d6.bin

# Self-play (après un premier entraînement)
./data_gen selfplay --games 2000 --time-ms 200 --out dataset_selfplay.bin

# Tout d'un coup avec les valeurs par défaut
./data_gen --all
```

### Entraîner et exporter

```bash
cd training
# Éditer DATA_FILES / EPOCHS / LR en haut de train.py
python train.py
python export_weights.py --model model.pt --output ../build/cnn_weights.bin
```

`cnn_weights.bin` placé dans le répertoire de travail est chargé automatiquement par `CNNEval`.

---

## Communication IA externe (ai_stub)

Le binaire `ai_stub` reçoit sur stdin :
```
W H player
c00 c01 … c(W-1)(H-1)
```
Et répond sur stdout :
```
((x1,y1),(x2,y2))
```

---

## Algorithmes IA (détail technique)

### NegamaxParDyn — Élagage dynamique inter-threads
- **YBW** : 1er coup exploré séquentiellement → établit `globalAlpha` réel
- **globalAlpha atomique** partagé entre les threads TBB
- À chaque nœud où `current == rootPlayer` : `alpha = max(alpha, globalAlpha)` → coupure si `alpha ≥ beta`
- Mise à jour thread-safe via CAS

### NegamaxParInc — État incrémental
- Remplace `MoveGen` + `Eval` par un état `IncState` maintenu incrémentalement
- `moveBuf[2][512]` : listes de coups sans allocation heap sur copie
- `applyMove` incrémental : libère source (saut), occupe destination, convertit adjacents — chaque étape met à jour moves + score localement
- Même schéma parallèle YBW + globalAlpha atomique
