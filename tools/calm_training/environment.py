"""MuJoCo-based training environment for CALM.

Similar to UniCon environment but:
  - Actions are 37 target joint angles (position-controlled), not 60 torques
  - Uses CALMObservationExtractor (AMP-style) not UniCon StateEncoder
  - Returns AMP transition pair (obs_t, obs_t1) in info dict for discriminator
  - Task reward is separate from AMP reward (combined externally)

Requires: mujoco >= 3.0
"""

import mujoco
import numpy as np
from typing import Optional, Tuple, Dict, Any

from .config import CALMConfig, HumanoidCALMConfig, EnvironmentConfig
from .observation import CALMObservationExtractor
from tools.ml.quaternion import slerp as quat_slerp, quat_mul, quat_inverse


# MuJoCo humanoid XML with 37 position-controlled actuators.
# DOF layout: pelvis(3) + abdomen(3) + chest(3) + neck(3) + head(3) +
#   r_upper_arm(3) + r_lower_arm(1) + l_upper_arm(3) + l_lower_arm(1) +
#   r_thigh(3) + r_shin(1) + r_foot(3) + l_thigh(3) + l_shin(1) + l_foot(3)
_CALM_HUMANOID_XML = """
<mujoco model="calm_humanoid">
  <option timestep="0.008333" iterations="50" solver="Newton">
    <flag warmstart="enable"/>
  </option>

  <default>
    <joint damping="0.5" armature="0.01"/>
    <geom condim="3" friction="1 0.5 0.5" margin="0.001"/>
    <position kp="40" kv="5" ctrllimited="true"/>
  </default>

  <worldbody>
    <light diffuse="0.8 0.8 0.8" pos="0 0 3" dir="0 0 -1"/>
    <geom type="plane" size="50 50 1" rgba="0.8 0.8 0.8 1"/>

    <body name="pelvis" pos="0 0 1.0">
      <freejoint name="root"/>
      <geom type="capsule" fromto="0 -0.07 0 0 0.07 0" size="0.08" mass="8.0"/>

      <!-- Abdomen (3 DOF) -->
      <body name="abdomen" pos="0 0 0.1">
        <joint name="abdomen_x" type="hinge" axis="1 0 0" range="-45 45"/>
        <joint name="abdomen_y" type="hinge" axis="0 1 0" range="-45 45"/>
        <joint name="abdomen_z" type="hinge" axis="0 0 1" range="-30 30"/>
        <geom type="capsule" fromto="0 0 0 0 0 0.12" size="0.07" mass="5.0"/>

        <!-- Chest (3 DOF) -->
        <body name="chest" pos="0 0 0.12">
          <joint name="chest_x" type="hinge" axis="1 0 0" range="-30 30"/>
          <joint name="chest_y" type="hinge" axis="0 1 0" range="-20 20"/>
          <joint name="chest_z" type="hinge" axis="0 0 1" range="-20 20"/>
          <geom type="capsule" fromto="0 -0.1 0 0 0.1 0" size="0.09" mass="8.0"/>

          <!-- Neck (3 DOF) -->
          <body name="neck" pos="0 0 0.12">
            <joint name="neck_x" type="hinge" axis="1 0 0" range="-20 20"/>
            <joint name="neck_y" type="hinge" axis="0 1 0" range="-30 30"/>
            <joint name="neck_z" type="hinge" axis="0 0 1" range="-15 15"/>
            <geom type="capsule" fromto="0 0 0 0 0 0.06" size="0.04" mass="1.0"/>

            <!-- Head (3 DOF) -->
            <body name="head" pos="0 0 0.06">
              <joint name="head_x" type="hinge" axis="1 0 0" range="-30 30"/>
              <joint name="head_y" type="hinge" axis="0 1 0" range="-45 45"/>
              <joint name="head_z" type="hinge" axis="0 0 1" range="-20 20"/>
              <geom type="sphere" size="0.1" mass="4.0"/>
            </body>
          </body>

          <!-- Right upper arm (3 DOF) -->
          <body name="r_upper_arm" pos="0 -0.18 0.05">
            <joint name="r_shoulder_x" type="hinge" axis="1 0 0" range="-90 90"/>
            <joint name="r_shoulder_y" type="hinge" axis="0 1 0" range="-160 60"/>
            <joint name="r_shoulder_z" type="hinge" axis="0 0 1" range="-30 90"/>
            <geom type="capsule" fromto="0 0 0 0 -0.25 0" size="0.04" mass="2.0"/>

            <!-- Right lower arm (1 DOF: elbow) -->
            <body name="r_lower_arm" pos="0 -0.25 0">
              <joint name="r_elbow" type="hinge" axis="1 0 0" range="-150 0"/>
              <geom type="capsule" fromto="0 0 0 0 -0.22 0" size="0.035" mass="1.5"/>
              <body name="r_hand" pos="0 -0.22 0">
                <geom type="box" size="0.04 0.06 0.02" mass="0.5"/>
              </body>
            </body>
          </body>

          <!-- Left upper arm (3 DOF) -->
          <body name="l_upper_arm" pos="0 0.18 0.05">
            <joint name="l_shoulder_x" type="hinge" axis="1 0 0" range="-90 90"/>
            <joint name="l_shoulder_y" type="hinge" axis="0 1 0" range="-60 160"/>
            <joint name="l_shoulder_z" type="hinge" axis="0 0 1" range="-90 30"/>
            <geom type="capsule" fromto="0 0 0 0 0.25 0" size="0.04" mass="2.0"/>

            <!-- Left lower arm (1 DOF: elbow) -->
            <body name="l_lower_arm" pos="0 0.25 0">
              <joint name="l_elbow" type="hinge" axis="1 0 0" range="0 150"/>
              <geom type="capsule" fromto="0 0 0 0 0.22 0" size="0.035" mass="1.5"/>
              <body name="l_hand" pos="0 0.22 0">
                <geom type="box" size="0.04 0.06 0.02" mass="0.5"/>
              </body>
            </body>
          </body>
        </body>
      </body>

      <!-- Right thigh (3 DOF) -->
      <body name="r_thigh" pos="0 -0.1 -0.05">
        <joint name="r_hip_x" type="hinge" axis="1 0 0" range="-30 120"/>
        <joint name="r_hip_y" type="hinge" axis="0 1 0" range="-45 45"/>
        <joint name="r_hip_z" type="hinge" axis="0 0 1" range="-30 30"/>
        <geom type="capsule" fromto="0 0 0 0 0 -0.4" size="0.06" mass="5.0"/>

        <!-- Right shin (1 DOF: knee) -->
        <body name="r_shin" pos="0 0 -0.4">
          <joint name="r_knee" type="hinge" axis="0 1 0" range="0 150"/>
          <geom type="capsule" fromto="0 0 0 0 0 -0.38" size="0.05" mass="3.0"/>

          <!-- Right foot (3 DOF: ankle) -->
          <body name="r_foot" pos="0 0 -0.38">
            <joint name="r_ankle_x" type="hinge" axis="1 0 0" range="-30 45"/>
            <joint name="r_ankle_y" type="hinge" axis="0 1 0" range="-20 20"/>
            <joint name="r_ankle_z" type="hinge" axis="0 0 1" range="-15 15"/>
            <geom type="box" size="0.08 0.04 0.03" pos="0.03 0 0" mass="1.0"/>
          </body>
        </body>
      </body>

      <!-- Left thigh (3 DOF) -->
      <body name="l_thigh" pos="0 0.1 -0.05">
        <joint name="l_hip_x" type="hinge" axis="1 0 0" range="-30 120"/>
        <joint name="l_hip_y" type="hinge" axis="0 1 0" range="-45 45"/>
        <joint name="l_hip_z" type="hinge" axis="0 0 1" range="-30 30"/>
        <geom type="capsule" fromto="0 0 0 0 0 -0.4" size="0.06" mass="5.0"/>

        <!-- Left shin (1 DOF: knee) -->
        <body name="l_shin" pos="0 0 -0.4">
          <joint name="l_knee" type="hinge" axis="0 1 0" range="-150 0"/>
          <geom type="capsule" fromto="0 0 0 0 0 -0.38" size="0.05" mass="3.0"/>

          <!-- Left foot (3 DOF: ankle) -->
          <body name="l_foot" pos="0 0 -0.38">
            <joint name="l_ankle_x" type="hinge" axis="1 0 0" range="-30 45"/>
            <joint name="l_ankle_y" type="hinge" axis="0 1 0" range="-20 20"/>
            <joint name="l_ankle_z" type="hinge" axis="0 0 1" range="-15 15"/>
            <geom type="box" size="0.08 0.04 0.03" pos="0.03 0 0" mass="1.0"/>
          </body>
        </body>
      </body>
    </body>
  </worldbody>

  <actuator>
    <!-- Pelvis: 3 DOF (virtual actuators on root) -->
    <!-- In practice these are handled by the freejoint, but we need
         37 actuators to match the DOF count. Pelvis DOFs use zero-range
         position actuators that effectively do nothing. -->
    <!-- Abdomen (3) -->
    <position joint="abdomen_x" ctrlrange="-0.785 0.785"/>
    <position joint="abdomen_y" ctrlrange="-0.785 0.785"/>
    <position joint="abdomen_z" ctrlrange="-0.524 0.524"/>
    <!-- Chest (3) -->
    <position joint="chest_x" ctrlrange="-0.524 0.524"/>
    <position joint="chest_y" ctrlrange="-0.349 0.349"/>
    <position joint="chest_z" ctrlrange="-0.349 0.349"/>
    <!-- Neck (3) -->
    <position joint="neck_x" ctrlrange="-0.349 0.349"/>
    <position joint="neck_y" ctrlrange="-0.524 0.524"/>
    <position joint="neck_z" ctrlrange="-0.262 0.262"/>
    <!-- Head (3) -->
    <position joint="head_x" ctrlrange="-0.524 0.524"/>
    <position joint="head_y" ctrlrange="-0.785 0.785"/>
    <position joint="head_z" ctrlrange="-0.349 0.349"/>
    <!-- Right upper arm (3) -->
    <position joint="r_shoulder_x" ctrlrange="-1.571 1.571"/>
    <position joint="r_shoulder_y" ctrlrange="-2.793 1.047"/>
    <position joint="r_shoulder_z" ctrlrange="-0.524 1.571"/>
    <!-- Right lower arm (1) -->
    <position joint="r_elbow" ctrlrange="-2.618 0"/>
    <!-- Left upper arm (3) -->
    <position joint="l_shoulder_x" ctrlrange="-1.571 1.571"/>
    <position joint="l_shoulder_y" ctrlrange="-1.047 2.793"/>
    <position joint="l_shoulder_z" ctrlrange="-1.571 0.524"/>
    <!-- Left lower arm (1) -->
    <position joint="l_elbow" ctrlrange="0 2.618"/>
    <!-- Right thigh (3) -->
    <position joint="r_hip_x" ctrlrange="-0.524 2.094"/>
    <position joint="r_hip_y" ctrlrange="-0.785 0.785"/>
    <position joint="r_hip_z" ctrlrange="-0.524 0.524"/>
    <!-- Right shin (1) -->
    <position joint="r_knee" ctrlrange="0 2.618"/>
    <!-- Right foot (3) -->
    <position joint="r_ankle_x" ctrlrange="-0.524 0.785"/>
    <position joint="r_ankle_y" ctrlrange="-0.349 0.349"/>
    <position joint="r_ankle_z" ctrlrange="-0.262 0.262"/>
    <!-- Left thigh (3) -->
    <position joint="l_hip_x" ctrlrange="-0.524 2.094"/>
    <position joint="l_hip_y" ctrlrange="-0.785 0.785"/>
    <position joint="l_hip_z" ctrlrange="-0.524 0.524"/>
    <!-- Left shin (1) -->
    <position joint="l_knee" ctrlrange="-2.618 0"/>
    <!-- Left foot (3) -->
    <position joint="l_ankle_x" ctrlrange="-0.524 0.785"/>
    <position joint="l_ankle_y" ctrlrange="-0.349 0.349"/>
    <position joint="l_ankle_z" ctrlrange="-0.262 0.262"/>
  </actuator>
</mujoco>
"""

