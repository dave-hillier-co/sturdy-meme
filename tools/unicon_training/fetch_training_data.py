#!/usr/bin/env python3
"""Convert Mixamo FBX files to the motion dict JSON format for training.

Reads FBX animation files, extracts skeleton poses per frame, maps Mixamo
joints to the 20-body ArticulatedBody humanoid, and writes JSON files
that tools/ml/motion_loader.py can consume.

Usage:
    # Convert all FBX files in a directory:
    python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/

    # Convert a single file:
    python tools/unicon_training/fetch_training_data.py my_anim.fbx -o assets/motions/

    # List joints in an FBX without converting (useful for debugging):
    python tools/unicon_training/fetch_training_data.py "assets/characters/fbx/Y Bot.fbx" --list-joints

    # Convert with a specific target FPS:
    python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/ --fps 60

Requirements:
    pip install pyassimp numpy
"""

import argparse
import json
import sys
from pathlib import Path
from typing import Dict, List, Optional, Any

import numpy as np

try:
    import pyassimp
    import pyassimp.postprocess as pp
except ImportError:
    print("Error: pyassimp is required. Install with: pip install pyassimp")
    print("You also need the assimp shared library installed on your system.")
    print("  Ubuntu/Debian: sudo apt install libassimp-dev")
    print("  macOS: brew install assimp")
    sys.exit(1)

from tools.ml.quaternion import quat_mul, quat_inverse, quat_from_axis_angle


# ============================================================================
# Mixamo joint name -> ArticulatedBody 20-part index mapping
# Matches the C++ ArticulatedBody::createHumanoidConfig() exactly.
# ============================================================================
HUMANOID_PARTS = [
    # idx  canonical name      Mixamo bone name (after stripping "mixamorig:" prefix)
    (0,  "Pelvis",            ["Hips"]),
    (1,  "LowerSpine",        ["Spine"]),
    (2,  "UpperSpine",        ["Spine1"]),
    (3,  "Chest",             ["Spine2"]),
    (4,  "Neck",              ["Neck"]),
    (5,  "Head",              ["Head"]),
    (6,  "LeftShoulder",      ["LeftShoulder"]),
    (7,  "LeftUpperArm",      ["LeftArm"]),
    (8,  "LeftForearm",       ["LeftForeArm"]),
    (9,  "LeftHand",          ["LeftHand"]),
    (10, "RightShoulder",     ["RightShoulder"]),
    (11, "RightUpperArm",     ["RightArm"]),
    (12, "RightForearm",      ["RightForeArm"]),
    (13, "RightHand",         ["RightHand"]),
    (14, "LeftThigh",         ["LeftUpLeg"]),
    (15, "LeftShin",          ["LeftLeg"]),
    (16, "LeftFoot",          ["LeftFoot"]),
    (17, "RightThigh",        ["RightUpLeg"]),
    (18, "RightShin",         ["RightLeg"]),
    (19, "RightFoot",         ["RightFoot"]),
]

NUM_PARTS = len(HUMANOID_PARTS)


def _build_mixamo_name_map() -> Dict[str, int]:
    """Build a lookup from Mixamo bone name -> part index."""
    name_map = {}
    for idx, canonical, aliases in HUMANOID_PARTS:
        name_map[canonical.lower()] = idx
        for alias in aliases:
            name_map[alias.lower()] = idx
            # Also handle the "mixamorig:" prefix
            name_map[f"mixamorig:{alias}".lower()] = idx
    return name_map

MIXAMO_NAME_MAP = _build_mixamo_name_map()


def _to_str(value) -> str:
    """Convert pyassimp string/bytes/struct to Python str."""
    if isinstance(value, str):
        return value
    if isinstance(value, bytes):
        return value.decode('utf-8')
    if hasattr(value, 'data'):
        return value.data.decode('utf-8')
    return str(value)


def _mat4_to_pos_rot(m) -> tuple:
    """Extract position and rotation (w,x,y,z) from a 4x4 matrix."""
    pos = np.array([m[0][3], m[1][3], m[2][3]], dtype=np.float32)

    # Extract 3x3 rotation and convert to quaternion
    rot_mat = np.array([
        [m[0][0], m[0][1], m[0][2]],
        [m[1][0], m[1][1], m[1][2]],
        [m[2][0], m[2][1], m[2][2]],
    ], dtype=np.float64)

    # Normalize to remove scale
    for i in range(3):
        n = np.linalg.norm(rot_mat[:, i])
        if n > 1e-6:
            rot_mat[:, i] /= n

    quat = _rotation_matrix_to_quat(rot_mat)
    return pos, quat.astype(np.float32)


