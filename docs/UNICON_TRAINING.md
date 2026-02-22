# UniCon Training Guide

Training pipeline for the UniCon (Universal Humanoid Controller) policy using PPO. Trains an MLP policy in MuJoCo that exports to the C++ Vulkan renderer via the MLP1 binary format.

## Quick Start

```bash
# 1. Set up Python environment
python3 -m venv .venv
source .venv/bin/activate
pip install torch numpy mujoco

# 2. Run a short training test (10 iterations, no motion data needed)
python -m tools.unicon_training.train --iterations 10 --num-envs 4 --output generated/unicon

# 3. Verify the exported weights
python -c "from tools.ml.export import verify_weights; verify_weights('generated/unicon/policy_weights.bin')"
```

Without motion data the trainer falls back to a synthetic standing-still clip, which is enough to verify the pipeline works end-to-end.

## Prerequisites

- Python 3.10+
- PyTorch (CPU is sufficient; CUDA or MPS for faster training)
- MuJoCo >= 3.0 (`pip install mujoco`)
- NumPy

For FBX motion conversion, additionally:
- pyassimp (`pip install pyassimp`)
- The assimp shared library:
  - macOS: `brew install assimp`
  - Ubuntu/Debian: `sudo apt install libassimp-dev`

Install all at once:

```bash
pip install torch numpy mujoco pyassimp
```

## Motion Data

### Directory layout

Motion clips live in `assets/motions/` as JSON files. Each file contains one clip with per-frame root and joint poses.

### JSON format

```json
{
  "fps": 30.0,
  "duration": 2.5,
  "num_frames": 75,
  "source": "Walking.fbx",
  "frames": [
    {
      "root_pos": [0.0, 1.0, 0.0],
      "root_rot": [1.0, 0.0, 0.0, 0.0],
      "joint_positions": [[...], ...],
      "joint_rotations": [[1.0, 0.0, 0.0, 0.0], ...]
    }
  ]
}
```

Quaternions use `(w, x, y, z)` convention. Joint arrays have 20 entries matching the `ArticulatedBody` part ordering (Pelvis, LowerSpine, UpperSpine, Chest, Neck, Head, LeftShoulder, LeftUpperArm, LeftForearm, LeftHand, RightShoulder, RightUpperArm, RightForearm, RightHand, LeftThigh, LeftShin, LeftFoot, RightThigh, RightShin, RightFoot).

### Converting FBX from Mixamo

