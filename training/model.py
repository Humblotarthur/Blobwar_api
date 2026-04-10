"""
BlobWarCNN — architecture identique à CNNEval.hpp pour compatibilité inférence C++.

  Conv1 : 3  → 16 filtres, 3×3, same padding, ReLU
  Conv2 : 16 → 32 filtres, 3×3, same padding, ReLU
  Flatten: 32×8×8 = 2048
  FC1   : 2048 → 64, ReLU
  FC2   : 64 → 1, tanh

Sortie : scalaire ∈ [-1, +1] depuis la perspective de P1.
Le C++ multiplie par SCORE_SCALE=64 à l'inférence — pas besoin ici.
"""

import torch
import torch.nn as nn


class BlobWarCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv1 = nn.Conv2d(3, 16, kernel_size=3, padding=1, bias=True)
        self.conv2 = nn.Conv2d(16, 32, kernel_size=3, padding=1, bias=True)
        self.fc1   = nn.Linear(32 * 8 * 8, 64, bias=True)
        self.fc2   = nn.Linear(64, 1, bias=True)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        """x : (B, 3, 8, 8)  →  sortie : (B,)"""
        x = torch.relu(self.conv1(x))   # (B, 16, 8, 8)
        x = torch.relu(self.conv2(x))   # (B, 32, 8, 8)
        x = x.flatten(1)               # (B, 2048)
        x = torch.relu(self.fc1(x))    # (B, 64)
        x = torch.tanh(self.fc2(x))    # (B, 1)
        return x.squeeze(-1)           # (B,)
