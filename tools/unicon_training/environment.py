"""MuJoCo-based training environment for UniCon.

Wraps a MuJoCo humanoid simulation with the UniCon observation/action space.
The environment closely matches the C++ Jolt-based runtime to minimize
sim-to-sim transfer gap.

Requires: mujoco >= 3.0
"""

import mujoco
import numpy as np
from pathlib import Path
from typing import Optional, Tuple, Dict, Any

from .config import TrainingConfig, HumanoidConfig, RewardConfig, RSISConfig
from .state_encoder import StateEncoder
from .reward import compute_reward
from tools.ml.quaternion import slerp as quat_slerp


# Default MuJoCo humanoid XML.
# Users can replace this with a custom model that better matches
# the ArticulatedBody 20-body humanoid.
_DEFAULT_HUMANOID_XML = """
<mujoco model="unicon_humanoid">
  <option timestep="0.004167" iterations="50" solver="Newton">
    <flag warmstart="enable"/>
  </option>

  <default>
    <joint damping="0.5" armature="0.01"/>
    <geom condim="3" friction="1 0.5 0.5" margin="0.001"/>
    <motor ctrllimited="true" ctrlrange="-1 1"/>
  </default>

  <worldbody>
    <light diffuse="0.8 0.8 0.8" pos="0 0 3" dir="0 0 -1"/>
    <geom type="plane" size="50 50 1" rgba="0.8 0.8 0.8 1"/>

    <body name="pelvis" pos="0 0 1.0">
      <freejoint name="root"/>
      <geom type="capsule" fromto="0 -0.07 0 0 0.07 0" size="0.08" mass="8.0"/>

      <body name="lower_spine" pos="0 0 0.1">
        <joint name="lower_spine_x" type="hinge" axis="1 0 0" range="-30 30"/>
        <joint name="lower_spine_y" type="hinge" axis="0 1 0" range="-30 30"/>
        <joint name="lower_spine_z" type="hinge" axis="0 0 1" range="-20 20"/>
        <geom type="capsule" fromto="0 0 0 0 0 0.12" size="0.07" mass="5.0"/>

        <body name="upper_spine" pos="0 0 0.12">
          <joint name="upper_spine_x" type="hinge" axis="1 0 0" range="-30 30"/>
          <joint name="upper_spine_y" type="hinge" axis="0 1 0" range="-20 20"/>
          <geom type="capsule" fromto="0 0 0 0 0 0.12" size="0.07" mass="5.0"/>

          <body name="chest" pos="0 0 0.12">
            <joint name="chest_x" type="hinge" axis="1 0 0" range="-15 15"/>
            <geom type="capsule" fromto="0 -0.1 0 0 0.1 0" size="0.09" mass="8.0"/>

            <body name="head" pos="0 0 0.15">
              <joint name="head_x" type="hinge" axis="1 0 0" range="-30 30"/>
              <joint name="head_y" type="hinge" axis="0 1 0" range="-45 45"/>
              <geom type="sphere" size="0.1" mass="4.0"/>
            </body>

            <body name="l_shoulder" pos="0 0.18 0.05">
              <joint name="l_shoulder_x" type="hinge" axis="1 0 0" range="-90 90"/>
              <joint name="l_shoulder_y" type="hinge" axis="0 1 0" range="-60 160"/>
              <joint name="l_shoulder_z" type="hinge" axis="0 0 1" range="-90 30"/>
              <geom type="capsule" fromto="0 0 0 0 0.25 0" size="0.04" mass="2.0"/>

              <body name="l_forearm" pos="0 0.25 0">
                <joint name="l_elbow" type="hinge" axis="1 0 0" range="0 150"/>
                <geom type="capsule" fromto="0 0 0 0 0.22 0" size="0.035" mass="1.5"/>

                <body name="l_hand" pos="0 0.22 0">
                  <joint name="l_wrist_x" type="hinge" axis="1 0 0" range="-45 45"/>
                  <geom type="box" size="0.04 0.06 0.02" mass="0.5"/>
                </body>
              </body>
            </body>

            <body name="r_shoulder" pos="0 -0.18 0.05">
              <joint name="r_shoulder_x" type="hinge" axis="1 0 0" range="-90 90"/>
              <joint name="r_shoulder_y" type="hinge" axis="0 1 0" range="-160 60"/>
              <joint name="r_shoulder_z" type="hinge" axis="0 0 1" range="-30 90"/>
              <geom type="capsule" fromto="0 0 0 0 -0.25 0" size="0.04" mass="2.0"/>

              <body name="r_forearm" pos="0 -0.25 0">
                <joint name="r_elbow" type="hinge" axis="1 0 0" range="-150 0"/>
                <geom type="capsule" fromto="0 0 0 0 -0.22 0" size="0.035" mass="1.5"/>

                <body name="r_hand" pos="0 -0.22 0">
                  <joint name="r_wrist_x" type="hinge" axis="1 0 0" range="-45 45"/>
                  <geom type="box" size="0.04 0.06 0.02" mass="0.5"/>
                </body>
              </body>
            </body>
          </body>
        </body>
      </body>

      <body name="l_thigh" pos="0 0.1 -0.05">
        <joint name="l_hip_x" type="hinge" axis="1 0 0" range="-30 120"/>
        <joint name="l_hip_y" type="hinge" axis="0 1 0" range="-45 45"/>
        <joint name="l_hip_z" type="hinge" axis="0 0 1" range="-30 30"/>
        <geom type="capsule" fromto="0 0 0 0 0 -0.4" size="0.06" mass="5.0"/>

        <body name="l_shin" pos="0 0 -0.4">
          <joint name="l_knee" type="hinge" axis="0 1 0" range="-150 0"/>
          <geom type="capsule" fromto="0 0 0 0 0 -0.38" size="0.05" mass="3.0"/>

          <body name="l_foot" pos="0 0 -0.38">
            <joint name="l_ankle_x" type="hinge" axis="1 0 0" range="-30 45"/>
            <joint name="l_ankle_y" type="hinge" axis="0 1 0" range="-20 20"/>
            <geom type="box" size="0.08 0.04 0.03" pos="0.03 0 0" mass="1.0"/>
          </body>
        </body>
      </body>

      <body name="r_thigh" pos="0 -0.1 -0.05">
        <joint name="r_hip_x" type="hinge" axis="1 0 0" range="-30 120"/>
        <joint name="r_hip_y" type="hinge" axis="0 1 0" range="-45 45"/>
        <joint name="r_hip_z" type="hinge" axis="0 0 1" range="-30 30"/>
        <geom type="capsule" fromto="0 0 0 0 0 -0.4" size="0.06" mass="5.0"/>

        <body name="r_shin" pos="0 0 -0.4">
          <joint name="r_knee" type="hinge" axis="0 1 0" range="0 150"/>
          <geom type="capsule" fromto="0 0 0 0 0 -0.38" size="0.05" mass="3.0"/>

          <body name="r_foot" pos="0 0 -0.38">
            <joint name="r_ankle_x" type="hinge" axis="1 0 0" range="-30 45"/>
            <joint name="r_ankle_y" type="hinge" axis="0 1 0" range="-20 20"/>
            <geom type="box" size="0.08 0.04 0.03" pos="0.03 0 0" mass="1.0"/>
          </body>
        </body>
      </body>
    </body>
  </worldbody>

  <actuator>
    <motor joint="lower_spine_x" gear="300"/>
    <motor joint="lower_spine_y" gear="300"/>
    <motor joint="lower_spine_z" gear="300"/>
    <motor joint="upper_spine_x" gear="300"/>
    <motor joint="upper_spine_y" gear="300"/>
    <motor joint="chest_x" gear="200"/>
    <motor joint="head_x" gear="50"/>
    <motor joint="head_y" gear="50"/>
    <motor joint="l_shoulder_x" gear="150"/>
    <motor joint="l_shoulder_y" gear="150"/>
    <motor joint="l_shoulder_z" gear="150"/>
    <motor joint="l_elbow" gear="100"/>
    <motor joint="l_wrist_x" gear="50"/>
    <motor joint="r_shoulder_x" gear="150"/>
    <motor joint="r_shoulder_y" gear="150"/>
    <motor joint="r_shoulder_z" gear="150"/>
    <motor joint="r_elbow" gear="100"/>
    <motor joint="r_wrist_x" gear="50"/>
    <motor joint="l_hip_x" gear="600"/>
    <motor joint="l_hip_y" gear="600"/>
    <motor joint="l_hip_z" gear="600"/>
    <motor joint="l_knee" gear="400"/>
    <motor joint="l_ankle_x" gear="200"/>
    <motor joint="l_ankle_y" gear="200"/>
    <motor joint="r_hip_x" gear="600"/>
    <motor joint="r_hip_y" gear="600"/>
    <motor joint="r_hip_z" gear="600"/>
    <motor joint="r_knee" gear="400"/>
    <motor joint="r_ankle_x" gear="200"/>
    <motor joint="r_ankle_y" gear="200"/>
  </actuator>
</mujoco>
"""

