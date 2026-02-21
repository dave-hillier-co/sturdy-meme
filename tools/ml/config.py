"""General-purpose configuration for MLP policies and PPO training."""

from dataclasses import dataclass


@dataclass
class PolicyConfig:
    """MLP architecture configuration."""
    hidden_layers: int = 3
    hidden_dim: int = 1024
    activation: str = "elu"


@dataclass
class PPOConfig:
    """PPO hyperparameters."""
    num_envs: int = 4096
    samples_per_env: int = 64
    num_epochs: int = 5
    minibatch_size: int = 512
    learning_rate: float = 3e-4
    gamma: float = 0.99
    gae_lambda: float = 0.95
    clip_epsilon: float = 0.2
    value_loss_coeff: float = 0.5
    entropy_coeff: float = 0.01
    max_grad_norm: float = 0.5
    kl_target: float = 0.01
    num_iterations: int = 10000
    checkpoint_interval: int = 100
    log_interval: int = 10
