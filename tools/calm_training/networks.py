"""CALM neural network architectures.

Four network types matching the C++ runtime:
  - StyleConditionedPolicy: LLC policy (style MLP + main MLP + mu head)
  - AMPDiscriminator: adversarial motion prior discriminator
  - MotionEncoder: maps stacked observations to latent space
  - HLCPolicy: task-specific high-level controller
"""

import torch
import torch.nn as nn
from typing import List

from .config import (
    HumanoidCALMConfig,
    LLCPolicyConfig,
    AMPConfig,
    EncoderConfig,
    HLCTaskConfig,
)


def _build_mlp(
    input_dim: int,
    hidden_sizes: List[int],
    output_dim: int,
    activation: str,
    output_activation: str = "none",
) -> nn.Sequential:
    """Build an MLP with specified architecture."""
    act_cls = {"relu": nn.ReLU, "tanh": nn.Tanh, "elu": nn.ELU}

    layers = []
    prev = input_dim
    for h in hidden_sizes:
        layers.append(nn.Linear(prev, h))
        if activation in act_cls:
            layers.append(act_cls[activation]())
        prev = h
    layers.append(nn.Linear(prev, output_dim))
    if output_activation in act_cls:
        layers.append(act_cls[output_activation]())

    seq = nn.Sequential(*layers)
    _init_weights(seq)
    return seq


def _init_weights(module: nn.Module):
    """Xavier initialization for all linear layers."""
    for m in module.modules():
        if isinstance(m, nn.Linear):
            nn.init.xavier_uniform_(m.weight)
            nn.init.zeros_(m.bias)


class StyleConditionedPolicy(nn.Module):
    """Low-Level Controller (LLC) policy.

    Matches C++ LowLevelController architecture:
      1. style_mlp: latent z -> style embedding (tanh activations)
      2. main_mlp: concat(style_embed, obs) -> hidden (relu activations)
      3. mu_head: hidden -> action mean (linear)
      4. Learnable log_std per action dimension

    The style MLP, main MLP, and mu head are exported as separate .bin files.
    """

    def __init__(
        self,
        config: LLCPolicyConfig,
        humanoid: HumanoidCALMConfig,
    ):
        super().__init__()
        self.config = config
        self.humanoid = humanoid

        policy_obs_dim = humanoid.policy_obs_dim  # 204
        action_dim = humanoid.num_dof  # 37

        # Style MLP: latent -> style embedding
        self.style_mlp = _build_mlp(
            input_dim=config.style.input_dim,
            hidden_sizes=config.style.hidden_sizes,
            output_dim=config.style.output_dim,
            activation=config.style.activation,
            output_activation=config.style.activation,
        )

        # Main MLP: concat(style_embed, obs) -> hidden
        main_input_dim = config.style.output_dim + policy_obs_dim
        self.main_mlp = _build_mlp(
            input_dim=main_input_dim,
            hidden_sizes=config.main.hidden_sizes,
            output_dim=config.main.hidden_sizes[-1],
            activation=config.main.activation,
        )
        # Override: main_mlp output layer should also have activation
        # Actually the main_mlp ends at the last hidden size, no extra output layer needed
        # Rebuild: hidden layers only, the last hidden IS the output
        main_layers = []
        prev = main_input_dim
        act_cls = {"relu": nn.ReLU, "tanh": nn.Tanh}
        for h in config.main.hidden_sizes:
            main_layers.append(nn.Linear(prev, h))
            main_layers.append(act_cls[config.main.activation]())
            prev = h
        self.main_mlp = nn.Sequential(*main_layers)
        _init_weights(self.main_mlp)

        # Mu head: hidden -> actions (linear, no activation)
        self.mu_head = nn.Sequential(
            nn.Linear(config.main.hidden_sizes[-1], action_dim),
        )
        _init_weights(self.mu_head)

        # Learnable exploration noise
        self.log_std = nn.Parameter(
            torch.full((action_dim,), config.log_std_init)
        )

    def forward(self, latent: torch.Tensor, obs: torch.Tensor) -> torch.Tensor:
        """Deterministic forward pass (mean action).

        Args:
            latent: (batch, latent_dim) latent code z
            obs: (batch, policy_obs_dim) stacked observations
        """
        style_embed = self.style_mlp(latent)
        combined = torch.cat([style_embed, obs], dim=-1)
        hidden = self.main_mlp(combined)
        return self.mu_head(hidden)

    def get_distribution(self, latent: torch.Tensor, obs: torch.Tensor):
        """Return Gaussian action distribution for sampling."""
        mean = self.forward(latent, obs)
        std = self.log_std.exp().expand_as(mean)
        return torch.distributions.Normal(mean, std)

    def evaluate_actions(
        self,
        latent: torch.Tensor,
        obs: torch.Tensor,
        actions: torch.Tensor,
    ):
        """Compute log_prob and entropy for state-action pairs.

        Used by PPO update. Returns (log_prob, entropy), both (batch,).
        """
        dist = self.get_distribution(latent, obs)
        log_prob = dist.log_prob(actions).sum(dim=-1)
        entropy = dist.entropy().sum(dim=-1)
        return log_prob, entropy

    def get_style_layers(self):
        """Yield (weight, bias, in_dim, out_dim) for style MLP linear layers."""
        for m in self.style_mlp:
            if isinstance(m, nn.Linear):
                yield (
                    m.weight.detach().cpu().numpy(),
                    m.bias.detach().cpu().numpy(),
                    m.in_features,
                    m.out_features,
                )

    def get_main_layers(self):
        """Yield (weight, bias, in_dim, out_dim) for main MLP linear layers."""
        for m in self.main_mlp:
            if isinstance(m, nn.Linear):
                yield (
                    m.weight.detach().cpu().numpy(),
                    m.bias.detach().cpu().numpy(),
                    m.in_features,
                    m.out_features,
                )

    def get_mu_head_layers(self):
        """Yield (weight, bias, in_dim, out_dim) for mu head linear layers."""
        for m in self.mu_head:
            if isinstance(m, nn.Linear):
                yield (
                    m.weight.detach().cpu().numpy(),
                    m.bias.detach().cpu().numpy(),
                    m.in_features,
                    m.out_features,
                )


