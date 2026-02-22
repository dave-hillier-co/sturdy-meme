"""Tests for CALM observation extractor."""

import numpy as np
import pytest

from .config import HumanoidCALMConfig
from .observation import (
    CALMObservationExtractor,
    get_heading_angle,
    remove_heading,
    quat_to_tan_norm_6d,
    rotate_to_heading_frame,
)


@pytest.fixture
def config():
    return HumanoidCALMConfig()


@pytest.fixture
def extractor(config):
    return CALMObservationExtractor(config)


class TestDimensions:
    def test_per_frame_dim(self, config):
        # root_height(1) + rot_6d(6) + vel(3) + ang_vel(3) +
        # dof_pos(37) + dof_vel(37) + key_body_pos(15) = 102
        assert config.per_frame_obs_dim == 102

    def test_policy_obs_dim(self, config):
        assert config.policy_obs_dim == 204  # 2 * 102

    def test_encoder_obs_dim(self, config):
        assert config.encoder_obs_dim == 1020  # 10 * 102

    def test_per_frame_breakdown(self, config):
        dim = (1 + 6 + 3 + 3 +
               config.num_dof + config.num_dof +
               config.num_key_bodies * 3)
        assert dim == config.per_frame_obs_dim


class TestHeadingAngle:
    def test_identity_quaternion(self):
        """Identity quaternion faces forward (+Z), heading = 0."""
        q = np.array([1.0, 0.0, 0.0, 0.0])
        heading = get_heading_angle(q)
        assert abs(heading) < 1e-6

    def test_90_degree_yaw(self):
        """90 degree Y rotation: forward = +X, heading = atan2(1, 0) = pi/2."""
        angle = np.pi / 2
        q = np.array([np.cos(angle / 2), 0.0, np.sin(angle / 2), 0.0])
        heading = get_heading_angle(q)
        assert abs(heading - np.pi / 2) < 1e-5

    def test_180_degree_yaw(self):
        """180 degree Y rotation: forward = -Z, heading = atan2(0, -1) = pi."""
        angle = np.pi
        q = np.array([np.cos(angle / 2), 0.0, np.sin(angle / 2), 0.0])
        heading = get_heading_angle(q)
        assert abs(abs(heading) - np.pi) < 1e-5


class TestRemoveHeading:
    def test_identity(self):
        """Removing heading from identity should give identity."""
        q = np.array([1.0, 0.0, 0.0, 0.0])
        result = remove_heading(q)
        np.testing.assert_allclose(result, q, atol=1e-6)

    def test_pure_yaw(self):
        """Removing heading from pure yaw rotation should give identity."""
        angle = 0.7
        q = np.array([np.cos(angle / 2), 0.0, np.sin(angle / 2), 0.0])
        result = remove_heading(q)
        # Result should be close to identity (no heading component)
        np.testing.assert_allclose(abs(result[0]), 1.0, atol=1e-5)

    def test_pure_pitch_unchanged(self):
        """Pure X rotation should be preserved by heading removal."""
        angle = 0.5
        q = np.array([np.cos(angle / 2), np.sin(angle / 2), 0.0, 0.0])
        result = remove_heading(q)
        # Should be approximately the same since heading is 0
        np.testing.assert_allclose(result, q, atol=1e-5)


class TestQuatTo6D:
    def test_identity(self):
        """Identity quaternion -> first two columns of identity matrix."""
        q = np.array([1.0, 0.0, 0.0, 0.0])
        result = quat_to_tan_norm_6d(q)
        # Column 0 = [1,0,0], Column 1 = [0,1,0]
        expected = np.array([1.0, 0.0, 0.0, 0.0, 1.0, 0.0])
        np.testing.assert_allclose(result, expected, atol=1e-6)

    def test_90_yaw(self):
        """90 degree Y rotation."""
        angle = np.pi / 2
        q = np.array([np.cos(angle / 2), 0.0, np.sin(angle / 2), 0.0])
        result = quat_to_tan_norm_6d(q)
        # Col 0 = [0, 0, -1], Col 1 = [0, 1, 0]
        expected = np.array([0.0, 0.0, -1.0, 0.0, 1.0, 0.0])
        np.testing.assert_allclose(result, expected, atol=1e-5)


class TestHeadingFrameRotation:
    def test_zero_heading(self):
        """Zero heading should not change the vector."""
        vec = np.array([1.0, 2.0, 3.0])
        result = rotate_to_heading_frame(vec, 0.0)
        np.testing.assert_allclose(result, vec, atol=1e-6)

    def test_90_heading(self):
        """90 degree heading: X -> -Z, Z -> X (in heading frame)."""
        vec = np.array([1.0, 0.0, 0.0])
        result = rotate_to_heading_frame(vec, np.pi / 2)
        # cos(-pi/2)*1 + sin(-pi/2)*0 = 0
        # y stays 0
        # -sin(-pi/2)*1 + cos(-pi/2)*0 = 1
        expected = np.array([0.0, 0.0, 1.0])
        np.testing.assert_allclose(result, expected, atol=1e-5)

    def test_y_component_unchanged(self):
        """Y component should always be preserved."""
        vec = np.array([1.0, 5.0, 1.0])
        for heading in [0.0, 0.5, 1.0, -1.0, np.pi]:
            result = rotate_to_heading_frame(vec, heading)
            assert abs(result[1] - 5.0) < 1e-6


