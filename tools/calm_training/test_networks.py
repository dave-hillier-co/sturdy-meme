"""Tests for CALM network architectures."""

import torch
import pytest

from .config import (
    HumanoidCALMConfig,
    LLCPolicyConfig,
    AMPConfig,
    EncoderConfig,
    HLCTaskConfig,
)
from .networks import (
    StyleConditionedPolicy,
    AMPDiscriminator,
    MotionEncoder,
    HLCPolicy,
)


@pytest.fixture
def humanoid():
    return HumanoidCALMConfig()


@pytest.fixture
def batch_size():
    return 8


class TestStyleConditionedPolicy:
    def test_output_shape(self, humanoid, batch_size):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        latent = torch.randn(batch_size, humanoid.latent_dim)
        obs = torch.randn(batch_size, humanoid.policy_obs_dim)
        actions = policy(latent, obs)

        assert actions.shape == (batch_size, humanoid.num_dof)

    def test_distribution(self, humanoid, batch_size):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        latent = torch.randn(batch_size, humanoid.latent_dim)
        obs = torch.randn(batch_size, humanoid.policy_obs_dim)
        dist = policy.get_distribution(latent, obs)

        sample = dist.sample()
        assert sample.shape == (batch_size, humanoid.num_dof)

    def test_evaluate_actions(self, humanoid, batch_size):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        latent = torch.randn(batch_size, humanoid.latent_dim)
        obs = torch.randn(batch_size, humanoid.policy_obs_dim)
        actions = torch.randn(batch_size, humanoid.num_dof)

        log_prob, entropy = policy.evaluate_actions(latent, obs, actions)
        assert log_prob.shape == (batch_size,)
        assert entropy.shape == (batch_size,)

    def test_style_layer_extraction(self, humanoid):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        layers = list(policy.get_style_layers())
        # Style MLP: 64 -> 256(tanh) -> 128(tanh) -> 64(tanh)
        assert len(layers) == 3
        w0, b0, in0, out0 = layers[0]
        assert in0 == 64 and out0 == 256
        w1, b1, in1, out1 = layers[1]
        assert in1 == 256 and out1 == 128
        w2, b2, in2, out2 = layers[2]
        assert in2 == 128 and out2 == 64

    def test_main_layer_extraction(self, humanoid):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        layers = list(policy.get_main_layers())
        # Main MLP: (64+204)=268 -> 1024(relu) -> 512(relu)
        assert len(layers) == 2
        _, _, in0, out0 = layers[0]
        assert in0 == 268 and out0 == 1024
        _, _, in1, out1 = layers[1]
        assert in1 == 1024 and out1 == 512

    def test_mu_head_layer_extraction(self, humanoid):
        config = LLCPolicyConfig()
        policy = StyleConditionedPolicy(config, humanoid)

        layers = list(policy.get_mu_head_layers())
        # Mu head: 512 -> 37 (linear)
        assert len(layers) == 1
        _, _, in0, out0 = layers[0]
        assert in0 == 512 and out0 == 37


class TestAMPDiscriminator:
    def test_output_shape(self, humanoid, batch_size):
        config = AMPConfig()
        disc = AMPDiscriminator(config, humanoid)

        obs_t = torch.randn(batch_size, humanoid.per_frame_obs_dim)
        obs_t1 = torch.randn(batch_size, humanoid.per_frame_obs_dim)

        logit = disc(obs_t, obs_t1)
        assert logit.shape == (batch_size,)

    def test_reward_range(self, humanoid, batch_size):
        config = AMPConfig()
        disc = AMPDiscriminator(config, humanoid)

        obs_t = torch.randn(batch_size, humanoid.per_frame_obs_dim)
        obs_t1 = torch.randn(batch_size, humanoid.per_frame_obs_dim)

        reward = disc.compute_reward(obs_t, obs_t1)
        assert reward.shape == (batch_size,)
        assert (reward >= 0.0).all()
        assert (reward <= 1.0).all()


class TestMotionEncoder:
    def test_output_shape(self, humanoid, batch_size):
        config = EncoderConfig()
        encoder = MotionEncoder(config, humanoid)

        stacked_obs = torch.randn(batch_size, humanoid.encoder_obs_dim)
        latent = encoder(stacked_obs)

        assert latent.shape == (batch_size, humanoid.latent_dim)

    def test_l2_normalization(self, humanoid, batch_size):
        config = EncoderConfig()
        encoder = MotionEncoder(config, humanoid)

        stacked_obs = torch.randn(batch_size, humanoid.encoder_obs_dim)
        latent = encoder(stacked_obs)

        norms = torch.norm(latent, dim=-1)
        torch.testing.assert_close(norms, torch.ones(batch_size), atol=1e-5, rtol=0)

    def test_layer_extraction(self, humanoid):
        config = EncoderConfig()
        encoder = MotionEncoder(config, humanoid)

        layers = list(encoder.get_layers())
        # Encoder: 1020 -> 1024(relu) -> 512(relu) -> 64(none)
        assert len(layers) == 3
        _, _, in0, out0 = layers[0]
        assert in0 == 1020 and out0 == 1024
        _, _, in1, out1 = layers[1]
        assert in1 == 1024 and out1 == 512
        _, _, in2, out2 = layers[2]
        assert in2 == 512 and out2 == 64


class TestHLCPolicy:
    @pytest.mark.parametrize("task_obs_dim", [2, 3, 6])
    def test_output_shape(self, humanoid, batch_size, task_obs_dim):
        config = HLCTaskConfig(task_obs_dim=task_obs_dim)
        hlc = HLCPolicy(config, humanoid)

        task_obs = torch.randn(batch_size, task_obs_dim)
        latent = hlc(task_obs)

        assert latent.shape == (batch_size, humanoid.latent_dim)

    @pytest.mark.parametrize("task_obs_dim", [2, 3, 6])
    def test_l2_normalization(self, humanoid, batch_size, task_obs_dim):
        config = HLCTaskConfig(task_obs_dim=task_obs_dim)
        hlc = HLCPolicy(config, humanoid)

        task_obs = torch.randn(batch_size, task_obs_dim)
        latent = hlc(task_obs)

        norms = torch.norm(latent, dim=-1)
        torch.testing.assert_close(norms, torch.ones(batch_size), atol=1e-5, rtol=0)

    def test_evaluate_actions(self, humanoid, batch_size):
        config = HLCTaskConfig(task_obs_dim=2)
        hlc = HLCPolicy(config, humanoid)

        task_obs = torch.randn(batch_size, 2)
        latents = torch.randn(batch_size, humanoid.latent_dim)

        log_prob, entropy = hlc.evaluate_actions(task_obs, latents)
        assert log_prob.shape == (batch_size,)
        assert entropy.shape == (batch_size,)

    def test_layer_extraction(self, humanoid):
        config = HLCTaskConfig(task_obs_dim=2)
        hlc = HLCPolicy(config, humanoid)

        layers = list(hlc.get_layers())
        # HLC heading: 2 -> 512(relu) -> 256(relu) -> 64(none)
        assert len(layers) == 3
        _, _, in0, out0 = layers[0]
        assert in0 == 2 and out0 == 512