def _rotation_matrix_to_quat(m: np.ndarray) -> np.ndarray:
    """Convert 3x3 rotation matrix to quaternion (w,x,y,z)."""
    trace = m[0, 0] + m[1, 1] + m[2, 2]

    if trace > 0:
        s = 0.5 / np.sqrt(trace + 1.0)
        w = 0.25 / s
        x = (m[2, 1] - m[1, 2]) * s
        y = (m[0, 2] - m[2, 0]) * s
        z = (m[1, 0] - m[0, 1]) * s
    elif m[0, 0] > m[1, 1] and m[0, 0] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[0, 0] - m[1, 1] - m[2, 2])
        w = (m[2, 1] - m[1, 2]) / s
        x = 0.25 * s
        y = (m[0, 1] + m[1, 0]) / s
        z = (m[0, 2] + m[2, 0]) / s
    elif m[1, 1] > m[2, 2]:
        s = 2.0 * np.sqrt(1.0 + m[1, 1] - m[0, 0] - m[2, 2])
        w = (m[0, 2] - m[2, 0]) / s
        x = (m[0, 1] + m[1, 0]) / s
        y = 0.25 * s
        z = (m[1, 2] + m[2, 1]) / s
    else:
        s = 2.0 * np.sqrt(1.0 + m[2, 2] - m[0, 0] - m[1, 1])
        w = (m[1, 0] - m[0, 1]) / s
        x = (m[0, 2] + m[2, 0]) / s
        y = (m[1, 2] + m[2, 1]) / s
        z = 0.25 * s

    q = np.array([w, x, y, z])
    return q / np.linalg.norm(q)


def _find_node_by_name(node, name: str):
    """Recursively find a scene node by name."""
    if _to_str(node.name) == name:
        return node
    for child in node.children:
        result = _find_node_by_name(child, name)
        if result is not None:
            return result
    return None


def _collect_nodes(node, parent_transform=None) -> Dict[str, tuple]:
    """Walk the scene graph and collect world transforms for every node."""
    if parent_transform is None:
        parent_transform = np.eye(4, dtype=np.float64)

    # pyassimp gives us node.transformation as a 4x4 matrix
    local = np.array(node.transformation, dtype=np.float64)
    world = parent_transform @ local

    name = _to_str(node.name)

    result = {name: world}
    for child in node.children:
        result.update(_collect_nodes(child, world))
    return result


def list_joints(fbx_path: str):
    """Print all joint/bone names found in an FBX file."""
    with pyassimp.load(fbx_path) as scene:
        nodes = _collect_nodes(scene.rootnode)
        print(f"\nJoints in {fbx_path} ({len(nodes)} nodes):")
        for name in sorted(nodes.keys()):
            normalized = name.lower()
            part_idx = MIXAMO_NAME_MAP.get(normalized)
            mapped = f" -> part[{part_idx}] {HUMANOID_PARTS[part_idx][1]}" if part_idx is not None else ""
            print(f"  {name}{mapped}")

        # Show mapping coverage
        mapped_count = sum(1 for n in nodes if n.lower() in MIXAMO_NAME_MAP)
        print(f"\nMapped: {mapped_count}/{NUM_PARTS} humanoid parts")
        missing = []
        found_indices = set()
        for n in nodes:
            idx = MIXAMO_NAME_MAP.get(n.lower())
            if idx is not None:
                found_indices.add(idx)
        for idx, canonical, _ in HUMANOID_PARTS:
            if idx not in found_indices:
                missing.append(canonical)
        if missing:
            print(f"Missing: {', '.join(missing)}")