class TestExtractor:
    def test_single_frame_shape(self, extractor, config):
        """Single frame extraction produces correct shape."""
        obs = extractor.extract_frame(
            root_pos=np.array([0.0, 1.0, 0.0]),
            root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
            root_vel=np.zeros(3),
            root_ang_vel=np.zeros(3),
            dof_positions=np.zeros(config.num_dof),
            key_body_positions=np.zeros((config.num_key_bodies, 3)),
            delta_time=1.0 / 60.0,
        )
        assert obs.shape == (config.per_frame_obs_dim,)

    def test_root_height(self, extractor, config):
        """Root height is in index 0."""
        obs = extractor.extract_frame(
            root_pos=np.array([0.0, 1.5, 0.0]),
            root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
            root_vel=np.zeros(3),
            root_ang_vel=np.zeros(3),
            dof_positions=np.zeros(config.num_dof),
            key_body_positions=np.zeros((config.num_key_bodies, 3)),
            delta_time=1.0 / 60.0,
        )
        assert abs(obs[0] - 1.5) < 1e-6

    def test_policy_obs_dim(self, extractor, config):
        """Policy obs has correct dimension after stacking."""
        for _ in range(3):
            extractor.extract_frame(
                root_pos=np.array([0.0, 1.0, 0.0]),
                root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
                root_vel=np.zeros(3),
                root_ang_vel=np.zeros(3),
                dof_positions=np.zeros(config.num_dof),
                key_body_positions=np.zeros((config.num_key_bodies, 3)),
                delta_time=1.0 / 60.0,
            )
        policy_obs = extractor.get_policy_obs()
        assert policy_obs.shape == (config.policy_obs_dim,)

    def test_encoder_obs_dim(self, extractor, config):
        """Encoder obs has correct dimension."""
        for _ in range(12):
            extractor.extract_frame(
                root_pos=np.array([0.0, 1.0, 0.0]),
                root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
                root_vel=np.zeros(3),
                root_ang_vel=np.zeros(3),
                dof_positions=np.zeros(config.num_dof),
                key_body_positions=np.zeros((config.num_key_bodies, 3)),
                delta_time=1.0 / 60.0,
            )
        encoder_obs = extractor.get_encoder_obs()
        assert encoder_obs.shape == (config.encoder_obs_dim,)

    def test_stacking_order(self, extractor, config):
        """Stacked obs should be oldest-to-newest."""
        for i in range(3):
            root_pos = np.array([0.0, float(i + 1), 0.0])
            extractor.extract_frame(
                root_pos=root_pos,
                root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
                root_vel=np.zeros(3),
                root_ang_vel=np.zeros(3),
                dof_positions=np.zeros(config.num_dof),
                key_body_positions=np.zeros((config.num_key_bodies, 3)),
                delta_time=1.0 / 60.0,
            )
        policy_obs = extractor.get_policy_obs()
        # Frame 0 (older) should be index 1 (height=2.0)
        # Frame 1 (newer) should be index 1+102 (height=3.0)
        assert abs(policy_obs[0] - 2.0) < 1e-6
        assert abs(policy_obs[config.per_frame_obs_dim] - 3.0) < 1e-6

    def test_reset_clears_history(self, extractor, config):
        """Reset should clear the history buffer."""
        extractor.extract_frame(
            root_pos=np.array([0.0, 1.0, 0.0]),
            root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
            root_vel=np.zeros(3),
            root_ang_vel=np.zeros(3),
            dof_positions=np.zeros(config.num_dof),
            key_body_positions=np.zeros((config.num_key_bodies, 3)),
            delta_time=1.0 / 60.0,
        )
        extractor.reset()
        policy_obs = extractor.get_policy_obs()
        # Should be all zeros after reset
        np.testing.assert_allclose(policy_obs, 0.0, atol=1e-8)

    def test_dof_velocity_finite_diff(self, extractor, config):
        """DOF velocities should be computed via finite differences."""
        dt = 1.0 / 60.0
        dof1 = np.zeros(config.num_dof, dtype=np.float32)
        dof2 = np.ones(config.num_dof, dtype=np.float32) * 0.1

        extractor.extract_frame(
            root_pos=np.zeros(3), root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
            root_vel=np.zeros(3), root_ang_vel=np.zeros(3),
            dof_positions=dof1,
            key_body_positions=np.zeros((config.num_key_bodies, 3)),
            delta_time=dt,
        )
        obs = extractor.extract_frame(
            root_pos=np.zeros(3), root_rot=np.array([1.0, 0.0, 0.0, 0.0]),
            root_vel=np.zeros(3), root_ang_vel=np.zeros(3),
            dof_positions=dof2,
            key_body_positions=np.zeros((config.num_key_bodies, 3)),
            delta_time=dt,
        )
        # DOF velocities are at offset 1+6+3+3+37 = 50, length 37
        dof_vel_start = 1 + 6 + 3 + 3 + config.num_dof
        dof_vel = obs[dof_vel_start:dof_vel_start + config.num_dof]
        expected_vel = (dof2 - dof1) / dt
        np.testing.assert_allclose(dof_vel, expected_vel, atol=1e-4)