# Mapping from the 20-part ArticulatedBody order to MuJoCo body names.
# The C++ ArticulatedBody has 20 parts; the MuJoCo humanoid has 17 bodies.
# Parts 4/5 (Neck/Head), 6/7 (LeftShoulder/LeftUpperArm), and 10/11
# (RightShoulder/RightUpperArm) share MuJoCo bodies via duplicates.
BODY_NAMES = [
    "pelvis",       # 0: Pelvis
    "lower_spine",  # 1: LowerSpine (Spine)
    "upper_spine",  # 2: UpperSpine (Spine1)
    "chest",        # 3: Chest (Spine2)
    "head",         # 4: Neck (mapped to head body)
    "head",         # 5: Head
    "l_shoulder",   # 6: LeftShoulder
    "l_shoulder",   # 7: LeftUpperArm (same MuJoCo body)
    "l_forearm",    # 8: LeftForearm
    "l_hand",       # 9: LeftHand
    "r_shoulder",   # 10: RightShoulder
    "r_shoulder",   # 11: RightUpperArm (same MuJoCo body)
    "r_forearm",    # 12: RightForearm
    "r_hand",       # 13: RightHand
    "l_thigh",      # 14: LeftThigh
    "l_shin",       # 15: LeftShin
    "l_foot",       # 16: LeftFoot
    "r_thigh",      # 17: RightThigh
    "r_shin",       # 18: RightShin
    "r_foot",       # 19: RightFoot
]

