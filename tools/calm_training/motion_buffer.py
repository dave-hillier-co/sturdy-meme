"""Motion transition buffer for AMP discriminator training.

Pre-extracts (obs_t, obs_t1) transition pairs from motion clips using
CALMObservationExtractor. Provides random sampling for discriminator training.
"""

import numpy as np
from typing import Dict, Any, Tuple

from .config import HumanoidCALMConfig
from .observation import CALMObservationExtractor


class MotionTransitionBuffer:
    """Stores pre-extracted (obs_t, obs_t1) pairs from motion clips.

    Used as the 'real' data source for AMP discriminator training.
    """

    def __init__(self, humanoid: HumanoidCALMConfig):
        self.humanoid = humanoid
        self.obs_t: np.ndarray = np.empty(
            (0, humanoid.per_frame_obs_dim), dtype=np.float32
        )
        self.obs_t1: np.ndarray = np.empty(
            (0, humanoid.per_frame_obs_dim), dtype=np.float32
        )

    def extract_from_motions(
        self,
        motions: Dict[str, Any],
        dt: float = 1.0 / 60.0,
    ) -> int:
        """Extract transition pairs from all motion clips.

        Args:
            motions: dict of motion clips (from motion_loader)
            dt: timestep between frames

        Returns:
            Number of transitions extracted.
        """
        all_obs_t = []
        all_obs_t1 = []

        for clip_name, clip in motions.items():
            frames = clip["frames"]
            fps = clip["fps"]
            clip_dt = 1.0 / fps if fps > 0 else dt

            extractor = CALMObservationExtractor(self.humanoid)

            # Extract per-frame observations
            frame_obs = []
            for i, frame in enumerate(frames):
                root_pos = np.array(frame["root_pos"], dtype=np.float32)
                root_rot = np.array(frame["root_rot"], dtype=np.float32)
                joint_rotations = np.array(
                    frame["joint_rotations"], dtype=np.float32
                )

                # Key body positions (use joint_positions if available)
                if "joint_positions" in frame:
                    all_joint_pos = np.array(
                        frame["joint_positions"], dtype=np.float32
                    )
                    # Pick key bodies (head=4, r_hand=13, l_hand=9,
                    # r_foot=19, l_foot=16)
                    key_indices = [4, 13, 9, 19, 16]
                    key_body_pos = np.zeros(
                        (self.humanoid.num_key_bodies, 3), dtype=np.float32
                    )
                    for k, idx in enumerate(key_indices):
                        if idx < len(all_joint_pos):
                            key_body_pos[k] = all_joint_pos[idx]
                else:
                    key_body_pos = np.zeros(
                        (self.humanoid.num_key_bodies, 3), dtype=np.float32
                    )

                prev_root_pos = (
                    np.array(frames[i - 1]["root_pos"], dtype=np.float32)
                    if i > 0
                    else None
                )
                prev_root_rot = (
                    np.array(frames[i - 1]["root_rot"], dtype=np.float32)
                    if i > 0
                    else None
                )

                obs = extractor.extract_frame_from_motion(
                    root_pos=root_pos,
                    root_rot=root_rot,
                    joint_rotations=joint_rotations,
                    key_body_positions=key_body_pos,
                    delta_time=clip_dt,
                    prev_root_pos=prev_root_pos,
                    prev_root_rot=prev_root_rot,
                )
                frame_obs.append(obs)

            # Create consecutive transition pairs
            for i in range(len(frame_obs) - 1):
                all_obs_t.append(frame_obs[i])
                all_obs_t1.append(frame_obs[i + 1])

        if all_obs_t:
            new_obs_t = np.stack(all_obs_t)
            new_obs_t1 = np.stack(all_obs_t1)

            # Filter out any transition pairs containing NaN
            valid_t = np.isfinite(new_obs_t).all(axis=1)
            valid_t1 = np.isfinite(new_obs_t1).all(axis=1)
            valid = valid_t & valid_t1
            removed = (~valid).sum()
            if removed > 0:
                print(f"  Removed {removed} transitions with NaN/Inf values")
            new_obs_t = new_obs_t[valid]
            new_obs_t1 = new_obs_t1[valid]

            self.obs_t = np.concatenate([self.obs_t, new_obs_t], axis=0)
            self.obs_t1 = np.concatenate([self.obs_t1, new_obs_t1], axis=0)

        return len(self.obs_t)

    def sample(
        self, batch_size: int, rng: np.random.RandomState = None
    ) -> Tuple[np.ndarray, np.ndarray]:
        """Sample random transition pairs.

        Returns:
            obs_t: (batch_size, per_frame_obs_dim)
            obs_t1: (batch_size, per_frame_obs_dim)
        """
        if rng is None:
            rng = np.random.RandomState()

        n = len(self.obs_t)
        if n == 0:
            return (
                np.zeros(
                    (batch_size, self.humanoid.per_frame_obs_dim),
                    dtype=np.float32,
                ),
                np.zeros(
                    (batch_size, self.humanoid.per_frame_obs_dim),
                    dtype=np.float32,
                ),
            )

        indices = rng.randint(0, n, size=batch_size)
        return self.obs_t[indices], self.obs_t1[indices]

    def __len__(self) -> int:
        return len(self.obs_t)

    def get_stacked_encoder_obs(
        self, num_steps: int = 10
    ) -> np.ndarray:
        """Get stacked observations suitable for encoder training.

        Extracts windows of consecutive frames from the stored transitions.

        Returns:
            (num_windows, num_steps * per_frame_obs_dim)
        """
        n = len(self.obs_t)
        if n < num_steps:
            return np.zeros(
                (0, num_steps * self.humanoid.per_frame_obs_dim),
                dtype=np.float32,
            )

        # Use sliding windows over stored observations
        # obs_t contains consecutive frames within each clip
        windows = []
        for start in range(0, n - num_steps + 1, num_steps):
            window = np.concatenate(
                [self.obs_t[start + i] for i in range(num_steps)]
            )
            windows.append(window)

        if not windows:
            return np.zeros(
                (0, num_steps * self.humanoid.per_frame_obs_dim),
                dtype=np.float32,
            )

        return np.stack(windows)