def convert_fbx(fbx_path: str, target_fps: float = 30.0,
                scale: float = 0.01) -> Optional[Dict[str, Any]]:
    """Convert a single FBX file to motion dict format.

    Args:
        fbx_path: path to .fbx file
        target_fps: output frame rate (Mixamo default is 30)
        scale: scale factor (Mixamo uses cm, we need meters)

    Returns:
        Motion dict or None if no animations found.
    """
    flags = (pp.aiProcess_Triangulate |
             pp.aiProcess_SortByPType |
             pp.aiProcess_LimitBoneWeights)

    with pyassimp.load(fbx_path, processing=flags) as scene:
        if not scene.animations:
            print(f"  Warning: {fbx_path} has no animations, skipping")
            return None

        anim = scene.animations[0]
        duration = anim.duration
        ticks_per_sec = anim.tickspersecond if anim.tickspersecond > 0 else 24.0
        duration_sec = duration / ticks_per_sec

        # Build channel lookup by bone name
        channels = {}
        for channel in anim.channels:
            name = _to_str(channel.nodename)
            channels[name] = channel

        # Sample at target FPS
        num_frames = max(1, int(duration_sec * target_fps))
        frames = []

        for frame_idx in range(num_frames):
            t = (frame_idx / max(num_frames - 1, 1)) * duration  # in ticks

            # Apply animation to the scene graph
            node_transforms = _evaluate_animation(scene.rootnode, channels, t, scale)

            # Extract the 20-part humanoid pose
            joint_positions = np.zeros((NUM_PARTS, 3), dtype=np.float32)
            joint_rotations = np.tile(
                np.array([1.0, 0.0, 0.0, 0.0], dtype=np.float32),
                (NUM_PARTS, 1),
            )

            for node_name, (pos, rot) in node_transforms.items():
                normalized = node_name.lower()
                part_idx = MIXAMO_NAME_MAP.get(normalized)
                if part_idx is not None:
                    joint_positions[part_idx] = pos
                    joint_rotations[part_idx] = rot

            root_pos = joint_positions[0].copy()
            root_rot = joint_rotations[0].copy()

            frames.append({
                "root_pos": root_pos.tolist(),
                "root_rot": root_rot.tolist(),
                "joint_positions": joint_positions.tolist(),
                "joint_rotations": joint_rotations.tolist(),
            })

        return {
            "fps": target_fps,
            "duration": duration_sec,
            "num_frames": num_frames,
            "source": Path(fbx_path).name,
            "frames": frames,
        }


def _evaluate_animation(root_node, channels: dict, time_ticks: float,
                        scale: float, parent_world=None) -> Dict[str, tuple]:
    """Evaluate the animation at a given time, returning world-space transforms."""
    if parent_world is None:
        parent_world = np.eye(4, dtype=np.float64)

    name = _to_str(root_node.name)

    # Start with the node's default transform
    local = np.array(root_node.transformation, dtype=np.float64)

    # Override with animation channel if present
    if name in channels:
        ch = channels[name]
        local = _sample_channel(ch, time_ticks)

    world = parent_world @ local

    # Extract position and rotation
    pos, rot = _mat4_to_pos_rot(world)
    pos *= scale  # cm to meters

    result = {name: (pos, rot)}

    for child in root_node.children:
        result.update(_evaluate_animation(child, channels, time_ticks, scale, world))

    return result


def _sample_channel(channel, time: float) -> np.ndarray:
    """Sample an animation channel at a given time, returning a 4x4 local matrix."""
    mat = np.eye(4, dtype=np.float64)

    # Sample translation
    if len(channel.positionkeys) > 0:
        pos = _interpolate_vec3_keys(channel.positionkeys, time)
        mat[0, 3] = pos[0]
        mat[1, 3] = pos[1]
        mat[2, 3] = pos[2]

    # Sample rotation
    if len(channel.rotationkeys) > 0:
        quat = _interpolate_quat_keys(channel.rotationkeys, time)
        rot_mat = _quat_to_rotation_matrix(quat)
        mat[:3, :3] = rot_mat

    # Sample scale
    if len(channel.scalingkeys) > 0:
        scl = _interpolate_vec3_keys(channel.scalingkeys, time)
        mat[0, :3] *= scl[0]
        mat[1, :3] *= scl[1]
        mat[2, :3] *= scl[2]

    return mat


def _interpolate_vec3_keys(keys, time: float) -> np.ndarray:
    """Interpolate between vector3 keyframes."""
    if len(keys) == 1:
        return np.asarray(keys[0].value, dtype=np.float64)[:3]

    # Find the two surrounding keys
    for i in range(len(keys) - 1):
        if time <= keys[i + 1].time:
            k0, k1 = keys[i], keys[i + 1]
            dt = k1.time - k0.time
            alpha = (time - k0.time) / dt if dt > 0 else 0.0
            v0 = np.asarray(k0.value, dtype=np.float64)[:3]
            v1 = np.asarray(k1.value, dtype=np.float64)[:3]
            return v0 + alpha * (v1 - v0)

    # Past the last key
    return np.asarray(keys[-1].value, dtype=np.float64)[:3]


def _interpolate_quat_keys(keys, time: float) -> np.ndarray:
    """Interpolate between quaternion keyframes. Returns (w,x,y,z)."""
    if len(keys) == 1:
        # pyassimp quaternion value is already ndarray (w, x, y, z)
        return np.asarray(keys[0].value, dtype=np.float64)[:4]

    for i in range(len(keys) - 1):
        if time <= keys[i + 1].time:
            k0, k1 = keys[i], keys[i + 1]
            dt = k1.time - k0.time
            alpha = (time - k0.time) / dt if dt > 0 else 0.0
            q0 = np.asarray(k0.value, dtype=np.float64)[:4]
            q1 = np.asarray(k1.value, dtype=np.float64)[:4]
            return _slerp(q0, q1, alpha)

    return np.asarray(keys[-1].value, dtype=np.float64)[:4]


