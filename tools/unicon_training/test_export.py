#!/usr/bin/env python3
"""Round-trip verification tests for the weight export pipeline.

Tests:
1. Export → read back → compare layer by layer
2. PyTorch forward pass matches manual (C++ equivalent) matmul + ELU
3. StateEncoder observation dimensions match expected values

Usage:
    python -m tools.unicon_training.test_export
"""

import struct
import tempfile
import os

import numpy as np
import torch

from tools.ml.config import PolicyConfig
from tools.ml.policy import MLPPolicy
from tools.ml.export import MAGIC, export_policy, verify_weights

from .state_encoder import StateEncoder


def test_export_round_trip():
    """Export a policy, read back the binary, compare layer by layer."""
    obs_dim = 429  # 20 joints, tau=1
    act_dim = 60

    config = PolicyConfig(hidden_dim=64, hidden_layers=2)  # Small for testing
    policy = MLPPolicy(obs_dim, act_dim, config)

    with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
        path = f.name

    export_policy(policy, path)

    with open(path, "rb") as f:
        magic, = struct.unpack("<I", f.read(4))
        assert magic == MAGIC, f"Magic mismatch: 0x{magic:08X}"

        num_layers, = struct.unpack("<I", f.read(4))
        expected_layers = list(policy.get_layer_params())
        assert num_layers == len(expected_layers)

        for i, (expected_w, expected_b, exp_in, exp_out) in enumerate(expected_layers):
            in_dim, = struct.unpack("<I", f.read(4))
            out_dim, = struct.unpack("<I", f.read(4))

            assert in_dim == exp_in, f"Layer {i}: in_dim {in_dim} vs {exp_in}"
            assert out_dim == exp_out, f"Layer {i}: out_dim {out_dim} vs {exp_out}"

            weights = np.frombuffer(f.read(out_dim * in_dim * 4), dtype=np.float32)
            weights = weights.reshape(out_dim, in_dim)
            biases = np.frombuffer(f.read(out_dim * 4), dtype=np.float32)

            np.testing.assert_array_almost_equal(
                weights, expected_w, decimal=6,
                err_msg=f"Layer {i} weights mismatch"
            )
            np.testing.assert_array_almost_equal(
                biases, expected_b, decimal=6,
                err_msg=f"Layer {i} biases mismatch"
            )

        remaining = f.read()
        assert len(remaining) == 0

    print("Round-trip test PASSED")
    verify_weights(path)
    os.unlink(path)


def test_forward_pass_matches():
    """Verify PyTorch forward pass matches manual matmul + ELU (simulating C++)."""
    obs_dim = 429
    act_dim = 60

    config = PolicyConfig(hidden_dim=64, hidden_layers=2)
    policy = MLPPolicy(obs_dim, act_dim, config)
    policy.eval()

    torch.manual_seed(123)
    obs = torch.randn(1, obs_dim)

    with torch.no_grad():
        pytorch_output = policy(obs).numpy().flatten()

    # Manual forward pass
    layers = list(policy.get_layer_params())
    x = obs.numpy().flatten()

    for i, (w, b, in_dim, out_dim) in enumerate(layers):
        x = w @ x + b
        if i < len(layers) - 1:
            x = np.where(x > 0, x, np.exp(x) - 1.0)  # ELU

    np.testing.assert_array_almost_equal(
        x, pytorch_output, decimal=4,
        err_msg="Forward pass mismatch"
    )
    print("Forward pass equivalence test PASSED")
    print(f"  Output sample: [{', '.join(f'{v:.4f}' for v in pytorch_output[:5])}...]")


def test_state_encoder_dims():
    """Verify observation dimension calculations match C++."""
    # 20 joints, tau=1: (1+1)*(11+200) + 1*7 = 429
    enc = StateEncoder(20, 1)
    assert enc.observation_dim == 429, f"Expected 429, got {enc.observation_dim}"

    # 20 joints, tau=3: (1+3)*211 + 3*7 = 865
    enc3 = StateEncoder(20, 3)
    assert enc3.observation_dim == 865, f"Expected 865, got {enc3.observation_dim}"

    print("State encoder dimension test PASSED")
    print(f"  20 joints, tau=1: obs_dim={enc.observation_dim}")
    print(f"  20 joints, tau=3: obs_dim={enc3.observation_dim}")


if __name__ == "__main__":
    test_state_encoder_dims()
    test_export_round_trip()
    test_forward_pass_matches()
    print("\nAll tests PASSED")
