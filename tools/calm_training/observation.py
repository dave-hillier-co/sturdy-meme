"""CALM observation extractor.

Replicates the observation layout from src/ml/ObservationExtractor.cpp.

Per-frame layout (102 dims):
    root_height(1) + root_rot_6D(6) + local_root_vel(3) +
    local_root_ang_vel(3) + dof_positions(37) + dof_velocities(37) +
    key_body_positions(5*3=15) = 102

Coordinate system: Y-up.
"""

import numpy as np
from collections import deque

from .config import HumanoidCALMConfig


def get_heading_angle(q: np.ndarray) -> float:
    """Compute heading angle from quaternion (w,x,y,z).

    Projects forward direction (0,0,1) onto XZ plane.
    Matches ObservationExtractor::getHeadingAngle.
    """
    w, x, y, z = q
    # Rotate forward vector (0,0,1) by quaternion
    # Using quaternion rotation formula: q * v * q^-1
    # For v = (0,0,1):
    #   forward.x = 2*(x*z + w*y)
    #   forward.y = 2*(y*z - w*x)
    #   forward.z = 1 - 2*(x*x + y*y)
    forward_x = 2.0 * (x * z + w * y)
    forward_z = 1.0 - 2.0 * (x * x + y * y)
    return np.arctan2(forward_x, forward_z)


def remove_heading(q: np.ndarray) -> np.ndarray:
    """Remove heading (yaw) from quaternion.

    Matches ObservationExtractor::removeHeading:
        headingQuat = angleAxis(-heading, Y)
        return headingQuat * q
    """
    heading = get_heading_angle(q)
    # angleAxis(-heading, Y=(0,1,0))
    half = -heading * 0.5
    heading_quat = np.array([np.cos(half), 0.0, np.sin(half), 0.0])
    return _quat_mul(heading_quat, q)


def quat_to_tan_norm_6d(q: np.ndarray) -> np.ndarray:
    """Convert quaternion to 6D rotation representation.

    Takes first two columns of the rotation matrix.
    Matches ObservationExtractor::quatToTanNorm6D.
    """
    w, x, y, z = q
    # Rotation matrix from quaternion (column-major like glm)
    # Column 0
    m00 = 1.0 - 2.0 * (y * y + z * z)
    m01 = 2.0 * (x * y + w * z)
    m02 = 2.0 * (x * z - w * y)
    # Column 1
    m10 = 2.0 * (x * y - w * z)
    m11 = 1.0 - 2.0 * (x * x + z * z)
    m12 = 2.0 * (y * z + w * x)

    return np.array([m00, m01, m02, m10, m11, m12], dtype=np.float32)


def rotate_to_heading_frame(vec: np.ndarray, heading: float) -> np.ndarray:
    """Rotate a 3D vector into the heading frame.

    Matches the inline rotation in ObservationExtractor:
        local.x = cos(-h) * world.x + sin(-h) * world.z
        local.y = world.y
        local.z = -sin(-h) * world.x + cos(-h) * world.z
    """
    cos_h = np.cos(-heading)
    sin_h = np.sin(-heading)
    return np.array([
        cos_h * vec[0] + sin_h * vec[2],
        vec[1],
        -sin_h * vec[0] + cos_h * vec[2],
    ], dtype=np.float32)


def _quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Quaternion multiplication (w,x,y,z)."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return np.array([
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    ])


def _quat_inverse(q: np.ndarray) -> np.ndarray:
    """Inverse of unit quaternion."""
    return np.array([q[0], -q[1], -q[2], -q[3]])


def _quat_to_axis_angle(q: np.ndarray):
    """Convert quaternion to axis-angle. Returns (axis, angle)."""
    w = np.clip(q[0], -1.0, 1.0)
    angle = 2.0 * np.arccos(abs(w))
    sin_half = np.sqrt(1.0 - w * w)
    if sin_half > 1e-6:
        axis = np.array([q[1], q[2], q[3]]) / sin_half
    else:
        axis = np.array([0.0, 1.0, 0.0])
    return axis, angle


def _matrix_to_euler_xyz(rot_matrix: np.ndarray) -> np.ndarray:
    """Extract Euler angles (XYZ intrinsic) from 3x3 rotation matrix.

    Matches ObservationExtractor::matrixToEulerXYZ.
    """
    sy = rot_matrix[0, 2]
    if abs(sy) < 0.99999:
        rx = np.arctan2(-rot_matrix[1, 2], rot_matrix[2, 2])
        ry = np.arcsin(sy)
        rz = np.arctan2(-rot_matrix[0, 1], rot_matrix[0, 0])
    else:
        # Gimbal lock
        rx = np.arctan2(rot_matrix[2, 1], rot_matrix[1, 1])
        ry = np.pi / 2.0 if sy > 0 else -np.pi / 2.0
        rz = 0.0
    return np.array([rx, ry, rz])


