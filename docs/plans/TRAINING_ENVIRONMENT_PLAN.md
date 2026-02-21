# Training Environment Plan

Build a Jolt-based physics training environment for AMP/CALM reinforcement learning, replacing the Isaac Gym dependency with the engine's existing Jolt Physics integration.

## Motivation

The current CALM pipeline assumes training happens externally in Isaac Gym (NVIDIA GPU-accelerated simulator). This creates a sim-to-sim gap: characters trained in Isaac Gym's PhysX/GPU backend must transfer to Jolt's CPU physics at inference. Building a Jolt-native training environment eliminates this gap and keeps the entire pipeline self-contained.

### Why Not Just Use Isaac Gym?

- **Sim-to-sim transfer gap**: Isaac Gym uses NVIDIA GPU PhysX with different contact resolution, joint limits, and damping behaviour. Policies trained there often need extensive tuning to work under Jolt.
- **NVIDIA lock-in**: Isaac Gym requires NVIDIA GPUs with CUDA. A Jolt-based trainer runs on any platform.
- **Iteration speed**: When training and inference use the same physics, what works in training works at runtime — no debugging two separate simulator configurations.
- **Asset pipeline unity**: Same URDF/skeleton definitions, same PD gain tuning, same joint limit conventions.

### What Isaac Gym Provides That We Must Replicate

1. **Vectorised environment**: Thousands of parallel character instances stepping in lockstep
2. **Gym-style API**: `reset()`, `step(actions)` → `(obs, reward, done, info)`
3. **Reference motion loading**: Load `.npy` motion clips, sample frames for the AMP discriminator
4. **AMP observation computation**: Build the ~105D observation vector from physics state
5. **Reward computation**: Task rewards + AMP style reward from the discriminator
6. **PPO training loop**: On-policy RL with GAE, clipped surrogate objective

---

## Architecture

```
┌──────────────────────────────────────────────────────────────────┐
│                     Python Training Driver                        │
│                                                                   │
│  ┌──────────┐  ┌──────────┐  ┌───────────┐  ┌───────────────┐   │
│  │ PPO      │  │ AMP      │  │ Motion    │  │ Checkpointer  │   │
│  │ Trainer  │  │ Discrim. │  │ Dataset   │  │ (→ calm_export)│   │
│  └────┬─────┘  └────┬─────┘  └─────┬─────┘  └───────────────┘   │
│       │              │              │                              │
│       ▼              ▼              ▼                              │
│  ┌──────────────────────────────────────────────────────────┐    │
│  │            VecEnv Python Wrapper (pybind11)               │    │
│  │  reset() / step() / get_obs() / compute_reward()         │    │
│  └──────────────────────┬───────────────────────────────────┘    │
└─────────────────────────┼────────────────────────────────────────┘
                          │ pybind11
┌─────────────────────────┼────────────────────────────────────────┐
│                   C++ Training Environment                        │
│                                                                   │
│  ┌──────────────────────┴───────────────────────────────────┐    │
│  │               TrainingVecEnv (C++)                         │    │
│  │  N parallel character instances, each with:                │    │
│  │  - Jolt PhysicsSystem (shared)                             │    │
│  │  - Ragdoll instance (per-character)                        │    │
│  │  - ObservationExtractor (from src/ml/)                     │    │
│  │  - ActionApplier (from src/ml/)                            │    │
│  │  - CharacterConfig (from src/ml/)                          │    │
│  │  - Reference motion sampler                                │    │
│  └──────────────────────────────────────────────────────────┘    │
│                                                                   │
│  Reuses: Jolt Physics, RagdollBuilder, RagdollInstance,           │
│          ObservationExtractor, ActionApplier, CharacterConfig      │
└──────────────────────────────────────────────────────────────────┘
```

---

## Phase 1: Motion Data Pipeline

Convert raw mocap (BVH from CMU, or FBX from Mixamo) into the training observation format.

### 1.1 BVH Parser (`tools/convert_mocap_to_training.py`)

- Parse BVH files (joint hierarchy + per-frame Euler rotations)
- Retarget to the engine's humanoid skeleton using the existing `data/calm/retarget_map.json`
- Compute forward kinematics to get joint positions and root transform per frame
- Output: `.npy` files containing per-frame state vectors (joint rotations as quaternions, root position/rotation, joint positions)

