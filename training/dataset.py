"""
BlobWarDataset — lit le fichier binaire produit par data_gen.

Format : N × { bb0:uint64, bb1:uint64, holes:uint64, target:float32 }  (28 octets/position)
"""

import numpy as np
import torch
from torch.utils.data import Dataset


# dtype numpy correspondant à TrainSample du C++
_SAMPLE_DTYPE = np.dtype([
    ('bb0',    '<u8'),
    ('bb1',    '<u8'),
    ('holes',  '<u8'),
    ('target', '<f4'),
])


def _bitboards_to_planes(bbs: np.ndarray) -> np.ndarray:
    """
    Convertit un tableau (N,) de uint64 en plans (N, 8, 8) float32.
    Utilise des opérations numpy vectorisées — pas de boucle Python.
    """
    bits = np.arange(64, dtype=np.uint64)
    planes = ((bbs[:, None] >> bits[None, :]) & np.uint64(1)).astype(np.float32)
    return planes.reshape(-1, 8, 8)


class BlobWarDataset(Dataset):
    def __init__(self, path: str):
        data = np.fromfile(path, dtype=_SAMPLE_DTYPE)
        if len(data) == 0:
            raise ValueError(f"Fichier vide ou format incorrect : {path}")

        # Pré-calcul de tous les tenseurs en mémoire
        bb0   = _bitboards_to_planes(data['bb0'])    # (N, 8, 8)
        bb1   = _bitboards_to_planes(data['bb1'])    # (N, 8, 8)
        holes = _bitboards_to_planes(data['holes'])  # (N, 8, 8)

        # Input : (N, 3, 8, 8)
        self.inputs  = torch.from_numpy(
            np.stack([bb0, bb1, holes], axis=1)      # (N, 3, 8, 8)
        )
        self.targets = torch.from_numpy(data['target'].copy())

        print(f"[Dataset] {len(self)} positions chargées depuis {path}")

    def __len__(self) -> int:
        return len(self.targets)

    def __getitem__(self, idx):
        return self.inputs[idx], self.targets[idx]
