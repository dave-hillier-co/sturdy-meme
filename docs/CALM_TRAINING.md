# CALM Training Guide

Training pipeline for CALM (Conditional Adversarial Latent Models). Three-phase training:

1. **Phase 1 -- LLC**: Low-Level Controller via PPO + AMP discriminator
2. **Phase 2 -- Encoder**: Motion encoder via contrastive learning (frozen LLC)
3. **Phase 3 -- HLC**: Task-specific High-Level Controllers via PPO (frozen LLC)

Exports networks to MLP1 binary format for the C++ runtime (`calm::ModelLoader`).

## Quick Start

```bash
# 1. Set up Python environment
python3 -m venv .venv
source .venv/bin/activate
pip install torch numpy mujoco

# 2. Phase 1: LLC training (short test)
python -m tools.calm_training.train_llc --iterations 10 --num-envs 4

# 3. Phase 2: Encoder training
python -m tools.calm_training.train_encoder --iterations 10

# 4. Phase 3: HLC training (one task)
python -m tools.calm_training.train_hlc --task heading --iterations 10 --num-envs 4

# 5. Verify exported weights
python -c "from tools.ml.export import verify_weights; verify_weights('checkpoints/calm/llc_style.bin')"
python -c "from tools.ml.export import verify_weights; verify_weights('checkpoints/calm/encoder.bin')"
python -c "from tools.ml.export import verify_weights; verify_weights('checkpoints/calm/hlc_heading.bin')"
```

Without motion data the trainers fall back to synthetic standing-still clips, which is enough to verify the pipeline end-to-end.

## Prerequisites

- Python 3.10+
- PyTorch (CPU is sufficient; CUDA or MPS for faster training)
- MuJoCo >= 3.0 (`pip install mujoco`)
- NumPy

Install all at once:

```bash
pip install torch numpy mujoco
```

## Motion Data

Motion clips live in `data/calm/motions/` as JSON files. Each clip has per-frame root and joint poses.

### JSON format

```json
{
  "fps": 60.0,
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

Quaternions use `(w, x, y, z)` convention. BVH files are also supported. See `tools/ml/motion_loader.py` for loading details.

## Phase 1: LLC Training (PPO + AMP)

The LLC (Low-Level Controller) is a style-conditioned policy that takes a latent code `z` and observations, and outputs 37 joint angle targets.

```bash
python -m tools.calm_training.train_llc [OPTIONS]
```

### How it works

Each iteration:
1. Sample random L2-normalized latents per environment
2. Collect rollouts: LLC policy produces actions from (latent, obs) pairs
3. AMP discriminator scores transitions as real (from motion clips) or fake (from policy)
4. Combine rewards: `r = 0.5 * r_style + 0.5 * r_task`
5. PPO update with style-conditioned evaluate_actions
6. WGAN-GP discriminator update

### CLI flags

| Flag | Default | Description |
|------|---------|-------------|
| `--config` | none | JSON config file |
| `--output` | `checkpoints/calm` | Output directory |
| `--motions` | `data/calm/motions` | Motion data directory |
| `--iterations` | 5000 | Training iterations |
| `--num-envs` | 4096 | Number of environments |
| `--device` | `auto` | `auto`, `cpu`, `cuda`, `mps` |
| `--seed` | `42` | Random seed |
| `--parallel` | off | Enable multiprocessing |
| `--num-workers` | auto | Worker processes for `--parallel` |

### Examples

Quick test:

```bash
python -m tools.calm_training.train_llc --iterations 10 --num-envs 4
```

Full training with parallel environments:

```bash
python -m tools.calm_training.train_llc --iterations 5000 --num-envs 256 --parallel --num-workers 4 --device mps
```

### Outputs

| File | Description |
|------|-------------|
| `llc_checkpoint_NNNNNN.pt` | PyTorch checkpoint |
| `llc_style.bin` | Style MLP (tanh activations) |
| `llc_main.bin` | Main MLP (relu activations) |
| `llc_mu_head.bin` | Action head (linear) |

## Phase 2: Encoder Training (Contrastive)

Trains a motion encoder that maps 10 stacked frames (1020 dims) to an L2-normalized 64D latent vector. Uses InfoNCE contrastive loss.

```bash
python -m tools.calm_training.train_encoder [OPTIONS]
```

### How it works

1. Pre-extract sliding windows of 10-frame stacked observations from all motion clips
2. For each batch: sample anchor observations, positive pairs (same clip, within `positive_window` frames), and negatives (different clips)
3. Encode all through the encoder network
4. InfoNCE loss: maximize similarity for positive pairs, minimize for negatives

### CLI flags

| Flag | Default | Description |
|------|---------|-------------|
| `--llc-checkpoint` | none | LLC checkpoint from Phase 1 |
| `--config` | none | JSON config file |
| `--output` | `checkpoints/calm` | Output directory |
| `--motions` | `data/calm/motions` | Motion data directory |
| `--iterations` | 5000 | Training iterations |
| `--device` | `auto` | Compute device |
| `--seed` | `42` | Random seed |

### Examples

```bash
python -m tools.calm_training.train_encoder \
    --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \
    --iterations 5000 --device mps