### 1.2 FBX Loader Integration

- Reuse the existing `FBXLoader` in `src/loaders/` to read Mixamo FBX clips
- Extract joint rotations per frame, convert to the training skeleton
- Same output format as 1.1

### 1.3 Motion Dataset Manager (`tools/motion_dataset.py`)

- YAML manifest listing all motion clips with metadata:
  ```yaml
  motions:
    - file: walk_forward.npy
      fps: 30
      tags: [walk, locomotion]
    - file: run_sprint.npy
      fps: 30
      tags: [run, locomotion]
  ```
- Computes and caches AMP observation vectors for all frames (the "real" distribution for the discriminator)
- Random frame sampling for AMP training

### 1.4 AMP Observation Computation (Python side)

- Replicate the observation formula from `src/ml/ObservationExtractor`:
  - Root height (1D)
  - Root rotation in 6D representation (6D)
  - Root linear velocity (3D)
  - Root angular velocity (3D)
  - DOF positions (N)
  - DOF velocities (N)
  - Key body positions relative to root (K*3)
- Velocities computed via finite differences between consecutive frames
- Must match the C++ extractor exactly — shared test vectors to validate

---

## Phase 2: Jolt Training Environment (C++)

A headless C++ environment that steps Jolt physics and produces observations. No rendering needed during training.

### 2.1 Single Character Environment (`src/training/CharacterEnv.h`)

Core gym-like interface for one character:

```cpp
namespace training {

struct EnvConfig {
    float simTimestep = 1.0f / 60.0f;
    int simSubsteps = 2;
    float earlyTerminationHeight = 0.3f;  // Fall detection
    int maxEpisodeSteps = 300;
};

class CharacterEnv {
public:
    CharacterEnv(const EnvConfig& config,
                 const ml::CharacterConfig& charConfig,
                 const Skeleton& skeleton);

    // Reset to a random reference motion frame
    void reset(const MotionFrame& initialFrame);

    // Apply PD target actions, step physics, extract new observation
    StepResult step(const ml::Tensor& actions);

    // Current AMP observation (for discriminator)
    const ml::Tensor& ampObs() const;

    // Current policy observation (stacked frames)
    const ml::Tensor& policyObs() const;

    struct StepResult {
        float taskReward;
        bool done;          // Episode terminated (fall, timeout)
        bool timeout;       // Specifically a timeout (vs fall)
    };

private:
    JPH::PhysicsSystem physics_;
    std::unique_ptr<physics::RagdollInstance> ragdoll_;
    ml::ObservationExtractor obsExtractor_;
    ml::ActionApplier actionApplier_;
    // ...
};

} // namespace training
```

### 2.2 PD Controller Action Application

Actions from the policy are target joint angles. The environment converts these to motor torques via PD control:

```
torque = kp * (target - current) - kd * velocity
```

Uses the existing `CharacterConfig::pdGains` for per-joint kp/kd values. The `RagdollInstance::driveToTargetPose()` method already implements this.

### 2.3 Reward Computation

**Task reward** (computed per-step):
- Heading task: reward for matching target heading direction
- Location task: reward for moving toward target position
- Strike task: reward for hand reaching target

**AMP style reward** (computed by the Python discriminator):
- The environment provides the observation; Python computes `r_style = clamp(1 - 0.25 * (D(obs) - 1)^2, 0, 1)` where D is the learned discriminator

**Early termination**:
- Root height below threshold (character fell)
- Episode step limit exceeded

### 2.4 Vectorised Environment (`src/training/VecEnv.h`)

Batch N character environments for parallel data collection:

```cpp
class VecEnv {
public:
    VecEnv(int numEnvs, const EnvConfig& config, ...);

    // Reset all done environments to random reference frames
    void resetDone(const std::vector<MotionFrame>& frames);

    // Step all environments with batched actions [N x actionDim]
    void step(const float* actions);

    // Batched observation output [N x obsDim]
    const float* observations() const;

    // Batched AMP observations [N x ampObsDim]
    const float* ampObservations() const;

    // Per-env results
    const float* rewards() const;
    const bool* dones() const;
};
```

