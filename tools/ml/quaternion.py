"""Quaternion math utilities (w,x,y,z convention).

Pure NumPy implementations for use in training and data processing.
All quaternions use (w,x,y,z) scalar-first ordering.
"""

import numpy as np


def quat_yaw(q: np.ndarray) -> float:
    """Extract yaw angle from quaternion (Y-up convention)."""
    w, x, y, z = q
    return np.arctan2(2.0 * (w * y + x * z), 1.0 - 2.0 * (y * y + z * z))


def quat_from_axis_angle(axis: np.ndarray, angle: float) -> np.ndarray:
    """Create quaternion from axis-angle representation."""
    half = angle * 0.5
    s = np.sin(half)
    return np.array([np.cos(half), axis[0] * s, axis[1] * s, axis[2] * s])


def quat_inverse(q: np.ndarray) -> np.ndarray:
    """Inverse of unit quaternion."""
    return np.array([q[0], -q[1], -q[2], -q[3]])


def quat_mul(a: np.ndarray, b: np.ndarray) -> np.ndarray:
    """Quaternion multiplication."""
    aw, ax, ay, az = a
    bw, bx, by, bz = b
    return np.array([
        aw * bw - ax * bx - ay * by - az * bz,
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
    ])


def quat_rotate_vec(q: np.ndarray, v: np.ndarray) -> np.ndarray:
    """Rotate vector v by quaternion q."""
    qv = np.array([0.0, v[0], v[1], v[2]])
    result = quat_mul(quat_mul(q, qv), quat_inverse(q))
    return result[1:4]


def euler_to_quat(rx: float, ry: float, rz: float) -> np.ndarray:
    """Convert Euler angles (radians, ZYX order) to quaternion."""
    cx, sx = np.cos(rx / 2), np.sin(rx / 2)
    cy, sy = np.cos(ry / 2), np.sin(ry / 2)
    cz, sz = np.cos(rz / 2), np.sin(rz / 2)

    w = cx * cy * cz + sx * sy * sz
    x = sx * cy * cz - cx * sy * sz
    y = cx * sy * cz + sx * cy * sz
    z = cx * cy * sz - sx * sy * cz

    return np.array([w, x, y, z], dtype=np.float32)


def slerp(q0: np.ndarray, q1: np.ndarray, t: float) -> np.ndarray:
    """Spherical linear interpolation between quaternions."""
    dot = np.dot(q0, q1)
    if dot < 0:
        q1 = -q1
        dot = -dot
    dot = np.clip(dot, -1.0, 1.0)
    if dot > 0.9995:
        result = q0 + t * (q1 - q0)
        return result / np.linalg.norm(result)
    theta = np.arccos(dot)
    sin_theta = np.sin(theta)
    return (np.sin((1 - t) * theta) * q0 + np.sin(t * theta) * q1) / sin_theta