def _quat_to_rotation_matrix(q: np.ndarray) -> np.ndarray:
    """Convert quaternion (w,x,y,z) to 3x3 rotation matrix."""
    w, x, y, z = q
    return np.array([
        [1 - 2*(y*y + z*z), 2*(x*y - w*z),     2*(x*z + w*y)],
        [2*(x*y + w*z),     1 - 2*(x*x + z*z), 2*(y*z - w*x)],
        [2*(x*z - w*y),     2*(y*z + w*x),     1 - 2*(x*x + y*y)],
    ])


class CALMObservationExtractor:
    """Extracts per-frame observations and maintains a temporal history buffer.

    Replicates ObservationExtractor from C++ for use in training.
    Supports both animation-based and physics-based observation extraction.
    """

    def __init__(self, config: HumanoidCALMConfig):
        self.config = config
        self._max_history = max(config.num_policy_obs_steps, config.num_encoder_obs_steps)
        self._history: deque = deque(maxlen=self._max_history)
        self._prev_dof_positions = np.zeros(config.num_dof, dtype=np.float32)
        self._prev_root_rot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)
        self._has_previous = False

    def reset(self):
        """Clear history and previous frame state."""
        self._history.clear()
        self._prev_dof_positions[:] = 0.0
        self._prev_root_rot = np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32)
        self._has_previous = False

    def extract_frame(
        self,
        root_pos: np.ndarray,
        root_rot: np.ndarray,
        root_vel: np.ndarray,
        root_ang_vel: np.ndarray,
        dof_positions: np.ndarray,
        key_body_positions: np.ndarray,
        delta_time: float,
    ) -> np.ndarray:
        """Extract a single frame observation from physics state.

        Args:
            root_pos: (3,) world position of root
            root_rot: (4,) quaternion (w,x,y,z) of root
            root_vel: (3,) world-space linear velocity
            root_ang_vel: (3,) world-space angular velocity
            dof_positions: (num_dof,) joint angles in radians
            key_body_positions: (num_key_bodies, 3) world positions
            delta_time: timestep for finite differences

        Returns:
            (per_frame_obs_dim,) observation vector
        """
        obs = np.zeros(self.config.per_frame_obs_dim, dtype=np.float32)
        idx = 0

        # 1) Root height (1D)
        obs[idx] = root_pos[1]  # Y-up
        idx += 1

        # 2) Heading-invariant root rotation (6D)
        heading_free = remove_heading(root_rot)
        rot_6d = quat_to_tan_norm_6d(heading_free)
        obs[idx:idx + 6] = rot_6d
        idx += 6

        # 3) Local root velocity in heading frame (3D)
        heading = get_heading_angle(root_rot)
        local_vel = rotate_to_heading_frame(root_vel, heading)
        obs[idx:idx + 3] = local_vel
        idx += 3

        # 4) Local root angular velocity in heading frame (3D)
        if self._has_previous and delta_time > 0.0:
            local_ang_vel = rotate_to_heading_frame(root_ang_vel, heading)
        else:
            local_ang_vel = np.zeros(3, dtype=np.float32)
        obs[idx:idx + 3] = local_ang_vel
        idx += 3

        # 5) DOF positions (num_dof)
        obs[idx:idx + self.config.num_dof] = dof_positions
        idx += self.config.num_dof

        # 6) DOF velocities via finite difference (num_dof)
        if self._has_previous and delta_time > 0.0:
            dof_vel = (dof_positions - self._prev_dof_positions) / delta_time
        else:
            dof_vel = np.zeros(self.config.num_dof, dtype=np.float32)
        obs[idx:idx + self.config.num_dof] = dof_vel
        idx += self.config.num_dof

        # 7) Key body positions relative to root in heading frame (num_key_bodies * 3)
        for kb_idx in range(self.config.num_key_bodies):
            rel_pos = key_body_positions[kb_idx] - root_pos
            local_pos = rotate_to_heading_frame(rel_pos, heading)
            obs[idx:idx + 3] = local_pos
            idx += 3

        assert idx == self.config.per_frame_obs_dim

        # Sanitize NaN/Inf values
        np.nan_to_num(obs, nan=0.0, posinf=100.0, neginf=-100.0, copy=False)
        np.clip(obs, -100.0, 100.0, out=obs)

        # Update history
        self._history.append(obs.copy())
        self._prev_dof_positions = dof_positions.copy()
        self._prev_root_rot = root_rot.copy()
        self._has_previous = True

        return obs

    def extract_frame_from_motion(
        self,
        root_pos: np.ndarray,
        root_rot: np.ndarray,
        joint_rotations: np.ndarray,
        key_body_positions: np.ndarray,
        delta_time: float,
        prev_root_pos: np.ndarray = None,
        prev_root_rot: np.ndarray = None,
        prev_joint_rotations: np.ndarray = None,
    ) -> np.ndarray:
        """Extract observation from motion clip data.

        Computes velocities from finite differences when previous frame is provided.

        Args:
            root_pos: (3,) root position
            root_rot: (4,) root quaternion (w,x,y,z)
            joint_rotations: (num_joints, 4) joint quaternions
            key_body_positions: (num_key_bodies, 3) key body positions
            delta_time: time between frames
            prev_root_pos: previous frame root position (for velocity)
            prev_root_rot: previous frame root quaternion (for angular velocity)
            prev_joint_rotations: previous frame joint rotations (for DOF velocity)
        """
        # Compute root velocity from finite differences
        if prev_root_pos is not None and delta_time > 0:
            root_vel = (root_pos - prev_root_pos) / delta_time
        else:
            root_vel = np.zeros(3, dtype=np.float32)

        # Compute root angular velocity from quaternion difference
        if prev_root_rot is not None and delta_time > 0:
            delta_rot = _quat_mul(root_rot, _quat_inverse(prev_root_rot))
            # Normalize to avoid numerical issues
            delta_norm = np.linalg.norm(delta_rot)
            if delta_norm > 1e-8:
                delta_rot = delta_rot / delta_norm
            else:
                delta_rot = np.array([1.0, 0.0, 0.0, 0.0])
            axis, angle = _quat_to_axis_angle(delta_rot)
            root_ang_vel = axis * (angle / delta_time)
            # Clamp angular velocity to reasonable range
            np.clip(root_ang_vel, -50.0, 50.0, out=root_ang_vel)
        else:
            root_ang_vel = np.zeros(3, dtype=np.float32)

        # Extract DOF positions from joint rotations (Euler XYZ decomposition)
        dof_positions = self._extract_dof_positions(joint_rotations)

        return self.extract_frame(
            root_pos, root_rot, root_vel, root_ang_vel,
            dof_positions, key_body_positions, delta_time,
        )

    def _extract_dof_positions(self, joint_rotations: np.ndarray) -> np.ndarray:
        """Extract DOF positions from joint quaternions.

        The DOF layout (37 DOFs) maps joints to Euler angle axes:
            pelvis(3) + abdomen(3) + chest(3) + neck(3) + head(3) +
            r_upper_arm(3) + r_lower_arm(1) + l_upper_arm(3) + l_lower_arm(1) +
            r_thigh(3) + r_shin(1) + r_foot(3) + l_thigh(3) + l_shin(1) + l_foot(3)
        """
        dof_positions = np.zeros(self.config.num_dof, dtype=np.float32)

        # Joint-to-DOF mapping: (joint_index, [axis indices])
        # Each joint contributes 1 or 3 Euler angles (XYZ) to the DOF vector
        joint_dof_map = [
            (0, [0, 1, 2]),    # pelvis: 3 DOF
            (1, [0, 1, 2]),    # abdomen: 3 DOF
            (2, [0, 1, 2]),    # chest: 3 DOF
            (3, [0, 1, 2]),    # neck: 3 DOF
            (4, [0, 1, 2]),    # head: 3 DOF
            (5, [0, 1, 2]),    # r_upper_arm: 3 DOF
            (6, [0]),          # r_lower_arm: 1 DOF (elbow)
            (7, [0, 1, 2]),    # l_upper_arm: 3 DOF
            (8, [0]),          # l_lower_arm: 1 DOF (elbow)
            (9, [0, 1, 2]),    # r_thigh: 3 DOF
            (10, [0]),         # r_shin: 1 DOF (knee)
            (11, [0, 1, 2]),   # r_foot: 3 DOF (ankle)
            (12, [0, 1, 2]),   # l_thigh: 3 DOF
            (13, [0]),         # l_shin: 1 DOF (knee)
            (14, [0, 1, 2]),   # l_foot: 3 DOF (ankle)
        ]

        dof_idx = 0
        for joint_idx, axes in joint_dof_map:
            if joint_idx < len(joint_rotations):
                rot_mat = _quat_to_rotation_matrix(joint_rotations[joint_idx])
                euler = _matrix_to_euler_xyz(rot_mat)
                for axis in axes:
                    dof_positions[dof_idx] = euler[axis]
                    dof_idx += 1
            else:
                dof_idx += len(axes)

        return dof_positions

    def get_policy_obs(self) -> np.ndarray:
        """Get temporally stacked observation for policy (oldest to newest).

        Returns:
            (policy_obs_dim,) = (num_policy_obs_steps * per_frame_obs_dim,)
        """
        return self._get_stacked_obs(self.config.num_policy_obs_steps)

    def get_encoder_obs(self) -> np.ndarray:
        """Get temporally stacked observation for encoder (oldest to newest).

        Returns:
            (encoder_obs_dim,) = (num_encoder_obs_steps * per_frame_obs_dim,)
        """
        return self._get_stacked_obs(self.config.num_encoder_obs_steps)

    def _get_stacked_obs(self, num_steps: int) -> np.ndarray:
        """Stack recent frames oldest-to-newest, zero-padding if insufficient history."""
        total_dim = num_steps * self.config.per_frame_obs_dim
        stacked = np.zeros(total_dim, dtype=np.float32)

        available = min(num_steps, len(self._history))
        for s in range(available):
            # Stack from oldest to newest: oldest goes first
            frame_idx = len(self._history) - available + s
            offset = s * self.config.per_frame_obs_dim
            stacked[offset:offset + self.config.per_frame_obs_dim] = self._history[frame_idx]

        return stacked