All N characters share one `JPH::PhysicsSystem` with separate body groups for collision isolation. Jolt handles multi-body simulation efficiently on CPU with its job system.

### 2.5 pybind11 Bindings (`src/training/bindings.cpp`)

Expose VecEnv to Python:

```python
import jolt_training

env = jolt_training.VecEnv(
    num_envs=4096,
    skeleton_path="data/characters/humanoid.glb",
    config=jolt_training.EnvConfig(sim_timestep=1/60)
)

obs = env.reset()
for step in range(max_steps):
    actions = policy(obs)
    obs, rewards, dones, infos = env.step(actions)
```

NumPy zero-copy views into the C++ buffers for minimal overhead.

---

## Phase 3: PPO Training Loop (Python)

Standard on-policy RL trainer using the Jolt VecEnv.

### 3.1 Policy and Value Networks (`training/networks.py`)

- **Policy network**: MLP matching the LLC architecture (input: stacked obs + latent z, output: action means + log_std)
- **Value network**: Separate MLP (input: stacked obs, output: scalar value)
- **AMP discriminator**: MLP (input: AMP obs, output: style score)

Network sizes match the existing CALM architecture so exported weights are directly compatible with the C++ inference engine.

### 3.2 PPO Algorithm (`training/ppo.py`)

Standard PPO with:
- Generalised Advantage Estimation (GAE, λ=0.95)
- Clipped surrogate objective (ε=0.2)
- Value function clipping
- Entropy bonus
- Gradient norm clipping

Combined reward: `r = w_task * r_task + w_style * r_style`
Default weights: `w_task = 0.5, w_style = 0.5` (CALM paper defaults)

### 3.3 AMP Discriminator Training (`training/amp.py`)

The discriminator learns to distinguish "real" (reference motion) observations from "fake" (policy rollout) observations:

- Binary classifier with gradient penalty (WGAN-GP style, as in AMP paper)
- Reference observations sampled from the motion dataset
- Policy observations collected during rollouts
- Updated every PPO epoch

The discriminator's output provides the style reward that encourages the policy to produce natural-looking motion.

### 3.4 Training Script (`training/train_amp.py`)

```bash
python training/train_amp.py \
    --num-envs 4096 \
    --motions data/calm/motions/manifest.yaml \
    --skeleton data/characters/humanoid.glb \
    --task heading \
    --output checkpoints/
```

Supports:
- AMP training (LLC only, no latent space)
- CALM training (LLC + encoder + latent space)
- HLC training (frozen LLC, task-specific policy)

### 3.5 Checkpoint Export

After training, use the existing `tools/calm_export.py` to convert PyTorch checkpoints to the engine's MLP1 binary format. The training script also saves intermediate checkpoints for resumption.

---

## Phase 4: CALM-Specific Training

Extends AMP training with the CALM latent space and encoder.

### 4.1 Encoder Network Training

The encoder maps stacked observation sequences to latent codes:
- Input: 10 frames of AMP observations (10 * obsDim)
- Output: 64D latent vector (L2-normalized)
- Trained with contrastive loss: clips from the same motion → similar latents, different motions → different latents

### 4.2 Style-Conditioned LLC Training

The LLC receives a latent code z concatenated with the observation:
1. Style MLP: z → style embedding (tanh activation)
2. Main MLP: concat(style_embed, obs) → hidden (ReLU activation)
3. Mu head: hidden → action means

Trained with:
- Per-motion-clip episodes: each episode samples one clip, encoder produces z, LLC must imitate that clip
- AMP discriminator provides style reward
- Task reward optional at this stage

### 4.3 HLC Training (Frozen LLC)

After the LLC is trained:
- Freeze LLC weights
- Train task-specific HLCs that output latent codes
- HLC input: task-specific observation (heading angle, target position, etc.)
- HLC output: 64D latent code → LLC produces motion
- Reward: task completion (reach target, match heading)

---

## Phase 5: Validation and Sim-to-Sim Verification

### 5.1 Shared Test Vectors

Generate test vectors that both the C++ inference engine and the Python training environment must agree on:
- Given the same skeleton state, both produce identical AMP observations
- Given the same actions, both produce identical PD torques
- Given the same weights, both produce identical MLP outputs

