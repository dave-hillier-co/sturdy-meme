"""General-purpose MLP networks for reinforcement learning.

MLPPolicy: feedforward network mapping observations to actions.
ValueNetwork: feedforward network mapping observations to scalar value.

Both support configurable depth, width, and activation functions.
"""

import torch
import torch.nn as nn
from typing import Optional

from .config import PolicyConfig


class MLPPolicy(nn.Module):
    """MLP policy network.

    Architecture:
        input -> [Linear -> activation] x N -> Linear -> output

    Hidden layers use the configured activation; output is linear.
    A learnable per-action log_std parameter is used for stochastic sampling.
    """

    def __init__(self, input_dim: int, output_dim: int,
                 config: Optional[PolicyConfig] = None):
        super().__init__()
        if config is None:
            config = PolicyConfig()

        self.input_dim = input_dim
        self.output_dim = output_dim

        activation_cls = {"elu": nn.ELU, "relu": nn.ReLU, "tanh": nn.Tanh}
        act = activation_cls.get(config.activation, nn.ELU)

        layers = []
        prev_dim = input_dim
        for _ in range(config.hidden_layers):
            layers.append(nn.Linear(prev_dim, config.hidden_dim))
            layers.append(act())
            prev_dim = config.hidden_dim
        layers.append(nn.Linear(prev_dim, output_dim))

        self.network = nn.Sequential(*layers)
        self.log_std = nn.Parameter(torch.full((output_dim,), -1.0))

        self._init_weights()

    def _init_weights(self):
        """Xavier initialization."""
        for m in self.network:
            if isinstance(m, nn.Linear):
                nn.init.xavier_uniform_(m.weight)
                nn.init.zeros_(m.bias)

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        """Deterministic forward pass (mean action)."""
        return self.network(obs)

    def get_distribution(self, obs: torch.Tensor):
        """Return Gaussian action distribution for sampling."""
        mean = self.forward(obs)
        std = self.log_std.exp().expand_as(mean)
        return torch.distributions.Normal(mean, std)

    def evaluate_actions(self, obs: torch.Tensor, actions: torch.Tensor):
        """Compute log_prob and entropy for given state-action pairs."""
        dist = self.get_distribution(obs)
        log_prob = dist.log_prob(actions).sum(dim=-1)
        entropy = dist.entropy().sum(dim=-1)
        return log_prob, entropy

    def get_layer_params(self):
        """Yield (weight, bias, input_dim, output_dim) for each linear layer.
        Weight layout: [out_features, in_features] row-major."""
        for module in self.network:
            if isinstance(module, nn.Linear):
                yield (
                    module.weight.detach().cpu().numpy(),
                    module.bias.detach().cpu().numpy(),
                    module.in_features,
                    module.out_features,
                )


class ValueNetwork(nn.Module):
    """MLP value function that maps observations to a scalar."""

    def __init__(self, input_dim: int, config: Optional[PolicyConfig] = None):
        super().__init__()
        if config is None:
            config = PolicyConfig()

        activation_cls = {"elu": nn.ELU, "relu": nn.ReLU, "tanh": nn.Tanh}
        act = activation_cls.get(config.activation, nn.ELU)

        layers = []
        prev_dim = input_dim
        for _ in range(config.hidden_layers):
            layers.append(nn.Linear(prev_dim, config.hidden_dim))
            layers.append(act())
            prev_dim = config.hidden_dim
        layers.append(nn.Linear(prev_dim, 1))

        self.network = nn.Sequential(*layers)
        self._init_weights()

    def _init_weights(self):
        for m in self.network:
            if isinstance(m, nn.Linear):
                nn.init.xavier_uniform_(m.weight)
                nn.init.zeros_(m.bias)

    def forward(self, obs: torch.Tensor) -> torch.Tensor:
        return self.network(obs).squeeze(-1)
