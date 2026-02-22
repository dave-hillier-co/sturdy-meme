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
    num_dof: int = 30  # 30 unique hinge joints in MuJoCo model

    # Per-body torque effort factors (50-600 range from paper).
    # Order matches the 20-part ArticulatedBody / BODY_NAMES ordering.
    effort_factors: List[float] = field(default_factory=lambda: [
        200.0,  # 0: Pelvis (no actuators, placeholder)
        300.0,  # 1: LowerSpine
        300.0,  # 2: UpperSpine
        200.0,  # 3: Chest
        100.0,  # 4: Neck (mapped to head joints)
        50.0,   # 5: Head (duplicate, no unique actuators)
        150.0,  # 6: LeftShoulder
        150.0,  # 7: LeftUpperArm (duplicate, no unique actuators)
        100.0,  # 8: LeftForearm
        50.0,   # 9: LeftHand
        150.0,  # 10: RightShoulder
        150.0,  # 11: RightUpperArm (duplicate, no unique actuators)
        100.0,  # 12: RightForearm
        50.0,   # 13: RightHand
        600.0,  # 14: LeftThigh
        400.0,  # 15: LeftShin
        200.0,  # 16: LeftFoot
        600.0,  # 17: RightThigh
        400.0,  # 18: RightShin
        200.0,  # 19: RightFoot
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

    # Number of future target frames (tau from the UniCon paper)
    tau: int = 1

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

    device: str = "auto"
    seed: int = 42

    # Parallelization
    parallel: bool = False   # Enable multiprocessing vec env
    num_workers: int = 0     # 0 = auto-detect (cpu_count - 1)
    max_envs: int = 0        # 0 = no limit
