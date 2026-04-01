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
    Eval/               Fonction d'évaluation partagée (matériel + position)
    AlphaBeta/          Alpha-Beta classique
    Negamax/            Negamax alpha-beta séquentiel
    NegamaxParallel/    Negamax parallèle TBB (root-parallel)
    NegamaxParDyn/      Negamax parallèle + élagage dynamique inter-threads (YBW + globalAlpha atomique)
    NegamaxParInc/      Negamax parallèle + état incrémental (moves et score sans rescan)

ai_stub/                Binaire IA séparé, communique via stdin/stdout
bench/                  Outil de benchmark (BenchAPI + exécutable)
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

### Usage

```bash
./bench <algo1> <depth1> <algo2> <depth2> [board] [num_games]
```

| Paramètre   | Valeurs possibles                                                      | Défaut     |
|-------------|------------------------------------------------------------------------|------------|
| `algo`      | `ab` \| `negamax` \| `negamax_par` \| `negamax_par_dyn` \| `negamax_par_inc` | —      |
| `depth`     | entier (ex. 4, 6)                                                      | —          |
| `board`     | `classic8` \| `cross8` \| `standard10` \| `cross9`                    | `classic8` |
| `num_games` | entier                                                                 | `1`        |

### Exemples

```bash
# 1 partie : negamax_par_dyn vs negamax_par_inc, plateau classique 8×8
./bench negamax_par_dyn 4 negamax_par_inc 4

# 1 partie : AlphaBeta vs Negamax parallèle, plateau cross 8×8
./bench ab 4 negamax_par 4 cross8

# 5 parties : negamax vs negamax_par_dyn, profondeur 6, plateau 10×10
./bench negamax 6 negamax_par_dyn 6 standard10 5

# Comparer l'incrémental vs le dynamique sur 3 parties
./bench negamax_par_inc 5 negamax_par_dyn 5 classic8 3
```

### Lire les résultats

```bash
cat log/output.txt
```

Exemple de sortie :
```
Plateau    : classic8
P1         : NegamaxParDyn(d=4)
P2         : NegamaxParInc(d=4)
Parties    : 1

=== Résultat : NegamaxParDyn(d=4) (P1) ===
  Coups joués : 34

Tranche   Nb P1   Moy P1 (ms)   Nb P2   Moy P2 (ms)   Moy tout (ms)
--------------------------------------------------------------------
0-9       5       312.45        5       287.12        299.78
10-19     5       489.33        5       401.67        445.50
20-29     4       601.20        5       512.88        552.56
30-39     3       445.10        2       389.40        423.08
======================================================================
```

Pour repartir de zéro :
```bash
rm log/output.txt
```

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