```

### Outputs

| File | Description |
|------|-------------|
| `encoder_checkpoint_NNNNNN.pt` | PyTorch checkpoint |
| `encoder.bin` | Encoder network (relu hidden, linear output) |

### Encoding a latent library

After training the encoder, pre-encode motion clips into a latent library:

```bash
python tools/calm_encode_library.py \
    --encoder-bin checkpoints/calm/encoder.bin \
    --clips data/motion_clips/ \
    --output data/calm/latent_library.json
```

## Phase 3: HLC Training (Task PPO)

Trains task-specific High-Level Controllers. Each HLC maps task observations to latent commands for the frozen LLC.

```bash
python -m tools.calm_training.train_hlc --task <TASK> [OPTIONS]
```

### Tasks

| Task | `task_obs_dim` | Observation | Reward |
|------|---------------|-------------|--------|
| `heading` | 2 | (sin, cos) of target angle | Match facing direction + speed |
| `location` | 3 | Relative target position (xyz) | Distance to target |
| `strike` | 6 | Target position + hand position | Hand proximity to target |

### CLI flags

| Flag | Default | Description |
|------|---------|-------------|
| `--task` | (required) | `heading`, `location`, or `strike` |
| `--llc-checkpoint` | none | LLC checkpoint from Phase 1 |
| `--config` | none | JSON config file |
| `--output` | `checkpoints/calm` | Output directory |
| `--motions` | `data/calm/motions` | Motion data directory |
| `--iterations` | 2000 | Training iterations |
| `--num-envs` | 4096 | Number of environments |
| `--device` | `auto` | Compute device |
| `--seed` | `42` | Random seed |

### Examples

Train all three HLCs:

```bash
python -m tools.calm_training.train_hlc --task heading \
    --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \
    --iterations 2000 --num-envs 64

python -m tools.calm_training.train_hlc --task location \
    --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \
    --iterations 2000 --num-envs 64

python -m tools.calm_training.train_hlc --task strike \
    --llc-checkpoint checkpoints/calm/llc_checkpoint_005000.pt \
    --iterations 2000 --num-envs 64
```

### Outputs

| File | Description |
|------|-------------|
| `hlc_{task}_checkpoint_NNNNNN.pt` | PyTorch checkpoint |
| `hlc_heading.bin` | Heading HLC (relu hidden, linear output) |
| `hlc_location.bin` | Location HLC |
| `hlc_strike.bin` | Strike HLC |

## Configuration Reference

All configuration is through `CALMConfig` (see `tools/calm_training/config.py`). Override via `--config` JSON or CLI flags.

### Observation Layout (102 dims per frame)

| Component | Dimensions | Description |
|-----------|-----------|-------------|
| Root height | 1 | Y coordinate |
| Root rotation 6D | 6 | Heading-invariant (first two columns of rotation matrix) |
| Local root velocity | 3 | In heading frame |
| Local root angular velocity | 3 | In heading frame |
| DOF positions | 37 | Joint angles |
| DOF velocities | 37 | Finite differences |
| Key body positions | 15 | 5 bodies x 3 (relative to root, heading frame) |

Stacked for policy: 2 frames = 204 dims. Stacked for encoder: 10 frames = 1020 dims.

### DOF Layout (37 DOFs)

pelvis(3) + abdomen(3) + chest(3) + neck(3) + head(3) + r_upper_arm(3) + r_lower_arm(1) + l_upper_arm(3) + l_lower_arm(1) + r_thigh(3) + r_shin(1) + r_foot(3) + l_thigh(3) + l_shin(1) + l_foot(3)

### Key Bodies (5)

head, right_hand, left_hand, right_foot, left_foot

### Network Architectures

**LLC (Style-Conditioned Policy)**:
- Style MLP: 64 -> 256(tanh) -> 128(tanh) -> 64(tanh)
- Main MLP: 268 -> 1024(relu) -> 512(relu)  (input = 64 style + 204 obs)
- Mu Head: 512 -> 37 (linear)
- Learnable per-action log_std

**AMP Discriminator**: 204 -> 1024(relu) -> 512(relu) -> 1 (input = concat(obs_t, obs_t1))

**Motion Encoder**: 1020 -> 1024(relu) -> 512(relu) -> 64 (L2 normalized)

**HLC** (per task): task_obs_dim -> 512(relu) -> 256(relu) -> 64 (L2 normalized)

**Value Networks**: obs_dim -> 1024(relu) -> 512(relu) -> 256(relu) -> 1

### PPO Hyperparameters

| Parameter | Value |
|-----------|-------|
| Learning rate | 3e-4 |
| Clip epsilon | 0.2 |
| Entropy coeff | 0.01 |
| Value coeff | 0.5 |
| Max grad norm | 1.0 |
| Gamma | 0.99 |
| GAE lambda | 0.95 |
| Epochs | 5 |
| Minibatch size | 512 |
| Steps per env | 32 |

### AMP Parameters

| Parameter | Value |
|-----------|-------|
| Discriminator LR | 1e-4 |
| Gradient penalty | 10.0 |
| Style reward weight | 0.5 |
| Task reward weight | 0.5 |

### Encoder Training Parameters

| Parameter | Value |
|-----------|-------|
| Learning rate | 1e-4 |
| Positive window | 30 frames |
| Negative clips | 4 |
| Contrastive margin | 1.0 |

## Export and C++ Integration

### Expected Directory Layout

After training all three phases, the output directory should contain:

```
checkpoints/calm/
  llc_style.bin        -- Style MLP weights
  llc_main.bin         -- Main policy MLP weights
  llc_mu_head.bin      -- Action head weights
  encoder.bin          -- Motion encoder
  hlc_heading.bin      -- Heading HLC
  hlc_location.bin     -- Location HLC
  hlc_strike.bin       -- Strike HLC
