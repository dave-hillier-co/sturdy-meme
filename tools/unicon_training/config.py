"""UniCon-specific training configuration.

Humanoid body specification, reward weights, and RSIS parameters
from the UniCon paper. General-purpose ML config lives in tools.ml.config.
"""

from dataclasses import dataclass, field
from typing import List

from tools.ml.config import PolicyConfig, PPOConfig


@dataclass
class HumanoidConfig:
    """20-body humanoid matching the C++ ArticulatedBody."""
    num_bodies: int = 20
    total_mass_kg: float = 70.0
    height_m: float = 1.8
    num_dof: int = 35

    # Per-joint torque effort factors (50-600 range from paper).
    # Order matches ArticulatedBody part indices.
    effort_factors: List[float] = field(default_factory=lambda: [
        200.0,  # pelvis
        300.0,  # lower_spine
        300.0,  # upper_spine
        200.0,  # chest
        50.0,   # head
        150.0,  # l_upper_arm
        100.0,  # l_forearm
        50.0,   # l_hand
        150.0,  # r_upper_arm
        100.0,  # r_forearm
        50.0,   # r_hand
        600.0,  # l_thigh
        400.0,  # l_shin
        200.0,  # l_foot
        600.0,  # r_thigh
        400.0,  # r_shin
        200.0,  # r_foot
        150.0,  # l_shoulder
        150.0,  # r_shoulder
        100.0,  # neck
    ])


@dataclass
class RewardConfig:
    """UniCon paper reward weights and kernel scales."""
    w_root_pos: float = 0.2
    w_root_rot: float = 0.2
    w_joint_pos: float = 0.1
    w_joint_rot: float = 0.4
    w_joint_ang_vel: float = 0.1

    k_root_pos: float = 10.0
    k_root_rot: float = 5.0
    k_joint_pos: float = 10.0
    k_joint_rot: float = 2.0
    k_joint_ang_vel: float = 0.1

    # Constrained multi-objective: terminate if any reward term < alpha
    alpha: float = 0.1


@dataclass
class RSISConfig:
    """Reactive State Initialization Scheme from the paper."""
    min_offset_frames: int = 5
    max_offset_frames: int = 10
    position_noise_std: float = 0.02
    rotation_noise_std: float = 0.05
    velocity_noise_std: float = 0.1


@dataclass
class TrainingConfig:
    """Top-level config wiring UniCon-specific and general-purpose settings."""
    humanoid: HumanoidConfig = field(default_factory=HumanoidConfig)
    policy: PolicyConfig = field(default_factory=PolicyConfig)
    reward: RewardConfig = field(default_factory=RewardConfig)
    ppo: PPOConfig = field(default_factory=PPOConfig)
    rsis: RSISConfig = field(default_factory=RSISConfig)

    # Physics simulation
    physics_dt: float = 1.0 / 60.0
    physics_substeps: int = 4

    # Motion data
    motion_dir: str = "assets/motions"
    output_dir: str = "generated/unicon"

    # Policy variance annealing
    initial_log_std: float = -1.0
    final_log_std: float = -3.0
    log_std_anneal_iterations: int = 5000

    device: str = "cuda"
    seed: int = 42