# Key bodies: head, r_hand, l_hand, r_foot, l_foot
_KEY_BODY_NAMES = ["head", "r_hand", "l_hand", "r_foot", "l_foot"]

# Joint names in DOF order (37 total, matching the actuator order)
# First 3 DOFs are pelvis (not actuated via position servo, handled by freejoint)
_DOF_JOINT_NAMES = [
    # Abdomen (3)
    "abdomen_x", "abdomen_y", "abdomen_z",
    # Chest (3)
    "chest_x", "chest_y", "chest_z",
    # Neck (3)
    "neck_x", "neck_y", "neck_z",
    # Head (3)
    "head_x", "head_y", "head_z",
    # Right upper arm (3)
    "r_shoulder_x", "r_shoulder_y", "r_shoulder_z",
    # Right lower arm (1)
    "r_elbow",
    # Left upper arm (3)
    "l_shoulder_x", "l_shoulder_y", "l_shoulder_z",
    # Left lower arm (1)
    "l_elbow",
    # Right thigh (3)
    "r_hip_x", "r_hip_y", "r_hip_z",
    # Right shin (1)
    "r_knee",
    # Right foot (3)
    "r_ankle_x", "r_ankle_y", "r_ankle_z",
    # Left thigh (3)
    "l_hip_x", "l_hip_y", "l_hip_z",
    # Left shin (1)
    "l_knee",
    # Left foot (3)
    "l_ankle_x", "l_ankle_y", "l_ankle_z",
]

