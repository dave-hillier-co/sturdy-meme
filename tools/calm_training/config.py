"""CALM training configuration.

Dataclass composition mirroring training/config/humanoid_calm.yaml.
Reuses PPOConfig from tools.ml.config for PPO hyperparameters.
"""

from dataclasses import dataclass, field
from typing import List

from tools.ml.config import PPOConfig


@dataclass
class HumanoidCALMConfig:
    """Humanoid skeleton and observation layout."""
    num_dof: int = 37
    num_key_bodies: int = 5
    per_frame_obs_dim: int = 102
    num_policy_obs_steps: int = 2
    num_encoder_obs_steps: int = 10
    latent_dim: int = 64

    # PD controller gains (for position-controlled actuators)
    pd_kp: float = 40.0
    pd_kd: float = 5.0

    @property
    def policy_obs_dim(self) -> int:
        return self.per_frame_obs_dim * self.num_policy_obs_steps

    @property
    def encoder_obs_dim(self) -> int:
        return self.per_frame_obs_dim * self.num_encoder_obs_steps


@dataclass
class StyleMLPConfig:
    """Style MLP: latent z -> style embedding."""
    input_dim: int = 64
    hidden_sizes: List[int] = field(default_factory=lambda: [256, 128])
    output_dim: int = 64
    activation: str = "tanh"


@dataclass
class MainMLPConfig:
    """Main policy MLP: concat(styleEmbed, obs) -> hidden."""
    hidden_sizes: List[int] = field(default_factory=lambda: [1024, 512])
    activation: str = "relu"


@dataclass
class MuHeadConfig:
    """Action head: hidden -> joint targets."""
    activation: str = "none"


@dataclass
class LLCPolicyConfig:
    """Full LLC (Low-Level Controller) policy architecture."""
    style: StyleMLPConfig = field(default_factory=StyleMLPConfig)
    main: MainMLPConfig = field(default_factory=MainMLPConfig)
    mu_head: MuHeadConfig = field(default_factory=MuHeadConfig)
    log_std_init: float = -1.0


@dataclass
class ValueConfig:
    """Value network architecture."""
    hidden_sizes: List[int] = field(default_factory=lambda: [1024, 512, 256])
    activation: str = "relu"


@dataclass
class AMPConfig:
    """AMP discriminator and training parameters."""
    hidden_sizes: List[int] = field(default_factory=lambda: [1024, 512])
    activation: str = "relu"
    learning_rate: float = 1e-4
    grad_penalty_weight: float = 10.0
    style_reward_weight: float = 0.5
    task_reward_weight: float = 0.5


@dataclass
class EncoderConfig:
    """Motion encoder architecture."""
    hidden_sizes: List[int] = field(default_factory=lambda: [1024, 512])
    output_dim: int = 64
    activation: str = "relu"
    normalize_output: bool = True


@dataclass
class EncoderTrainingConfig:
    """Contrastive encoder training parameters."""
    learning_rate: float = 1e-4
    contrastive_margin: float = 1.0
    positive_window: int = 30
    negative_clips: int = 4
    num_iterations: int = 5000
    batch_size: int = 256
    log_interval: int = 10
    checkpoint_interval: int = 50


@dataclass
class HLCTaskConfig:
    """HLC architecture for a single task."""
    hidden_sizes: List[int] = field(default_factory=lambda: [512, 256])
    learning_rate: float = 3e-4
    task_obs_dim: int = 2


@dataclass
class HLCConfig:
    """All HLC task configurations."""
    heading: HLCTaskConfig = field(
        default_factory=lambda: HLCTaskConfig(task_obs_dim=2)
    )
    location: HLCTaskConfig = field(
        default_factory=lambda: HLCTaskConfig(task_obs_dim=3)
    )
    strike: HLCTaskConfig = field(
        default_factory=lambda: HLCTaskConfig(task_obs_dim=6)
    )


@dataclass
class EnvironmentConfig:
    """Environment parameters."""
    num_envs: int = 4096
    sim_timestep: float = 1.0 / 60.0
    sim_substeps: int = 2
    early_termination_height: float = 0.3
    max_episode_steps: int = 300


@dataclass
class CALMConfig:
    """Top-level CALM training configuration."""
    humanoid: HumanoidCALMConfig = field(default_factory=HumanoidCALMConfig)
    llc_policy: LLCPolicyConfig = field(default_factory=LLCPolicyConfig)
    value: ValueConfig = field(default_factory=ValueConfig)
    amp: AMPConfig = field(default_factory=AMPConfig)
    encoder: EncoderConfig = field(default_factory=EncoderConfig)
    encoder_training: EncoderTrainingConfig = field(default_factory=EncoderTrainingConfig)
    hlc: HLCConfig = field(default_factory=HLCConfig)
    env: EnvironmentConfig = field(default_factory=EnvironmentConfig)
    ppo: PPOConfig = field(default_factory=lambda: PPOConfig(
        num_envs=4096,
        samples_per_env=32,
        num_epochs=5,
        minibatch_size=512,
        learning_rate=3e-4,
        gamma=0.99,
        gae_lambda=0.95,
        clip_epsilon=0.2,
        value_loss_coeff=0.5,
        entropy_coeff=0.01,
        max_grad_norm=1.0,
    ))

    # Training schedule
    llc_epochs: int = 5000
    hlc_epochs: int = 2000
    checkpoint_interval: int = 50
    log_interval: int = 10

    # Paths
    motion_dir: str = "data/calm/motions"
    output_dir: str = "checkpoints/calm"

    # Runtime
    device: str = "auto"
    seed: int = 42
    parallel: bool = False
    num_workers: int = 0
    max_envs: int = 0
