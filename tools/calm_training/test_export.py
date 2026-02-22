"""Tests for CALM export pipeline.

Verifies round-trip: export to .bin, load with calm_encode_library tools,
run numpy forward pass, compare to PyTorch output.
"""

import tempfile
from pathlib import Path

import numpy as np
import torch
import pytest

from .config import HumanoidCALMConfig, LLCPolicyConfig, EncoderConfig, HLCTaskConfig
from .networks import StyleConditionedPolicy, MotionEncoder, HLCPolicy
from .export import export_llc, export_encoder, export_hlc

# Reuse the numpy-based loader from calm_encode_library
from tools.calm_encode_library import load_mlp_bin, forward_mlp_numpy


@pytest.fixture
def humanoid():
    return HumanoidCALMConfig()


class TestExportLLC:
    def test_export_creates_files(self, humanoid, tmp_path):
        policy = StyleConditionedPolicy(LLCPolicyConfig(), humanoid)
        export_llc(policy, str(tmp_path))

        assert (tmp_path / "llc_style.bin").exists()
        assert (tmp_path / "llc_main.bin").exists()
        assert (tmp_path / "llc_mu_head.bin").exists()

    def test_style_roundtrip(self, humanoid, tmp_path):
        """Exported style MLP matches PyTorch forward pass."""
        policy = StyleConditionedPolicy(LLCPolicyConfig(), humanoid)
        export_llc(policy, str(tmp_path))

        layers = load_mlp_bin(tmp_path / "llc_style.bin")
        x_np = np.random.randn(humanoid.latent_dim).astype(np.float32)
        x_torch = torch.tensor(x_np).unsqueeze(0)

        np_out = forward_mlp_numpy(layers, x_np)

        with torch.no_grad():
            torch_out = policy.style_mlp(x_torch).squeeze(0).numpy()

        np.testing.assert_allclose(np_out, torch_out, atol=1e-5)

    def test_main_roundtrip(self, humanoid, tmp_path):
        """Exported main MLP matches PyTorch forward pass."""
        policy = StyleConditionedPolicy(LLCPolicyConfig(), humanoid)
        export_llc(policy, str(tmp_path))

        layers = load_mlp_bin(tmp_path / "llc_main.bin")
        # Input to main = concat(style_output=64, obs=204) = 268
        x_np = np.random.randn(268).astype(np.float32)
        x_torch = torch.tensor(x_np).unsqueeze(0)

        np_out = forward_mlp_numpy(layers, x_np)

        with torch.no_grad():
            torch_out = policy.main_mlp(x_torch).squeeze(0).numpy()

        np.testing.assert_allclose(np_out, torch_out, atol=1e-5)

    def test_mu_head_roundtrip(self, humanoid, tmp_path):
        """Exported mu head matches PyTorch forward pass."""
        policy = StyleConditionedPolicy(LLCPolicyConfig(), humanoid)
        export_llc(policy, str(tmp_path))

        layers = load_mlp_bin(tmp_path / "llc_mu_head.bin")
        x_np = np.random.randn(512).astype(np.float32)
        x_torch = torch.tensor(x_np).unsqueeze(0)

        np_out = forward_mlp_numpy(layers, x_np)

        with torch.no_grad():
            torch_out = policy.mu_head(x_torch).squeeze(0).numpy()

        np.testing.assert_allclose(np_out, torch_out, atol=1e-5)

    def test_full_pipeline_roundtrip(self, humanoid, tmp_path):
        """Full LLC forward pass: style -> main -> mu_head matches."""
        policy = StyleConditionedPolicy(LLCPolicyConfig(), humanoid)
        export_llc(policy, str(tmp_path))

        style_layers = load_mlp_bin(tmp_path / "llc_style.bin")
        main_layers = load_mlp_bin(tmp_path / "llc_main.bin")
        mu_layers = load_mlp_bin(tmp_path / "llc_mu_head.bin")

        latent_np = np.random.randn(humanoid.latent_dim).astype(np.float32)
        obs_np = np.random.randn(humanoid.policy_obs_dim).astype(np.float32)

        # Numpy pipeline
        style_out = forward_mlp_numpy(style_layers, latent_np)
        combined = np.concatenate([style_out, obs_np])
        main_out = forward_mlp_numpy(main_layers, combined)
        actions_np = forward_mlp_numpy(mu_layers, main_out)

        # PyTorch pipeline
        latent_torch = torch.tensor(latent_np).unsqueeze(0)
        obs_torch = torch.tensor(obs_np).unsqueeze(0)
        with torch.no_grad():
            actions_torch = policy(latent_torch, obs_torch).squeeze(0).numpy()

        np.testing.assert_allclose(actions_np, actions_torch, atol=1e-4)


class TestExportEncoder:
    def test_export_creates_file(self, humanoid, tmp_path):
        encoder = MotionEncoder(EncoderConfig(), humanoid)
        export_encoder(encoder, str(tmp_path))
        assert (tmp_path / "encoder.bin").exists()

    def test_roundtrip(self, humanoid, tmp_path):
        """Exported encoder matches PyTorch forward pass (pre-normalization)."""
        config = EncoderConfig(normalize_output=False)
        encoder = MotionEncoder(config, humanoid)
        export_encoder(encoder, str(tmp_path))

        layers = load_mlp_bin(tmp_path / "encoder.bin")
        x_np = np.random.randn(humanoid.encoder_obs_dim).astype(np.float32)
        x_torch = torch.tensor(x_np).unsqueeze(0)

        np_out = forward_mlp_numpy(layers, x_np)

        with torch.no_grad():
            torch_out = encoder(x_torch).squeeze(0).numpy()

        np.testing.assert_allclose(np_out, torch_out, atol=1e-4)


class TestExportHLC:
    @pytest.mark.parametrize("task,obs_dim", [
        ("heading", 2), ("location", 3), ("strike", 6),
    ])
    def test_export_creates_file(self, humanoid, tmp_path, task, obs_dim):
        config = HLCTaskConfig(task_obs_dim=obs_dim)
        hlc = HLCPolicy(config, humanoid)
        export_hlc(hlc, task, str(tmp_path))
        assert (tmp_path / f"hlc_{task}.bin").exists()

    def test_roundtrip(self, humanoid, tmp_path):
        """Exported HLC matches PyTorch forward pass (pre-normalization)."""
        config = HLCTaskConfig(task_obs_dim=2)
        hlc = HLCPolicy(config, humanoid)
        export_hlc(hlc, "heading", str(tmp_path))

        layers = load_mlp_bin(tmp_path / "hlc_heading.bin")
        x_np = np.random.randn(2).astype(np.float32)
        x_torch = torch.tensor(x_np).unsqueeze(0)

        np_out = forward_mlp_numpy(layers, x_np)

        # PyTorch network output (pre-normalization)
        with torch.no_grad():
            torch_out = hlc.network(x_torch).squeeze(0).numpy()

        np.testing.assert_allclose(np_out, torch_out, atol=1e-5)