```

Optional additional files:
- `latent_library.json` -- Pre-encoded behavior latent vectors
- `retarget_map.json` -- Skeleton joint retargeting map

### C++ Loading

The C++ runtime loads all components via `calm::ModelLoader::loadAll()`:

```cpp
calm::ModelLoader loader;
loader.loadAll("checkpoints/calm/", characterConfig);
// Populates: LLC (style + main + mu_head), encoder, HLCs
```

Individual components can also be loaded:

```cpp
loader.loadLLC("checkpoints/calm/");    // style + main + mu_head
loader.loadEncoder("checkpoints/calm/"); // encoder
loader.loadHLC("heading", "checkpoints/calm/"); // single HLC
```

### MLP1 Binary Format

All `.bin` files use the MLP1 format:

```
Header (12 bytes):
  uint32  magic       = 0x4D4C5031 ("MLP1")
  uint32  version     = 1
  uint32  numLayers

Per layer:
  uint32  inFeatures
  uint32  outFeatures
  uint32  activationType   (0=None, 1=ReLU, 2=Tanh)
  float32[outFeatures * inFeatures]  weights (row-major)
  float32[outFeatures]               biases
```

## Verification

### Run all tests

```bash
python -m pytest tools/calm_training/test_observation.py -v
python -m pytest tools/calm_training/test_networks.py -v
python -m pytest tools/calm_training/test_export.py -v
```

### Full pipeline test

```bash
# Phase 1
python -m tools.calm_training.train_llc --iterations 10 --num-envs 4 --output /tmp/calm_test

# Phase 2
python -m tools.calm_training.train_encoder --iterations 10 --output /tmp/calm_test

# Phase 3
python -m tools.calm_training.train_hlc --task heading --iterations 10 --num-envs 4 --output /tmp/calm_test
python -m tools.calm_training.train_hlc --task location --iterations 10 --num-envs 4 --output /tmp/calm_test
python -m tools.calm_training.train_hlc --task strike --iterations 10 --num-envs 4 --output /tmp/calm_test

# Verify all exported files
python -c "
from tools.ml.export import verify_weights
for f in ['llc_style.bin', 'llc_main.bin', 'llc_mu_head.bin', 'encoder.bin',
          'hlc_heading.bin', 'hlc_location.bin', 'hlc_strike.bin']:
    verify_weights(f'/tmp/calm_test/{f}')
"
```

### Verify numpy forward pass matches PyTorch

```bash
python -m pytest tools/calm_training/test_export.py::TestExportLLC::test_full_pipeline_roundtrip -v
```

### Checking training output

During LLC training, watch for:
- `reward` increasing (policy improving)
- `d_loss` staying near zero (discriminator balanced)
- `real` and `fake` scores converging (discriminator can't easily distinguish)
- `entropy` gradually decreasing (policy becoming more deterministic)

During encoder training, watch for:
- `loss` (InfoNCE) decreasing (encoder learning to distinguish clips)

During HLC training, watch for:
- `reward` increasing (task performance improving)