Test vectors stored as JSON in `tests/data/training_test_vectors.json`.

### 5.2 Round-Trip Test

1. Train a simple policy (walk forward) in the Jolt training env
2. Export weights with `calm_export.py`
3. Load in the C++ inference engine
4. Verify the character walks forward without falling

### 5.3 Performance Benchmarks

- Measure Jolt VecEnv throughput (steps/second) at various env counts (256, 1024, 4096)
- Compare wall-clock training time against Isaac Gym for the same task
- Jolt CPU will be slower than Isaac Gym GPU — target 10K-50K steps/sec for 4096 envs using Jolt's multi-threaded job system

---

## Implementation Order

The phases are designed so each delivers something testable:

1. **Phase 1** (Motion Pipeline): Download CMU data, convert to `.npy`, verify observations match C++ extractor
2. **Phase 2.1-2.3** (Single Env): One character, step physics, extract obs, compute reward — test that a hand-written policy can walk
3. **Phase 2.4-2.5** (VecEnv + Python): Parallelise, add pybind11 — verify env runs from Python
4. **Phase 3.1-3.2** (PPO): Train a simple AMP walk policy — character should walk without falling
5. **Phase 3.3** (Discriminator): Add AMP style reward — character motion should look natural
6. **Phase 4.1-4.2** (CALM LLC): Add encoder and style-conditioned training — multiple behaviours from one policy
7. **Phase 4.3** (HLC): Task-specific controllers — heading, location, strike
8. **Phase 5** (Validation): End-to-end round-trip test

---

## Directory Structure

```
training/                          # Python training code
  train_amp.py                     # Main training script
  ppo.py                           # PPO algorithm
  amp.py                           # AMP discriminator
  networks.py                      # Policy/value/discriminator networks
  vec_env_wrapper.py               # Python wrapper for C++ VecEnv
  motion_dataset.py                # Motion clip loading and sampling
  config/
    humanoid_amp.yaml              # AMP training config
    humanoid_calm.yaml             # CALM training config

src/training/                      # C++ training environment
  CharacterEnv.h / .cpp            # Single character environment
  VecEnv.h / .cpp                  # Vectorised environment
  MotionFrame.h                    # Reference motion frame data
  RewardComputer.h / .cpp          # Task reward computation
  bindings.cpp                     # pybind11 Python bindings

tools/
  convert_mocap_to_training.py     # BVH/FBX → .npy conversion
  calm_export.py                   # (existing) PyTorch → MLP1 binary
  calm_encode_library.py           # (existing) Encode latent library

scripts/
  download_cmu_mocap.sh            # (existing) Download CMU BVH data

data/
  mocap/cmu/                       # Raw BVH files from CMU
  calm/motions/                    # Converted .npy training clips
  calm/models/                     # Exported model weights
```

---

## Dependencies

**C++ (already available)**:
- Jolt Physics (via vcpkg)
- GLM (via vcpkg)
- Existing `src/ml/` inference code
- Existing `src/physics/` ragdoll code

**C++ (new)**:
- pybind11 (via vcpkg) — for Python bindings

**Python**:
- PyTorch — policy/value/discriminator networks + PPO
- NumPy — observation buffers, motion data
- PyYAML — config files
- pybind11 — build the C++ extension module
- Optional: TensorBoard/wandb for training monitoring

---

## Key Design Decisions

**CPU training with Jolt vs GPU training with Isaac Gym**:
Jolt is CPU-only, so training will be slower than Isaac Gym's GPU parallelism. However:
- Zero sim-to-sim gap (same physics for training and inference)
- No NVIDIA GPU requirement
- Jolt's job system parallelises across CPU cores effectively
- For this project, training quality matters more than training speed — a policy that works perfectly at inference is worth slower training

**Shared C++ code between training and inference**:
`ObservationExtractor`, `ActionApplier`, `CharacterConfig`, and `RagdollInstance` are used identically in both training and inference. This guarantees observation/action consistency.

**Python for RL, C++ for physics**:
The RL algorithm (PPO, discriminator) runs in Python/PyTorch. The physics environment (Jolt stepping, observation extraction, PD control) runs in C++ exposed via pybind11. This matches the Isaac Gym architecture and leverages PyTorch's autograd for the learning algorithms.
