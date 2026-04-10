"""
BlobWarCNN — Entraînement supervisé (bootstrap) et self-play.

Configurer les paramètres ci-dessous puis lancer :
  python train.py
"""

# ==============================================================================
# CONFIGURATION — modifier ici
# ==============================================================================

# Fichiers de données (.bin générés par data_gen) — un ou plusieurs
DATA_FILES = [
    "../build/dataset_bootstrap_d4.bin",
    "../build/dataset_bootstrap_d6.bin",
]

# Modèle de départ (None = nouveau modèle aléatoire, chemin = continuer l'entraînement)
MODEL_IN = None
# MODEL_IN = "model.pt"

# Chemin de sauvegarde du meilleur modèle
MODEL_OUT = "model.pt"

# Hyperparamètres
EPOCHS     = 10
BATCH_SIZE = 128
LR         = 1e-3      # learning rate initial
VAL_RATIO  = 0.1       # fraction des données réservée à la validation

# "cpu" ou "cuda" — None = détection automatique
DEVICE = None

# ==============================================================================

import torch
import torch.nn as nn
from torch.utils.data import DataLoader, ConcatDataset, random_split

from model   import BlobWarCNN
from dataset import BlobWarDataset


def train():
    device = torch.device(
        DEVICE if DEVICE else ('cuda' if torch.cuda.is_available() else 'cpu')
    )
    print(f"[Train] device={device}")

    # ── Données ───────────────────────────────────────────────────────────────
    datasets = [BlobWarDataset(p) for p in DATA_FILES]
    dataset  = ConcatDataset(datasets) if len(datasets) > 1 else datasets[0]

    val_size   = max(1, int(len(dataset) * VAL_RATIO))
    train_size = len(dataset) - val_size
    train_ds, val_ds = random_split(dataset, [train_size, val_size])

    train_loader = DataLoader(train_ds, batch_size=BATCH_SIZE,
                              shuffle=True,  num_workers=0, pin_memory=False)
    val_loader   = DataLoader(val_ds,   batch_size=BATCH_SIZE,
                              shuffle=False, num_workers=0)

    print(f"[Train] {train_size} train / {val_size} val  (total={len(dataset)})")

    # ── Modèle ────────────────────────────────────────────────────────────────
    model = BlobWarCNN().to(device)
    if MODEL_IN is not None:
        model.load_state_dict(torch.load(MODEL_IN, map_location=device))
        print(f"[Train] poids chargés depuis {MODEL_IN}")

    optimizer = torch.optim.Adam(model.parameters(), lr=LR)
    scheduler = torch.optim.lr_scheduler.ReduceLROnPlateau(
        optimizer, patience=2, factor=0.5
    )
    criterion = nn.MSELoss()

    best_val_loss = float('inf')

    # ── Boucle d'entraînement ─────────────────────────────────────────────────
    for epoch in range(1, EPOCHS + 1):
        model.train()
        train_loss = 0.0
        for X, y in train_loader:
            X, y = X.to(device), y.to(device)
            optimizer.zero_grad()
            loss = criterion(model(X), y)
            loss.backward()
            optimizer.step()
            train_loss += loss.item()
        train_loss /= len(train_loader)

        model.eval()
        val_loss = 0.0
        with torch.no_grad():
            for X, y in val_loader:
                X, y = X.to(device), y.to(device)
                val_loss += criterion(model(X), y).item()
        val_loss /= len(val_loader)

        prev_lr = optimizer.param_groups[0]['lr']
        scheduler.step(val_loss)
        curr_lr = optimizer.param_groups[0]['lr']
        lr_tag  = f"  ↓ lr→{curr_lr:.2e}" if curr_lr != prev_lr else ""

        print(f"Epoch {epoch:>3}/{EPOCHS}  "
              f"train={train_loss:.5f}  val={val_loss:.5f}  "
              f"lr={curr_lr:.2e}{lr_tag}")

        if val_loss < best_val_loss:
            best_val_loss = val_loss
            torch.save(model.state_dict(), MODEL_OUT)
            print(f"  ↳ sauvegardé → {MODEL_OUT}")

    print(f"\n[Train] Terminé. Meilleure val_loss={best_val_loss:.5f}")


if __name__ == '__main__':
    train()