def _slerp(q0: np.ndarray, q1: np.ndarray, t: float) -> np.ndarray:
    """Quaternion slerp."""
    dot = np.dot(q0, q1)
    if dot < 0:
        q1 = -q1
        dot = -dot
    if dot > 0.9995:
        result = q0 + t * (q1 - q0)
        return result / np.linalg.norm(result)
    theta = np.arccos(np.clip(dot, -1, 1))
    sin_theta = np.sin(theta)
    return (np.sin((1 - t) * theta) * q0 + np.sin(t * theta) * q1) / sin_theta


def _quat_to_rotation_matrix(q: np.ndarray) -> np.ndarray:
    """Convert quaternion (w,x,y,z) to 3x3 rotation matrix."""
    w, x, y, z = q
    return np.array([
        [1 - 2*(y*y + z*z),     2*(x*y - w*z),     2*(x*z + w*y)],
        [    2*(x*y + w*z), 1 - 2*(x*x + z*z),     2*(y*z - w*x)],
        [    2*(x*z - w*y),     2*(y*z + w*x), 1 - 2*(x*x + y*y)],
    ], dtype=np.float64)


def convert_directory(input_dir: str, output_dir: str,
                      target_fps: float = 30.0, scale: float = 0.01):
    """Convert all FBX files in a directory to motion JSON files."""
    input_path = Path(input_dir)
    output_path = Path(output_dir)
    output_path.mkdir(parents=True, exist_ok=True)

    fbx_files = sorted(input_path.glob("*.fbx")) + sorted(input_path.glob("*.FBX"))

    if not fbx_files:
        print(f"No FBX files found in {input_dir}")
        return

    print(f"Found {len(fbx_files)} FBX files in {input_dir}")
    print(f"Output directory: {output_dir}")
    print(f"Target FPS: {target_fps}")
    print()

    converted = 0
    skipped = 0
    errors = 0

    for fbx_file in fbx_files:
        name = fbx_file.stem
        out_file = output_path / f"{name}.json"

        print(f"Converting: {fbx_file.name} ... ", end="", flush=True)

        try:
            motion = convert_fbx(str(fbx_file), target_fps, scale)
            if motion is None:
                skipped += 1
                continue

            with open(out_file, "w") as f:
                json.dump(motion, f)

            size_kb = out_file.stat().st_size / 1024
            print(f"OK ({motion['num_frames']} frames, {size_kb:.0f} KB)")
            converted += 1

        except Exception as e:
            print(f"ERROR: {e}")
            errors += 1

    print(f"\nDone: {converted} converted, {skipped} skipped, {errors} errors")


def main():
    parser = argparse.ArgumentParser(
        description="Convert Mixamo FBX files to training motion data",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  # Convert all FBX in a directory:
  python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/

  # Inspect joints in a single FBX:
  python tools/unicon_training/fetch_training_data.py "assets/characters/fbx/Y Bot.fbx" --list-joints

  # Convert at 60 FPS:
  python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/ --fps 60
        """,
    )
    parser.add_argument("input", type=str,
                        help="FBX file or directory of FBX files")
    parser.add_argument("-o", "--output", type=str, default="assets/motions",
                        help="Output directory for JSON motion files (default: assets/motions)")
    parser.add_argument("--fps", type=float, default=30.0,
                        help="Target frames per second (default: 30)")
    parser.add_argument("--scale", type=float, default=0.01,
                        help="Scale factor, cm to meters (default: 0.01)")
    parser.add_argument("--list-joints", action="store_true",
                        help="List joints in FBX file(s) and exit")
    args = parser.parse_args()

    input_path = Path(args.input)

    if args.list_joints:
        if input_path.is_dir():
            for fbx in sorted(input_path.glob("*.fbx")):
                list_joints(str(fbx))
        else:
            list_joints(str(input_path))
        return

    if input_path.is_dir():
        convert_directory(str(input_path), args.output, args.fps, args.scale)
    elif input_path.is_file():
        motion = convert_fbx(str(input_path), args.fps, args.scale)
        if motion:
            out_dir = Path(args.output)
            out_dir.mkdir(parents=True, exist_ok=True)
            out_file = out_dir / f"{input_path.stem}.json"
            with open(out_file, "w") as f:
                json.dump(motion, f)
            print(f"Written: {out_file} ({motion['num_frames']} frames)")
    else:
        print(f"Error: {args.input} not found")
        sys.exit(1)


if __name__ == "__main__":
    main()