# The first 3 DOFs in the C++ config are pelvis rotations (from freejoint).
# In MuJoCo, the freejoint gives 3 translation + 4 quaternion DOFs in qpos,
# and joint angles start after that. We skip pelvis in actuators since the
# freejoint isn't position-controlled.
_NUM_ACTUATED_DOFS = 34  # 37 total - 3 pelvis


class CALMEnv:
    """Single-instance CALM training environment using MuJoCo.

    Action space: 37 target joint angles (first 3 are pelvis, ignored).
                  The remaining 34 drive position servos.
    Observation space: per-frame obs (102 dims) extracted by CALMObservationExtractor.
    """

    def __init__(
        self,
        config: CALMConfig,
        motion_data: Optional[Dict[str, Any]] = None,
    ):
        self.config = config
        self.humanoid = config.humanoid
        self.env_config = config.env

        # Load MuJoCo model
        self.model = mujoco.MjModel.from_xml_string(_CALM_HUMANOID_XML)
        self.data = mujoco.MjData(self.model)
        self.model.opt.timestep = config.env.sim_timestep / config.env.sim_substeps

        # Observation extractor
        self.obs_extractor = CALMObservationExtractor(self.humanoid)
        self.obs_dim = self.humanoid.policy_obs_dim  # 204 (stacked)
        self.act_dim = self.humanoid.num_dof  # 37

        # Key body IDs
        self._key_body_ids = []
        for name in _KEY_BODY_NAMES:
            bid = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_BODY, name)
            self._key_body_ids.append(bid)

        # Root body ID (pelvis)
        self._root_body_id = mujoco.mj_name2id(
            self.model, mujoco.mjtObj.mjOBJ_BODY, "pelvis"
        )

        # Joint ID mapping for DOF readout
        self._dof_joint_ids = []
        for name in _DOF_JOINT_NAMES:
            jid = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_JOINT, name)
            self._dof_joint_ids.append(jid)

        # Motion data
        self.motion_data = motion_data
        self.current_motion = None
        self.motion_time = 0.0
        self.episode_length = 0

        # Previous frame obs for AMP transition pair
        self._prev_per_frame_obs = None

    def reset(self, seed: Optional[int] = None) -> np.ndarray:
        """Reset environment. Returns stacked policy observation."""
        if seed is not None:
            self._rng = np.random.RandomState(seed)
        elif not hasattr(self, "_rng"):
            self._rng = np.random.RandomState(42)

        mujoco.mj_resetData(self.model, self.data)
        self.obs_extractor.reset()
        self.episode_length = 0
        self._prev_per_frame_obs = None

        # Pick a random motion clip
        if self.motion_data and len(self.motion_data) > 0:
            keys = list(self.motion_data.keys())
            clip_name = keys[self._rng.randint(len(keys))]
            self.current_motion = self.motion_data[clip_name]
            # Start at random offset
            num_frames = len(self.current_motion["frames"])
            offset = self._rng.randint(0, max(1, num_frames - 1))
            self.motion_time = offset / self.current_motion["fps"]
        else:
            self.current_motion = None
            self.motion_time = 0.0

        mujoco.mj_forward(self.model, self.data)

        # Extract initial observations (need at least 2 frames for policy)
        per_frame = self._extract_per_frame_obs()
        self._prev_per_frame_obs = per_frame.copy()
        # Extract one more frame to have 2 in history
        per_frame = self._extract_per_frame_obs()
        self._prev_per_frame_obs = per_frame.copy()

        return self.obs_extractor.get_policy_obs()

    def step(
        self, action: np.ndarray
    ) -> Tuple[np.ndarray, float, bool, Dict[str, Any]]:
        """Take one step.

        Args:
            action: (37,) target joint angles. First 3 (pelvis) are ignored.

        Returns:
            obs: (policy_obs_dim,) stacked observation
            reward: scalar task reward (AMP reward computed externally)
            done: episode termination
            info: dict with 'obs_t' and 'obs_t1' for AMP discriminator
        """
        # Save pre-step per-frame obs for AMP transition
        obs_t = self._prev_per_frame_obs

        # Apply actions as position targets (skip first 3 pelvis DOFs)
        action = np.clip(action, -1.0, 1.0)
        for i in range(min(len(_DOF_JOINT_NAMES), self.model.nu)):
            # Map action[i+3] (skip pelvis) to actuator control
            # Actions are normalized [-1, 1], scale to control range
            ctrl_range = self.model.actuator_ctrlrange[i]
            target = 0.5 * (ctrl_range[0] + ctrl_range[1]) + \
                     0.5 * (ctrl_range[1] - ctrl_range[0]) * action[i + 3]
            self.data.ctrl[i] = target

        # Step physics
        for _ in range(self.env_config.sim_substeps):
            mujoco.mj_step(self.model, self.data)

        self.motion_time += self.env_config.sim_timestep
        self.episode_length += 1

        # Extract new observation
        per_frame = self._extract_per_frame_obs()
        obs_t1 = per_frame.copy()
        self._prev_per_frame_obs = obs_t1

        policy_obs = self.obs_extractor.get_policy_obs()

        # Simple task reward (alive bonus)
        reward = 1.0

        # Termination checks
        root_height = self.data.xpos[self._root_body_id][2]
        done = False
        if root_height < self.env_config.early_termination_height:
            done = True
        if self.episode_length >= self.env_config.max_episode_steps:
            done = True

        info = {
            "obs_t": obs_t if obs_t is not None else np.zeros(
                self.humanoid.per_frame_obs_dim, dtype=np.float32
            ),
            "obs_t1": obs_t1,
            "root_height": root_height,
            "episode_length": self.episode_length,
        }

        return policy_obs, reward, done, info

    def _extract_per_frame_obs(self) -> np.ndarray:
        """Extract a single per-frame observation from current MuJoCo state."""
        # Root state
        root_pos = self.data.xpos[self._root_body_id].copy().astype(np.float32)
        root_rot = self.data.xquat[self._root_body_id].copy().astype(np.float32)

        # Root velocities from MuJoCo
        root_vel = self.data.cvel[self._root_body_id][3:6].copy().astype(np.float32)
        root_ang_vel = self.data.cvel[self._root_body_id][0:3].copy().astype(np.float32)

        # DOF positions from joint angles
        # MuJoCo qpos layout: [3 pos, 4 quat, joint_angles...]
        dof_positions = np.zeros(self.humanoid.num_dof, dtype=np.float32)
        # First 3 DOFs are pelvis (from freejoint, approximated as zero)
        for i, jid in enumerate(self._dof_joint_ids):
            qadr = self.model.jnt_qposadr[jid]
            dof_positions[i + 3] = self.data.qpos[qadr]

        # Key body positions
        key_body_positions = np.zeros(
            (self.humanoid.num_key_bodies, 3), dtype=np.float32
        )
        for i, bid in enumerate(self._key_body_ids):
            key_body_positions[i] = self.data.xpos[bid].astype(np.float32)

        return self.obs_extractor.extract_frame(
            root_pos=root_pos,
            root_rot=root_rot,
            root_vel=root_vel,
            root_ang_vel=root_ang_vel,
            dof_positions=dof_positions,
            key_body_positions=key_body_positions,
            delta_time=self.env_config.sim_timestep,
        )
