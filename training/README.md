# Training BlobWarCNN

Pipeline complet pour entraîner le réseau de neurones utilisé par l'IA.

---

## Prérequis

```bash
pip install -r requirements.txt   # torch >= 2.0, numpy >= 1.24
```

---

## Fichiers

| Fichier | Rôle |
|---|---|
| `train.py` | Entraînement supervisé — **éditer la config en haut du fichier** |
| `model.py` | Architecture CNN (ne pas modifier) |
| `dataset.py` | Lecture des fichiers `.bin` (ne pas modifier) |
| `export_weights.py` | Export du modèle `.pt` → `cnn_weights.bin` pour le C++ |

---

## Workflow complet

### 1. Générer les datasets (depuis `build/`)

```bash
cd build

# Dataset bootstrap shallow : 10 000 parties, profondeur 4
./data_gen bootstrap_shallow --games 10000 --depth 4 --out dataset_bootstrap_d4.bin

# Dataset bootstrap deep : 5 000 parties, profondeur 6
./data_gen bootstrap_deep --games 5000 --depth 6 --out dataset_bootstrap_d6.bin

# Self-play : 2 000 parties, 200 ms par coup (après avoir un modèle entraîné)
./data_gen selfplay --games 2000 --time-ms 200 --out dataset_selfplay.bin

# Ou tout d'un coup avec les paramètres par défaut :
./data_gen --all
```

### 2. Configurer l'entraînement

Ouvrir `train.py` et modifier la section **CONFIGURATION** en haut :

```python
# Fichiers à utiliser (un ou plusieurs, ils sont concaténés)
DATA_FILES = [
    "../build/dataset_bootstrap_d4.bin",
    "../build/dataset_bootstrap_d6.bin",
]

# Reprendre un entraînement existant (None = partir de zéro)
MODEL_IN = None
# MODEL_IN = "model.pt"

MODEL_OUT  = "model.pt"   # fichier de sortie
EPOCHS     = 10
BATCH_SIZE = 128
LR         = 1e-3
VAL_RATIO  = 0.1          # 10% des données pour la validation
DEVICE     = None         # None = auto (cuda si dispo, sinon cpu)
```

### 3. Lancer l'entraînement

```bash
cd training
python train.py
```

Sortie attendue :
```
[Train] device=cpu
[Dataset] 45231 positions chargées depuis ../build/dataset_bootstrap_d4.bin
[Dataset] 22418 positions chargées depuis ../build/dataset_bootstrap_d6.bin
[Train] 60884 train / 6765 val  (total=67649)
Epoch   1/10  train=0.08412  val=0.07923  lr=1.00e-03
  ↳ sauvegardé → model.pt
Epoch   2/10  train=0.07105  val=0.07341  lr=1.00e-03
  ↳ sauvegardé → model.pt
...
[Train] Terminé. Meilleure val_loss=0.06812
```

### 4. Exporter les poids vers le C++

```bash
python export_weights.py --model model.pt --output ../build/cnn_weights.bin
```

Le fichier `cnn_weights.bin` est automatiquement chargé par `CNNEval` au démarrage (cherché dans le répertoire courant).

---

## Cas d'usage courants

### Bootstrap initial (première fois)
```python
DATA_FILES = ["../build/dataset_bootstrap_d4.bin",
              "../build/dataset_bootstrap_d6.bin"]
MODEL_IN   = None
EPOCHS     = 10
LR         = 1e-3
```

### Continuer l'entraînement sur self-play
```python
DATA_FILES = ["../build/dataset_selfplay.bin"]
MODEL_IN   = "model.pt"   # reprend les poids existants
EPOCHS     = 5
LR         = 1e-4         # lr réduit pour le fine-tuning
```

### Combiner bootstrap + self-play
```python
DATA_FILES = ["../build/dataset_bootstrap_d4.bin",
              "../build/dataset_bootstrap_d6.bin",
              "../build/dataset_selfplay.bin"]
MODEL_IN   = "model.pt"
EPOCHS     = 5
LR         = 1e-4
```

---

## Architecture du modèle

```
Input : (B, 3, 8, 8)   — 3 canaux : bb_P1, bb_P2, holes
Conv1 : 3  → 16 filtres, 3×3, padding=same, ReLU
Conv2 : 16 → 32 filtres, 3×3, padding=same, ReLU
Flatten: 32×8×8 = 2048
FC1   : 2048 → 64, ReLU
FC2   : 64   → 1,  tanh
Output: scalaire ∈ (-1, +1)  — proche de +1 = bon pour P1, -1 = bon pour P2
```

Identique à `CNNEval.hpp` — les poids exportés sont directement compatibles.

---

## Format des datasets

Fichier binaire : `N × 28 octets`

```
{ bb0: uint64, bb1: uint64, holes: uint64, target: float32 }
```

| Champ | Description |
|---|---|
| `bb0` | Bitboard P1 (bit i = case (i%8, i//8)) |
| `bb1` | Bitboard P2 |
| `holes` | Cases inaccessibles |
| `target` | Bootstrap : `tanh(score/100)` — Self-play : `+1 / 0 / -1` |
