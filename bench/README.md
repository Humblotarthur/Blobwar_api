# Bench — Outil de benchmark Blobwar

Lance des parties entre deux IA, mesure les temps de réflexion par coup et produit un rapport dans `log/output.txt`.

---

## Usage

```
bench <algo1> [-d] <val1> <algo2> [-d] <val2> [board] [num_games]
```

- **`-d`** : le paramètre suivant est un **temps limite en ms** (modèles dynamiques avec iterative deepening)  
- **sans `-d`** : le paramètre est une **profondeur fixe**

---

## Modèles disponibles

### Profondeur fixe — `bench <algo> <depth>`

| Nom | Classe | Description |
|-----|--------|-------------|
| `ab` | `AlphaBetaAI` | Alpha-Beta classique séquentiel |
| `negamax` | `NegamaxAI` | Negamax alpha-beta séquentiel |
| `negamax_par` | `NegamaxParallelAI` | Negamax parallèle YBW (root-parallel TBB) |
| `negamax_par_dyn` | `NegamaxYBWAI` | YBW + élagage dynamique (globalAlpha atomique) |
| `negamax_par_inc` | `NegamaxParIncAI` | YBW + état incrémental (moves+score incrémentaux) |
| `ybwz` | `NegamaxParIncAI_YBWsearchZobrist` | YBW + Zobrist hashing (transposition table) |
| `stud` | `NegamaxParIncAIStud` | Parametrique — charge `negamax_config.ini` |

### Dynamiques (iterative deepening) — `bench <algo> -d <ms>`

| Nom | Classe | Description |
|-----|--------|-------------|
| `ybw_dyn` | `NegamaxYBWDynAI` | YBW + iterative deepening |
| `ybwz_dyn` | `NegamaxParIncAI_YBWsearchZobristDYN` | YBW + Zobrist + iterative deepening **(référence)** |
| `stud_dyn` | `NegamaxParIncAIDynStud` | Parametrique + iterative deepening (charge `negamax_config.ini`) |
| `stud_zero` | `NegamaxParIncAIDynStud` | `stud_dyn` sans aucun élagage — baseline pure |
| `pmr_dyn` | `NegamaxParIncAIDynStud_PMR` | PMR activé (pmrRatio=0.2, pmrMinDepth=3) |
| `pmr_zero` | `NegamaxParIncAIDynStud_PMR` | PMR désactivé — base logique PMR |
| `lmr2_zero` | `NegamaxParIncAIDynStud_PMR_LMR2` | LMR v2 désactivé — base logique LMR2 |
| `dyn_zero` | `NegamaxParIncAIDynStud` | Dynamique sans PMR/LMR — baseline |

---

## Plateaux

| Nom | Taille | Description |
|-----|--------|-------------|
| `classic8` | 8×8 | Cases vides, pions aux 4 coins (défaut) |
| `cross8` | 8×8 | Trous en diamant au centre |
| `standard10` | 10×10 | Cases vides, pions aux 4 coins |
| `cross9` | 9×9 | Trous 2×2 dans chaque angle |

---

## Exemples

```bash
# Deux modèles dynamiques face à face (250ms chacun)
./bench ybwz_dyn -d 250 ybwz_dyn -d 250

# Modèle dynamique vs profondeur fixe
./bench ybwz_dyn -d 250 ab 5
./bench ybwz_dyn -d 250 negamax_par_inc 6 classic8 3

# Comparaison PMR vs référence (5 parties, 250ms)
./bench pmr_dyn -d 250 ybwz_dyn -d 250 classic8 5

# Modèles baselines (toutes les baselines avec 150ms)
./bench stud_zero -d 150 ybwz_dyn -d 150
./bench pmr_zero  -d 150 ybwz_dyn -d 150
./bench lmr2_zero -d 150 ybwz_dyn -d 150
./bench dyn_zero  -d 150 ybwz_dyn -d 150

# Modèles profondeur fixe classiques
./bench ab 5 negamax_par 5 classic8 3
./bench negamax_par_inc 5 ybwz 5 cross8 3

# Modèle stud (charge negamax_config.ini automatiquement)
./bench stud 5 ybwz 5
./bench stud_dyn -d 250 ybwz_dyn -d 250
```

---

## Résultats

Les résultats sont écrits dans `log/output.txt` (append).

```bash
cat log/output.txt       # lire les résultats
rm  log/output.txt       # repartir à zéro
```

### Format du log

```
Plateau    : classic8
P1         : pmr_dyn (t=250ms)
P2         : ybwz_dyn (t=250ms)
Parties    : 5

=== Série : 5 partie(s) ===
  P1 : NegamaxParIncAIDynStud_PMR(t=250ms,pmr=0.200000)
  P2 : YBWsearchZobristDYN(t=250ms)
  Victoires P1 : 3  |  Victoires P2 : 2  |  Nuls : 0

--- Temps moyen par tranche (toutes parties) ---
Tranche   Nb P1   Moy P1 (ms)   Nb P2   Moy P2 (ms)
------------------------------------------------------
0-9       25      252.10        25      251.88
...
```

### Aide

```bash
./bench -h
./bench --help
```
