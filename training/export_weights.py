"""
Export des poids PyTorch vers le format binaire CNNW lu par CNNEval.cpp.

Usage :
  python export_weights.py --model model.pt --output cnn_weights.bin

Format CNNW (identique à CNNEval::saveWeights) :
  [MAGIC: "CNNW" 4B][VERSION: uint32=1][weights float32 LE dans l'ordre de déclaration]

Ordre des arrays :
  conv1w (16×3×3×3 = 432 f32)
  conv1b (16 f32)
  conv2w (32×16×3×3 = 4608 f32)
  conv2b (32 f32)
  fc1w   (64×2048 = 131072 f32)
  fc1b   (64 f32)
  fc2w   (1×64 = 64 f32)    → C++ lit comme std::array<float, 64>
  fc2b   (1 f32 scalaire)
"""

import argparse
import struct
import numpy as np
import torch

from model import BlobWarCNN


MAGIC   = 0x574E4E43  # 'C','N','N','W' en little-endian
VERSION = 1


def export(model_path: str, output_path: str):
    model = BlobWarCNN()
    model.load_state_dict(torch.load(model_path, map_location='cpu'))
    model.eval()

    with open(output_path, 'wb') as f:
        # En-tête
        f.write(struct.pack('<II', MAGIC, VERSION))

        def write_array(param: torch.Tensor):
            data = param.detach().cpu().numpy().astype(np.float32)
            f.write(data.tobytes())  # little-endian float32

        write_array(model.conv1.weight)   # (16, 3, 3, 3)
        write_array(model.conv1.bias)     # (16,)
        write_array(model.conv2.weight)   # (32, 16, 3, 3)
        write_array(model.conv2.bias)     # (32,)
        write_array(model.fc1.weight)     # (64, 2048)
        write_array(model.fc1.bias)       # (64,)
        write_array(model.fc2.weight)     # (1, 64)  → 64 floats, compatible C++
        write_array(model.fc2.bias)       # (1,)     → 1 float scalaire

    total_params = sum(p.numel() for p in model.parameters())
    size_kb = (total_params * 4 + 8) / 1024
    print(f"[Export] {total_params} paramètres → {output_path}  ({size_kb:.1f} KB)")


def main():
    p = argparse.ArgumentParser(description="Export BlobWarCNN → CNNW binaire")
    p.add_argument('--model',  required=True, help="model.pt (PyTorch state dict)")
    p.add_argument('--output', default='cnn_weights.bin', help="Fichier de sortie")
    args = p.parse_args()
    export(args.model, args.output)


if __name__ == '__main__':
    main()