class AMPDiscriminator(nn.Module):
    """Adversarial Motion Prior discriminator.

    Input: concat(obs_t, obs_t1) = 2 * per_frame_obs_dim = 204
    Output: scalar logit (real vs fake).

    AMP reward: clamp(1 - 0.25 * (D(s,s') - 1)^2, 0, 1)
    """

    def __init__(self, config: AMPConfig, humanoid: HumanoidCALMConfig):
        super().__init__()
        input_dim = 2 * humanoid.per_frame_obs_dim  # 204
        self.network = _build_mlp(
            input_dim=input_dim,
            hidden_sizes=config.hidden_sizes,
            output_dim=1,
            activation=config.activation,
        )

    def forward(self, obs_t: torch.Tensor, obs_t1: torch.Tensor) -> torch.Tensor:
        """Compute discriminator logit.

        Args:
            obs_t: (batch, per_frame_obs_dim) current frame
            obs_t1: (batch, per_frame_obs_dim) next frame

        Returns:
            (batch,) logit values
        """
        x = torch.cat([obs_t, obs_t1], dim=-1)
        return self.network(x).squeeze(-1)

    def compute_reward(self, obs_t: torch.Tensor, obs_t1: torch.Tensor) -> torch.Tensor:
        """Compute AMP-style reward from discriminator output.

        reward = clamp(1 - 0.25 * (D(s,s') - 1)^2, 0, 1)
        """
        with torch.no_grad():
            logit = self.forward(obs_t, obs_t1)
            reward = 1.0 - 0.25 * (logit - 1.0) ** 2
            return reward.clamp(0.0, 1.0)


class MotionEncoder(nn.Module):
    """Motion encoder: stacked observations -> L2-normalized latent vector.

    Input: num_encoder_obs_steps * per_frame_obs_dim = 1020
    Output: latent_dim = 64 (L2 normalized)
    """

    def __init__(self, config: EncoderConfig, humanoid: HumanoidCALMConfig):
        super().__init__()
        input_dim = humanoid.encoder_obs_dim  # 1020
        self.network = _build_mlp(
            input_dim=input_dim,
            hidden_sizes=config.hidden_sizes,
            output_dim=config.output_dim,
            activation=config.activation,
        )
        self.normalize = config.normalize_output

    def forward(self, stacked_obs: torch.Tensor) -> torch.Tensor:
        """Encode stacked observations to latent vector.

        Args:
            stacked_obs: (batch, encoder_obs_dim)

        Returns:
            (batch, latent_dim) L2-normalized latent vectors
        """
        z = self.network(stacked_obs)
        if self.normalize:
            z = torch.nn.functional.normalize(z, dim=-1)
        return z

    def get_layers(self):
        """Yield (weight, bias, in_dim, out_dim) for all linear layers."""
        for m in self.network:
            if isinstance(m, nn.Linear):
                yield (
                    m.weight.detach().cpu().numpy(),
                    m.bias.detach().cpu().numpy(),
                    m.in_features,
                    m.out_features,
                )


class HLCPolicy(nn.Module):
    """High-Level Controller policy.

    Maps task-specific observations to L2-normalized latent commands.

    Tasks:
      - heading: task_obs_dim=2 (sin, cos of target angle)
      - location: task_obs_dim=3 (relative target position)
      - strike: task_obs_dim=6 (target pos + hand pos)
    """

    def __init__(self, task_config: HLCTaskConfig, humanoid: HumanoidCALMConfig):
        super().__init__()
        self.latent_dim = humanoid.latent_dim

        self.network = _build_mlp(
            input_dim=task_config.task_obs_dim,
            hidden_sizes=task_config.hidden_sizes,
            output_dim=humanoid.latent_dim,
            activation="relu",
        )

        # Learnable exploration noise in latent space
        self.log_std = nn.Parameter(
            torch.full((humanoid.latent_dim,), -1.0)
        )

    def forward(self, task_obs: torch.Tensor) -> torch.Tensor:
        """Deterministic forward: task obs -> L2-normalized latent.

        Args:
            task_obs: (batch, task_obs_dim)

        Returns:
            (batch, latent_dim) L2-normalized latent commands
        """
        z = self.network(task_obs)
        return torch.nn.functional.normalize(z, dim=-1)

    def get_distribution(self, task_obs: torch.Tensor):
        """Return Gaussian distribution in latent space."""
        mean = self.forward(task_obs)
        std = self.log_std.exp().expand_as(mean)
        return torch.distributions.Normal(mean, std)

    def evaluate_actions(
        self,
        task_obs: torch.Tensor,
        latents: torch.Tensor,
    ):
        """Compute log_prob and entropy for given task obs and latent actions."""
        dist = self.get_distribution(task_obs)
        log_prob = dist.log_prob(latents).sum(dim=-1)
        entropy = dist.entropy().sum(dim=-1)
        return log_prob, entropy

    def get_layers(self):
        """Yield (weight, bias, in_dim, out_dim) for all linear layers."""
        for m in self.network:
            if isinstance(m, nn.Linear):
                yield (
                    m.weight.detach().cpu().numpy(),
                    m.bias.detach().cpu().numpy(),
                    m.in_features,
                    m.out_features,
                )
