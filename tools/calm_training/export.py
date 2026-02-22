"""Export CALM networks to MLP1 binary format.

Writes separate .bin files for each network component, matching
the directory layout expected by C++ ModelLoader::loadAll().

Output files:
    llc_style.bin    - Style MLP (tanh activations)
    llc_main.bin     - Main policy MLP (relu activations)
    llc_mu_head.bin  - Action head (no activation)
    encoder.bin      - Motion encoder (relu hidden, no output activation)
    hlc_{task}.bin   - HLC networks (relu hidden, no output activation)
"""

import struct
from pathlib import Path
from typing import List, Tuple

import numpy as np

from .networks import StyleConditionedPolicy, MotionEncoder, HLCPolicy

# MLP1 format constants (must match tools/ml/export.py and C++ ModelLoader)
MAGIC = 0x4D4C5031
VERSION = 1
ACT_NONE = 0
ACT_RELU = 1
ACT_TANH = 2


def _write_mlp_bin(
    path: Path,
    layers: List[Tuple[np.ndarray, np.ndarray, int, int]],
    activations: List[int],
) -> None:
    """Write layers to MLP1 binary format.

    Args:
        path: output .bin file
        layers: list of (weight, bias, in_dim, out_dim) tuples
        activations: activation type per layer
    """
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(path, "wb") as f:
        f.write(struct.pack("<III", MAGIC, VERSION, len(layers)))
        for i, (weight, bias, in_dim, out_dim) in enumerate(layers):
            act = activations[i] if i < len(activations) else ACT_NONE
            f.write(struct.pack("<III", in_dim, out_dim, act))
            w = weight.astype(np.float32)
            assert w.shape == (out_dim, in_dim), f"Expected ({out_dim}, {in_dim}), got {w.shape}"
            f.write(w.tobytes())
            b = bias.astype(np.float32)
            assert b.shape == (out_dim,), f"Expected ({out_dim},), got {b.shape}"
            f.write(b.tobytes())
    print(f"  Wrote {path} ({len(layers)} layers, {path.stat().st_size} bytes)")


def export_llc(policy: StyleConditionedPolicy, output_dir: str) -> None:
    """Export LLC components to three .bin files.

    Produces:
        llc_style.bin   - Style MLP with tanh activations
        llc_main.bin    - Main MLP with relu activations
        llc_mu_head.bin - Action head with no activation
    """
    output_dir = Path(output_dir)
    print("Exporting LLC components...")

    # Style MLP: all layers use tanh
    style_layers = list(policy.get_style_layers())
    style_activations = [ACT_TANH] * len(style_layers)
    _write_mlp_bin(output_dir / "llc_style.bin", style_layers, style_activations)

    # Main MLP: all layers use relu
    main_layers = list(policy.get_main_layers())
    main_activations = [ACT_RELU] * len(main_layers)
    _write_mlp_bin(output_dir / "llc_main.bin", main_layers, main_activations)

    # Mu head: no activation
    mu_layers = list(policy.get_mu_head_layers())
    mu_activations = [ACT_NONE] * len(mu_layers)
    _write_mlp_bin(output_dir / "llc_mu_head.bin", mu_layers, mu_activations)


def export_encoder(encoder: MotionEncoder, output_dir: str) -> None:
    """Export motion encoder to encoder.bin.

    Hidden layers use relu, output layer has no activation
    (L2 normalization is applied externally in C++).
    """
    output_dir = Path(output_dir)
    print("Exporting encoder...")

    layers = list(encoder.get_layers())
    # relu on hidden layers, none on output
    activations = [ACT_RELU] * (len(layers) - 1) + [ACT_NONE]
    _write_mlp_bin(output_dir / "encoder.bin", layers, activations)


def export_hlc(hlc: HLCPolicy, task: str, output_dir: str) -> None:
    """Export HLC to hlc_{task}.bin.

    Hidden layers use relu, output layer has no activation
    (L2 normalization is applied externally in C++).
    """
    output_dir = Path(output_dir)
    print(f"Exporting HLC ({task})...")

    layers = list(hlc.get_layers())
    activations = [ACT_RELU] * (len(layers) - 1) + [ACT_NONE]
    _write_mlp_bin(output_dir / f"hlc_{task}.bin", layers, activations)
