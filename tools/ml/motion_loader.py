"""Load motion capture data from BVH and JSON files.

Parses motion files into a dictionary format:
    {
        "fps": float,
        "frames": [
            {
                "root_pos": ndarray (3,),
                "root_rot": ndarray (4,),  # w,x,y,z
                "joint_positions": ndarray (J, 3),
                "joint_rotations": ndarray (J, 4),
            },
            ...
        ]
    }
"""

import json
import numpy as np
from pathlib import Path
from typing import Dict, Any

from .quaternion import euler_to_quat


def load_bvh(path: str, num_joints: int) -> Dict[str, Any]:
    """Load a BVH motion capture file.

    Handles the standard BVH format (e.g. CMU Mocap database).
    """
    with open(path, "r") as f:
        content = f.read()

    lines = content.strip().split("\n")
    idx = 0

    # Parse hierarchy
    joint_names = []
    joint_offsets = []
    joint_parents = []
    parent_stack = [-1]

    while idx < len(lines):
        line = lines[idx].strip()
        if line.startswith("ROOT") or line.startswith("JOINT"):
            name = line.split()[-1]
            joint_names.append(name)
            joint_parents.append(parent_stack[-1])
        elif line.startswith("End Site"):
            idx += 1
            while idx < len(lines) and "}" not in lines[idx]:
                idx += 1
            idx += 1
            continue
        elif line.startswith("OFFSET"):
            parts = line.split()
            joint_offsets.append([float(parts[1]), float(parts[2]), float(parts[3])])
        elif line == "{":
            if joint_names:
                parent_stack.append(len(joint_names) - 1)
        elif line == "}":
            parent_stack.pop()
        elif line.startswith("MOTION"):
            idx += 1
            break
        idx += 1

    # Parse motion data header
    num_frames = 0
    frame_time = 1.0 / 30.0

    while idx < len(lines):
        line = lines[idx].strip()
        if line.startswith("Frames:"):
            num_frames = int(line.split(":")[1].strip())
        elif line.startswith("Frame Time:"):
            frame_time = float(line.split(":")[1].strip())
            idx += 1
            break
        idx += 1

    fps = 1.0 / frame_time
    frames = []

    for _ in range(num_frames):
        if idx >= len(lines):
            break
        values = [float(v) for v in lines[idx].strip().split()]
        idx += 1

        root_pos = np.array(values[0:3], dtype=np.float32) * 0.01  # cm to m

        rot_offset = 3
        joint_rotations = np.zeros((num_joints, 4), dtype=np.float32)
        joint_rotations[:, 0] = 1.0  # identity

        for j in range(min(len(joint_names), num_joints)):
            if rot_offset + 3 <= len(values):
                rx = np.radians(values[rot_offset])
                ry = np.radians(values[rot_offset + 1])
                rz = np.radians(values[rot_offset + 2])
                joint_rotations[j] = euler_to_quat(rx, ry, rz)
                rot_offset += 3

        # Forward kinematics for joint positions
        joint_positions = np.zeros((num_joints, 3), dtype=np.float32)
        joint_positions[0] = root_pos
        offsets = np.array(joint_offsets[:num_joints], dtype=np.float32) * 0.01

        for j in range(1, min(len(joint_names), num_joints)):
            parent = joint_parents[j]
            if 0 <= parent < num_joints:
                joint_positions[j] = joint_positions[parent] + offsets[j]

        frames.append({
            "root_pos": root_pos,
            "root_rot": joint_rotations[0],
            "joint_positions": joint_positions,
            "joint_rotations": joint_rotations,
        })

    return {"fps": fps, "frames": frames}


def load_json(path: str, num_joints: int) -> Dict[str, Any]:
    """Load a JSON motion file (as produced by fetch_training_data.py).

    The JSON format stores lists of floats; this converts them back to ndarrays.
    """
    with open(path, "r") as f:
        data = json.load(f)

    frames = []
    for raw in data["frames"]:
        jp = np.array(raw["joint_positions"], dtype=np.float32)
        jr = np.array(raw["joint_rotations"], dtype=np.float32)

        # Pad or trim to num_joints
        if jp.shape[0] < num_joints:
            pad_p = np.zeros((num_joints - jp.shape[0], 3), dtype=np.float32)
            jp = np.concatenate([jp, pad_p], axis=0)
            pad_r = np.tile(
                np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                (num_joints - jr.shape[0], 1),
            )
            jr = np.concatenate([jr, pad_r], axis=0)
        else:
            jp = jp[:num_joints]
            jr = jr[:num_joints]

        frames.append({
            "root_pos": np.array(raw["root_pos"], dtype=np.float32),
            "root_rot": np.array(raw["root_rot"], dtype=np.float32),
            "joint_positions": jp,
            "joint_rotations": jr,
        })

    return {"fps": float(data["fps"]), "frames": frames}


def load_motion_directory(
    motion_dir: str,
    num_joints: int,
    extensions: tuple = (".bvh", ".json"),
) -> Dict[str, Dict[str, Any]]:
    """Load all motion files from a directory."""
    motion_dir = Path(motion_dir)
    motions = {}

    if not motion_dir.exists():
        return motions

    for ext in extensions:
        for filepath in motion_dir.glob(f"**/*{ext}"):
            name = filepath.stem
            try:
                if ext == ".bvh":
                    motions[name] = load_bvh(str(filepath), num_joints)
                elif ext == ".json":
                    motions[name] = load_json(str(filepath), num_joints)
            except Exception as e:
                print(f"Warning: failed to load {filepath}: {e}")

    return motions


def generate_standing_motion(
    num_joints: int,
    duration_seconds: float = 5.0,
    fps: float = 60.0,
) -> Dict[str, Any]:
    """Generate a standing-still motion clip for testing."""
    num_frames = int(duration_seconds * fps)
    frames = []

    for _ in range(num_frames):
        frames.append({
            "root_pos": np.array([0.0, 1.0, 0.0], dtype=np.float32),
            "root_rot": np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
            "joint_positions": np.zeros((num_joints, 3), dtype=np.float32),
            "joint_rotations": np.tile(
                np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                (num_joints, 1),
            ),
        })

    return {"fps": fps, "frames": frames}
