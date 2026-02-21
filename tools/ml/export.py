"""Export MLP policy weights to/from the C++ MLPPolicy binary format.

Binary format (little-endian):
    Header:
        uint32  magic = 0x4D4C5001  ("MLP\\x01")
        uint32  numLayers
    Per layer:
        uint32  inputDim
        uint32  outputDim
        float32[outputDim * inputDim]  weights (row-major)
        float32[outputDim]             biases

This matches src/core/ml/MLPPolicy.h loadWeights().
All functions except export_policy() use stdlib only (no numpy/torch).
"""

import math
import random
import struct
from pathlib import Path

MAGIC = 0x4D4C5001  # "MLP\x01"


def export_policy(policy, output_path: str):
    """Export a PyTorch MLPPolicy to C++ binary format.

    Args:
        policy: any nn.Module with a get_layer_params() method yielding
                (weights_ndarray, biases_ndarray, in_dim, out_dim) per layer.
        output_path: path to write the binary file.

    Requires: numpy (via PyTorch).
    """
    import numpy as np

    layers = list(policy.get_layer_params())
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<I", len(layers)))

        for weights, biases, in_dim, out_dim in layers:
            f.write(struct.pack("<I", in_dim))
            f.write(struct.pack("<I", out_dim))

            w = weights.astype(np.float32)
            assert w.shape == (out_dim, in_dim), f"Expected ({out_dim}, {in_dim}), got {w.shape}"
            f.write(w.tobytes())

            b = biases.astype(np.float32)
            assert b.shape == (out_dim,), f"Expected ({out_dim},), got {b.shape}"
            f.write(b.tobytes())

    expected_size = 8
    for _, _, in_dim, out_dim in layers:
        expected_size += 8 + out_dim * in_dim * 4 + out_dim * 4
    actual_size = Path(output_path).stat().st_size
    assert actual_size == expected_size, (
        f"Size mismatch: expected {expected_size}, got {actual_size}"
    )

    print(f"Exported {len(layers)} layers to {output_path} ({actual_size} bytes)")
    for i, (_, _, in_dim, out_dim) in enumerate(layers):
        print(f"  Layer {i}: {in_dim} -> {out_dim}")


def export_random_policy(input_dim: int, output_dim: int, output_path: str,
                         hidden_dim: int = 1024, hidden_layers: int = 3,
                         seed: int = 42):
    """Generate and export Xavier-initialized random weights.

    Stdlib only — no numpy or torch required.
    """
    rng = random.Random(seed)

    def _make_layer(in_dim: int, out_dim: int) -> tuple:
        stddev = math.sqrt(2.0 / (in_dim + out_dim))
        weights = [rng.gauss(0, stddev) for _ in range(out_dim * in_dim)]
        biases = [0.0] * out_dim
        return weights, biases, in_dim, out_dim

    layers = []
    prev_dim = input_dim
    for _ in range(hidden_layers):
        layers.append(_make_layer(prev_dim, hidden_dim))
        prev_dim = hidden_dim
    layers.append(_make_layer(prev_dim, output_dim))

    Path(output_path).parent.mkdir(parents=True, exist_ok=True)

    with open(output_path, "wb") as f:
        f.write(struct.pack("<I", MAGIC))
        f.write(struct.pack("<I", len(layers)))
        for weights, biases, in_dim, out_dim in layers:
            f.write(struct.pack("<I", in_dim))
            f.write(struct.pack("<I", out_dim))
            f.write(struct.pack(f"<{len(weights)}f", *weights))
            f.write(struct.pack(f"<{len(biases)}f", *biases))

    size = Path(output_path).stat().st_size
    print(f"Exported random policy to {output_path} ({size} bytes)")
    print(f"  Architecture: {input_dim} -> " +
          " -> ".join([f"{hidden_dim}" for _ in range(hidden_layers)]) +
          f" -> {output_dim}")


def verify_weights(path: str):
    """Read back and verify a weight file. Stdlib only."""
    with open(path, "rb") as f:
        magic, = struct.unpack("<I", f.read(4))
        assert magic == MAGIC, f"Bad magic: 0x{magic:08X} (expected 0x{MAGIC:08X})"

        num_layers, = struct.unpack("<I", f.read(4))
        print(f"Weight file: {path}")
        print(f"  Layers: {num_layers}")

        for i in range(num_layers):
            in_dim, = struct.unpack("<I", f.read(4))
            out_dim, = struct.unpack("<I", f.read(4))

            weight_count = out_dim * in_dim
            weight_bytes = f.read(weight_count * 4)
            bias_bytes = f.read(out_dim * 4)

            assert len(weight_bytes) == weight_count * 4, (
                f"Layer {i}: expected {weight_count * 4} weight bytes, got {len(weight_bytes)}"
            )
            assert len(bias_bytes) == out_dim * 4, (
                f"Layer {i}: expected {out_dim * 4} bias bytes, got {len(bias_bytes)}"
            )

            weights = struct.unpack(f"<{weight_count}f", weight_bytes)
            biases = struct.unpack(f"<{out_dim}f", bias_bytes)

            print(f"  Layer {i}: {in_dim} -> {out_dim}, "
                  f"weights [{min(weights):.4f}, {max(weights):.4f}], "
                  f"biases [{min(biases):.4f}, {max(biases):.4f}]")

        remaining = f.read()
        assert len(remaining) == 0, f"Unexpected {len(remaining)} trailing bytes"

    print("  Verification: OK")