Download animations from [Mixamo](https://www.mixamo.com/) in FBX format, then convert:

```bash
# Convert all FBX files in a directory
python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/

# Convert a single file
python tools/unicon_training/fetch_training_data.py my_anim.fbx -o assets/motions/

# Inspect joints in an FBX (useful for debugging mapping issues)
python tools/unicon_training/fetch_training_data.py "assets/characters/fbx/Y Bot.fbx" --list-joints

# Convert at 60 FPS instead of the default 30
python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/ --fps 60
```

#### fetch_training_data.py flags

| Flag | Default | Description |
|------|---------|-------------|
| `input` | (required) | FBX file or directory of FBX files |
| `-o`, `--output` | `assets/motions` | Output directory for JSON files |
| `--fps` | `30.0` | Target frames per second |
| `--scale` | `0.01` | Scale factor (Mixamo uses cm, training uses meters) |
| `--list-joints` | off | List joints and exit without converting |

The converter maps Mixamo bone names (e.g. `mixamorig:LeftArm`) to the 20-part humanoid automatically.

### BVH files

The motion loader also supports CMU-style BVH files. Place `.bvh` files in `assets/motions/` and they will be loaded alongside JSON clips.

## Running Training

```bash
python -m tools.unicon_training.train [OPTIONS]
```

### CLI flags

| Flag | Default | Description |
|------|---------|-------------|
| `--config` | none | Path to JSON config file with overrides |
| `--output` | `generated/unicon` | Output directory for checkpoints and weights |
| `--motions` | `assets/motions` | Motion capture data directory |
| `--iterations` | (from config: 10000) | Number of PPO iterations |
| `--num-envs` | (from config: 4096) | Number of parallel environments |
| `--device` | `auto` | Compute device: `auto`, `cpu`, `cuda`, `mps` |
| `--seed` | `42` | Random seed |
| `--parallel` | off | Enable multiprocessing subprocess vec env |
| `--num-workers` | auto (cpu_count - 1) | Number of worker processes when `--parallel` is set |

### Example commands

Quick test run (a few seconds):

```bash
python -m tools.unicon_training.train --iterations 10 --num-envs 4
```

Full sequential training:

```bash
python -m tools.unicon_training.train --iterations 5000 --num-envs 64 --device mps
```

Full parallel training (recommended for longer runs):

```bash
python -m tools.unicon_training.train --iterations 5000 --num-envs 256 --parallel --num-workers 4 --device mps
```

### Device auto-selection

The `auto` device setting picks the best available: CUDA > MPS > CPU. On Apple Silicon Macs, MPS gives a significant speedup for the neural network forward/backward passes.

### Performance reference

On an Apple Silicon Mac (M-series):

| Mode | Environments | Throughput |
|------|-------------|------------|
| Sequential, CPU | 64 | ~237 steps/s |
| Parallel + MPS | 256, 4 workers | ~1490 steps/s |

## Configuration

Training is configured through nested dataclasses. All fields have sensible defaults. Override via `--config` JSON file or individual CLI flags.

### TrainingConfig (top-level)

| Field | Default | Description |
|-------|---------|-------------|
| `tau` | `1` | Number of future target frames in observation |
| `physics_dt` | `1/60` | Simulation timestep (seconds) |
| `physics_substeps` | `4` | MuJoCo substeps per policy step |
| `motion_dir` | `assets/motions` | Motion data directory |
| `output_dir` | `generated/unicon` | Output directory |
| `initial_log_std` | `-1.0` | Starting exploration noise (log std dev) |
| `final_log_std` | `-3.0` | Final exploration noise after annealing |
| `log_std_anneal_iterations` | `5000` | Iterations to anneal log_std |
| `device` | `auto` | Compute device |
| `seed` | `42` | Random seed |
| `parallel` | `false` | Enable subprocess vec env |
| `num_workers` | `0` | Worker count (0 = auto) |
| `max_envs` | `0` | Cap on environment count (0 = no limit) |

### PPOConfig

| Field | Default | Description |
|-------|---------|-------------|
| `num_envs` | `4096` | Number of environments |
| `samples_per_env` | `64` | Rollout length per environment per iteration |
| `num_epochs` | `5` | PPO epochs per iteration |
| `minibatch_size` | `512` | Minibatch size for PPO updates |
| `learning_rate` | `3e-4` | Adam learning rate |
| `gamma` | `0.99` | Discount factor |
| `gae_lambda` | `0.95` | GAE lambda |
| `clip_epsilon` | `0.2` | PPO clip range |
| `value_loss_coeff` | `0.5` | Value loss weight |
| `entropy_coeff` | `0.01` | Entropy bonus weight |
| `max_grad_norm` | `0.5` | Gradient clipping |
| `kl_target` | `0.01` | Target KL divergence |
| `num_iterations` | `10000` | Total training iterations |
| `checkpoint_interval` | `100` | Iterations between checkpoints |
| `log_interval` | `10` | Iterations between log lines |

### PolicyConfig

| Field | Default | Description |
|-------|---------|-------------|
| `hidden_layers` | `3` | Number of hidden layers |
| `hidden_dim` | `1024` | Hidden layer width |
| `activation` | `elu` | Activation function (`elu`, `relu`, `tanh`) |

### RewardConfig

| Field | Default | Description |
|-------|---------|-------------|
| `w_root_pos` | `0.2` | Weight for root position tracking |
| `w_root_rot` | `0.2` | Weight for root rotation tracking |
| `w_joint_pos` | `0.1` | Weight for joint position tracking |
| `w_joint_rot` | `0.4` | Weight for joint rotation tracking |
| `w_joint_ang_vel` | `0.1` | Weight for joint angular velocity tracking |
| `k_root_pos` | `10.0` | Kernel scale for root position |
| `k_root_rot` | `5.0` | Kernel scale for root rotation |
| `k_joint_pos` | `10.0` | Kernel scale for joint position |
| `k_joint_rot` | `2.0` | Kernel scale for joint rotation |
| `k_joint_ang_vel` | `0.1` | Kernel scale for joint angular velocity |
| `alpha` | `0.1` | Early termination threshold per reward term |

### RSISConfig (Reactive State Initialization Scheme)

| Field | Default | Description |
|-------|---------|-------------|
| `min_offset_frames` | `5` | Minimum random start offset into clip |
| `max_offset_frames` | `10` | Maximum random start offset into clip |
| `position_noise_std` | `0.02` | Std dev of initial position noise (meters) |
| `rotation_noise_std` | `0.05` | Std dev of initial joint angle noise (radians) |
| `velocity_noise_std` | `0.1` | Std dev of initial velocity noise |

### HumanoidConfig

| Field | Default | Description |
|-------|---------|-------------|
| `num_bodies` | `20` | Number of body parts |
| `total_mass_kg` | `70.0` | Total humanoid mass |
| `height_m` | `1.8` | Standing height |
| `num_dof` | `30` | Degrees of freedom (hinge joints) |
| `effort_factors` | (per-body list) | Torque scaling per body part (50--600 range) |

### JSON config overrides

Create a JSON file with top-level keys matching `TrainingConfig` fields:

```json
{
  "tau": 3,
  "initial_log_std": -0.5,
  "final_log_std": -2.5
}
```

```bash
python -m tools.unicon_training.train --config my_config.json
```

Note: nested config objects (e.g. `ppo`, `reward`) are set as top-level dict keys matching `TrainingConfig` attribute names. CLI flags for `--iterations`, `--num-envs`, `--device`, `--parallel`, and `--num-workers` take precedence over the JSON file.

## Architecture Overview

### Training loop

Each iteration:

1. **Rollout collection** -- step all environments for `samples_per_env` steps, collecting (obs, action, log_prob, reward, done, value) tuples into a `RolloutBuffer`.
2. **GAE computation** -- compute advantages and returns using Generalized Advantage Estimation.
3. **PPO update** -- run `num_epochs` of minibatch PPO on the collected data, clipping the policy ratio and updating both policy and value networks.
4. **Log-std annealing** -- linearly interpolate the policy's exploration noise from `initial_log_std` to `final_log_std` over `log_std_anneal_iterations` iterations.
5. **Checkpoint** -- every `checkpoint_interval` iterations, save a `.pt` checkpoint and export the best policy to `policy_weights.bin`.

### Observation encoding

The `StateEncoder` produces a 429-dimensional observation vector (with `num_joints=20`, `tau=1`):

- **Per-frame encoding** (211 dims): root height (1) + root rotation quaternion (4) + joint positions relative to root (20 x 3 = 60) + joint rotation quaternions (20 x 4 = 80) + root linear velocity (3) + root angular velocity (3) + joint angular velocities (20 x 3 = 60)
- **Observation**: `(1 + tau)` frame encodings (current + tau future targets) + `tau` root offset vectors (7 dims each: 2 horizontal + 1 height + 4 quaternion)
- Total: `(1 + 1) * 211 + 1 * 7 = 429`

### Action space

The policy outputs 60 values (20 body parts x 3 torque components), normalized to [-1, 1]. These map to 30 MuJoCo actuators via a sparse mapping -- unused outputs (for parts like Pelvis with no actuators, or duplicate parts like LeftUpperArm) are discarded. Applied torques are scaled by per-body effort factors (50--600 Nm range).

### Reward formula

Weighted sum of Gaussian kernel tracking terms:

```
r = 0.2 * exp(-10  * ||root_pos_error||^2)
  + 0.2 * exp(-5   * ||root_rot_error||^2)
  + 0.1 * exp(-10  * ||joint_pos_error||^2)
  + 0.4 * exp(-2   * ||joint_rot_error||^2)
  + 0.1 * exp(-0.1 * ||joint_ang_vel_error||^2)
```

Episodes terminate early if any individual reward term falls below `alpha` (0.1), implementing the constrained multi-objective approach from the UniCon paper.

### Parallel environments

Two modes:

- **Sequential** (default): all environments step in the main process. Simpler, lower overhead per step.
- **Parallel** (`--parallel`): a `SubprocVecEnv` distributes environments across worker processes using `multiprocessing.Pipe`. Each worker loads motion data independently and owns a subset of environments. Uses the `spawn` context for macOS compatibility.

## Export and C++ Integration

### MLP1 binary format

The exported `policy_weights.bin` uses a compact binary format:

```
Header (12 bytes):
  uint32  magic       = 0x4D4C5031 ("MLP1")
  uint32  version     = 1
  uint32  numLayers

Per layer:
  uint32  inFeatures
  uint32  outFeatures
  uint32  activationType   (0=None, 1=ReLU, 2=Tanh, 3=ELU)
  float32[outFeatures * inFeatures]  weights (row-major)
  float32[outFeatures]               biases
```

Hidden layers use ELU activation; the output layer uses None (linear).

### File locations

| File | Description |
|------|-------------|
| `generated/unicon/policy_weights.bin` | Exported policy for C++ runtime |
| `generated/unicon/checkpoint_NNNNNN.pt` | PyTorch checkpoint (policy + value + optimizer state) |

### C++ loading

The C++ runtime loads the policy via `ml::unicon::Controller` (`src/ml/unicon/Controller.h`):

```cpp
ml::unicon::Controller controller;
controller.init(20 /* numJoints */, 1 /* tau */);
controller.loadPolicy("generated/unicon/policy_weights.bin");
```

`loadPolicy()` delegates to `ml::ModelLoader::loadMLP()` (`src/ml/ModelLoader.h`) which parses the MLP1 binary format and populates an `MLPNetwork` for inference. At runtime, `controller.update()` encodes the current humanoid state, runs the MLP forward pass, and applies the resulting torques to the `ArticulatedBody` ragdolls via the physics engine.

### Checkpoint format

`.pt` files are standard PyTorch checkpoints containing:

```python
{
    "iteration": int,
    "policy_state_dict": ...,
    "value_state_dict": ...,
    "optimizer_state_dict": ...,
    "total_timesteps": int,
    "best_reward": float,
}
```

These can be loaded to resume training or to re-export with different settings.

## Verification

### Automated tests

Run the export round-trip tests:

```bash
python -m pytest tools/unicon_training/test_export.py -v
```

This verifies:

- **State encoder dimensions**: 429 dims for (20 joints, tau=1), 865 dims for (20 joints, tau=3)
- **Export round-trip**: policy weights survive export to MLP1 binary and read back identically
- **Forward pass equivalence**: manual matrix multiply + ELU matches PyTorch's forward pass, simulating C++ inference

### Manual verification

Verify an exported weight file:

```bash
python -c "from tools.ml.export import verify_weights; verify_weights('generated/unicon/policy_weights.bin')"
```

This reads the binary file, checks the magic header, validates layer dimensions, and prints weight statistics for each layer.

### Checking training output

During training, watch for:

- `reward` increasing over iterations (motion tracking improving)
- `entropy` gradually decreasing (policy becoming more deterministic as log_std anneals)
- `kl` staying near the target (0.01) -- large spikes indicate instability
- `ep_len` increasing (humanoid staying upright longer)