# Maps each of the 60 policy action outputs (20 parts * 3 torque components)
# to a MuJoCo actuator index, or None if the output is unused.
# Built at module level from actuator names in the MuJoCo XML.
_ACTION_TO_ACTUATOR_NAMES = [
    # Part 0: Pelvis (freejoint, no actuators)
    None, None, None,
    # Part 1: LowerSpine
    "lower_spine_x", "lower_spine_y", "lower_spine_z",
    # Part 2: UpperSpine
    "upper_spine_x", "upper_spine_y", None,
    # Part 3: Chest
    "chest_x", None, None,
    # Part 4: Neck -> head
    "head_x", "head_y", None,
    # Part 5: Head (duplicate, no unique actuators)
    None, None, None,
    # Part 6: LeftShoulder
    "l_shoulder_x", "l_shoulder_y", "l_shoulder_z",
    # Part 7: LeftUpperArm (duplicate of part 6, no unique actuators)
    None, None, None,
    # Part 8: LeftForearm
    "l_elbow", None, None,
    # Part 9: LeftHand
    "l_wrist_x", None, None,
    # Part 10: RightShoulder
    "r_shoulder_x", "r_shoulder_y", "r_shoulder_z",
    # Part 11: RightUpperArm (duplicate of part 10)
    None, None, None,
    # Part 12: RightForearm
    "r_elbow", None, None,
    # Part 13: RightHand
    "r_wrist_x", None, None,
    # Part 14: LeftThigh
    "l_hip_x", "l_hip_y", "l_hip_z",
    # Part 15: LeftShin
    "l_knee", None, None,
    # Part 16: LeftFoot
    "l_ankle_x", "l_ankle_y", None,
    # Part 17: RightThigh
    "r_hip_x", "r_hip_y", "r_hip_z",
    # Part 18: RightShin
    "r_knee", None, None,
    # Part 19: RightFoot
    "r_ankle_x", "r_ankle_y", None,
]


class UniConEnv:
    """Single-instance training environment using MuJoCo.

    Follows a Gymnasium-like interface but doesn't inherit from it
    to avoid the dependency. Can be wrapped with gymnasium.Env if needed.

    Action space: normalized torques in [-1, 1] for each actuator.
                  Scaled by effort factors before application.
    Observation space: state encoding matching C++ StateEncoder.
    """

    def __init__(
        self,
        config: TrainingConfig,
        xml_path: Optional[str] = None,
        motion_data: Optional[Dict[str, Any]] = None,
    ):
        self.config = config

        # Load MuJoCo model
        if xml_path and Path(xml_path).exists():
            self.model = mujoco.MjModel.from_xml_path(xml_path)
        else:
            self.model = mujoco.MjModel.from_xml_string(_DEFAULT_HUMANOID_XML)

        self.data = mujoco.MjData(self.model)
        self.model.opt.timestep = config.physics_dt / config.physics_substeps

        # Body and joint counts - always 20 to match C++ ArticulatedBody
        self.num_bodies = config.humanoid.num_bodies
        self.num_actuators = self.model.nu

        # State encoder matching C++
        self.encoder = StateEncoder(self.num_bodies, config.tau)
        self.obs_dim = self.encoder.observation_dim
        self.act_dim = config.humanoid.num_bodies * 3  # 20 * 3 = 60

        # Build action-to-actuator index mapping
        self._action_map = []  # length 60, each entry is MuJoCo actuator idx or -1
        for name in _ACTION_TO_ACTUATOR_NAMES:
            if name is None:
                self._action_map.append(-1)
            else:
                joint_id = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_JOINT, name)
                # Find actuator that drives this joint
                found = -1
                for ai in range(self.model.nu):
                    if self.model.actuator_trnid[ai, 0] == joint_id:
                        found = ai
                        break
                self._action_map.append(found)

        # Motion data for reference trajectories
        self.motion_data = motion_data
        self.current_motion = None
        self.motion_time = 0.0
        self.episode_length = 0

        # RSIS config
        self.rsis = config.rsis

        # Body ID mapping (MuJoCo body id for each part index)
        self._body_ids = []
        for name in BODY_NAMES[:self.num_bodies]:
            try:
                bid = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_BODY, name)
                self._body_ids.append(bid)
            except Exception:
                self._body_ids.append(1)  # fallback to first non-world body

    def reset(self, seed: Optional[int] = None) -> np.ndarray:
        """Reset the environment, optionally with RSIS."""
        if seed is not None:
            self._rng = np.random.RandomState(seed)
        elif not hasattr(self, "_rng"):
            self._rng = np.random.RandomState(self.config.seed)

        mujoco.mj_resetData(self.model, self.data)

        # Pick a random motion clip if available
        if self.motion_data and len(self.motion_data) > 0:
            keys = list(self.motion_data.keys())
            clip_name = keys[self._rng.randint(len(keys))]
            self.current_motion = self.motion_data[clip_name]

            # RSIS: start at a random offset into the clip
            offset = self._rng.randint(
                self.rsis.min_offset_frames,
                self.rsis.max_offset_frames + 1,
            )
            self.motion_time = offset * self.config.physics_dt

            # Set initial state from motion with noise
            self._set_state_from_motion(self.motion_time)
        else:
            self.current_motion = None
            self.motion_time = 0.0

        self.episode_length = 0
        mujoco.mj_forward(self.model, self.data)
        return self._get_obs()

    def step(self, action: np.ndarray) -> Tuple[np.ndarray, float, bool, Dict]:
        """Take one step: apply action, simulate, compute reward.

        Args:
            action: normalized torques in [-1, 1] of shape (act_dim,)

        Returns:
            obs, reward, done, info
        """
        # Clip and apply action via the 60->30 actuator mapping
        action = np.clip(action, -1.0, 1.0)
        self.data.ctrl[:] = 0.0
        effort = self.config.humanoid.effort_factors
        for i, actuator_idx in enumerate(self._action_map):
            if actuator_idx >= 0:
                part_idx = i // 3
                self.data.ctrl[actuator_idx] = action[i] * effort[part_idx]

        # Step physics (substeps are handled by MuJoCo timestep)
        for _ in range(self.config.physics_substeps):
            mujoco.mj_step(self.model, self.data)

        self.motion_time += self.config.physics_dt
        self.episode_length += 1

        # Get state
        obs = self._get_obs()

        # Compute reward
        reward, terms, should_terminate = self._compute_reward()

        # Check termination
        done = should_terminate
        # Also terminate if humanoid has fallen (root height too low)
        root_height = self.data.xpos[self._body_ids[0]][2]
        if root_height < 0.3:
            done = True
        # Max episode length
        if self.episode_length >= 1000:
            done = True

        info = {
            "reward_terms": terms,
            "root_height": root_height,
            "episode_length": self.episode_length,
        }

        return obs, reward, done, info

    def _get_obs(self) -> np.ndarray:
        """Build observation vector matching C++ StateEncoder."""
        root_pos, root_rot = self._get_body_state(0)[:2]
        root_lin_vel = self.data.cvel[self._body_ids[0]][3:6].copy()
        root_ang_vel = self.data.cvel[self._body_ids[0]][0:3].copy()

        joint_positions = np.zeros((self.num_bodies, 3), dtype=np.float32)
        joint_rotations = np.zeros((self.num_bodies, 4), dtype=np.float32)
        joint_ang_vels = np.zeros((self.num_bodies, 3), dtype=np.float32)

        for i in range(self.num_bodies):
            pos, rot = self._get_body_state(i)[:2]
            joint_positions[i] = pos
            joint_rotations[i] = rot
            # MuJoCo cvel: [angular(3), linear(3)]
            joint_ang_vels[i] = self.data.cvel[self._body_ids[i]][0:3]

        # Build target frames
        target_frames = self._get_target_frames()

        obs = self.encoder.encode(
            root_pos, root_rot, root_lin_vel, root_ang_vel,
            joint_positions, joint_rotations, joint_ang_vels,
            target_frames,
        )
        # Sanitize: replace NaN/inf from bad motion data with zeros
        np.nan_to_num(obs, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
        return obs

    def _get_body_state(self, part_idx: int) -> Tuple[np.ndarray, np.ndarray]:
        """Get position and rotation of a body part.

        Returns:
            pos: (3,) position
            rot: (4,) quaternion (w,x,y,z)
        """
        bid = self._body_ids[part_idx]
        pos = self.data.xpos[bid].copy().astype(np.float32)
        # MuJoCo uses (w,x,y,z) quaternion format, same as our convention
        rot = self.data.xquat[bid].copy().astype(np.float32)
        return pos, rot

    def _get_target_frames(self):
        """Get target frames from current motion clip."""
        frames = []
        for k in range(self.encoder.tau):
            future_time = self.motion_time + (k + 1) * self.config.physics_dt
            if self.current_motion is not None:
                frame = self._sample_motion(future_time)
            else:
                # Default: standing target
                frame = self._standing_target()
            frames.append(frame)
        return frames

    def _standing_target(self) -> dict:
        """Default standing target when no motion data is available."""
        root_pos, root_rot = self._get_body_state(0)[:2]
        return {
            "root_pos": np.array([root_pos[0], 1.0, root_pos[2]], dtype=np.float32),
            "root_rot": np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            "root_lin_vel": np.zeros(3, dtype=np.float32),
            "root_ang_vel": np.zeros(3, dtype=np.float32),
            "joint_positions": np.zeros((self.num_bodies, 3), dtype=np.float32),
            "joint_rotations": np.tile(
                np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                (self.num_bodies, 1),
            ),
            "joint_ang_vels": np.zeros((self.num_bodies, 3), dtype=np.float32),
        }

    def _sample_motion(self, time: float) -> dict:
        """Sample a motion clip at a given time.

        Motion data format (dict):
            "fps": float
            "frames": list of dicts, each with:
                "root_pos": (3,)
                "root_rot": (4,)  # w,x,y,z
                "joint_positions": (J, 3)
                "joint_rotations": (J, 4)
        """
        motion = self.current_motion
        fps = motion["fps"]
        frames = motion["frames"]
        num_frames = len(frames)

        # Compute frame index and interpolation factor
        frame_f = time * fps
        frame_idx = int(frame_f) % num_frames
        next_idx = (frame_idx + 1) % num_frames
        alpha = frame_f - int(frame_f)

        f0 = frames[frame_idx]
        f1 = frames[next_idx]

        # Linear interpolation for positions, slerp for rotations
        root_pos = (1 - alpha) * f0["root_pos"] + alpha * f1["root_pos"]
        root_rot = quat_slerp(f0["root_rot"], f1["root_rot"], alpha)
        joint_pos = (1 - alpha) * f0["joint_positions"] + alpha * f1["joint_positions"]

        joint_rot = np.zeros_like(f0["joint_rotations"])
        for j in range(joint_rot.shape[0]):
            joint_rot[j] = quat_slerp(
                f0["joint_rotations"][j], f1["joint_rotations"][j], alpha
            )

        # Finite-difference velocities
        dt = 1.0 / fps
        root_lin_vel = (f1["root_pos"] - f0["root_pos"]) / dt
        root_ang_vel = np.zeros(3, dtype=np.float32)  # simplified
        joint_ang_vels = np.zeros((joint_rot.shape[0], 3), dtype=np.float32)

        result = {
            "root_pos": root_pos.astype(np.float32),
            "root_rot": root_rot.astype(np.float32),
            "root_lin_vel": root_lin_vel.astype(np.float32),
            "root_ang_vel": root_ang_vel.astype(np.float32),
            "joint_positions": joint_pos.astype(np.float32),
            "joint_rotations": joint_rot.astype(np.float32),
            "joint_ang_vels": joint_ang_vels.astype(np.float32),
        }
        # Sanitize any NaN/inf from bad motion data
        for key, val in result.items():
            np.nan_to_num(val, copy=False, nan=0.0, posinf=0.0, neginf=0.0)
        return result

    def _compute_reward(self) -> Tuple[float, np.ndarray, bool]:
        """Compute reward by comparing current state to target."""
        target = self._get_target_frames()[0] if self.encoder.tau > 0 else self._standing_target()

        # Current state
        actual_root_pos, actual_root_rot = self._get_body_state(0)[:2]
        actual_joint_pos = np.zeros((self.num_bodies, 3), dtype=np.float32)
        actual_joint_rot = np.zeros((self.num_bodies, 4), dtype=np.float32)
        actual_joint_ang_vel = np.zeros((self.num_bodies, 3), dtype=np.float32)

        for i in range(self.num_bodies):
            pos, rot = self._get_body_state(i)[:2]
            actual_joint_pos[i] = pos
            actual_joint_rot[i] = rot
            actual_joint_ang_vel[i] = self.data.cvel[self._body_ids[i]][0:3]

        return compute_reward(
            actual_root_pos, actual_root_rot,
            actual_joint_pos, actual_joint_rot, actual_joint_ang_vel,
            target["root_pos"], target["root_rot"],
            target["joint_positions"], target["joint_rotations"],
            target["joint_ang_vels"],
            self.config.reward,
        )

    def _set_state_from_motion(self, time: float):
        """Set MuJoCo state from a motion clip frame (for RSIS)."""
        if self.current_motion is None:
            return
        frame = self._sample_motion(time)
        # Set root position and orientation via qpos
        # MuJoCo humanoid qpos: [3 pos, 4 quat, joint angles...]
        if self.model.nq >= 7:
            self.data.qpos[0:3] = frame["root_pos"]
            self.data.qpos[3:7] = frame["root_rot"]
            # Add noise per RSIS
            self.data.qpos[0:3] += self._rng.normal(0, self.rsis.position_noise_std, 3)
            if self.model.nq > 7:
                self.data.qpos[7:] += self._rng.normal(
                    0, self.rsis.rotation_noise_std, self.model.nq - 7
                )
