# CALM Implementation Plan

Conditional Adversarial Latent Models for directable virtual characters, adapted to this engine's C++ Vulkan architecture.

## Background

CALM (Tessler et al., SIGGRAPH 2023) learns a structured latent space of character behaviors from motion capture data. A low-level controller (LLC) takes a latent code z and produces character motion; a high-level controller (HLC) selects latent codes for task objectives. Behaviors compose at inference time through latent interpolation and finite state machines, with no additional training.

### Why CALM Over Existing Systems

The engine already has AnimationStateMachine, BlendSpace, and MotionMatching. CALM adds:
- **Learned behavior diversity**: A single policy produces walk, run, crouch, kick, etc. based on a 64D latent code — no manual state machine authoring
- **Smooth composability**: Interpolating on the latent hypersphere produces natural transitions between any two behaviors
- **Task-driven control**: High-level policies learn to select latent codes for goals (go to location, strike target, follow heading) without hand-tuned animation logic
- **Physics grounding**: Motions emerge from physics simulation, so characters react naturally to terrain, collisions, and external forces

### Architecture Overview

```
┌─────────────────────────────────────────────────────────┐
│                    Inference (C++)                        │
│                                                          │
│  Task/FSM ──→ HLC(obs,task) ──→ z ──→ LLC(obs,z) ──→ a │
│                                  │                   │   │
│                        latent    │    PD targets /    │   │
│                        space     │    joint torques   │   │
│                                  │                   │   │
│  LatentLibrary ─────────────────→│   Jolt Physics ←──┘   │
│  (pre-encoded clips)             │       │               │
│                                  │       ↓               │
│                                  │   Skeleton Pose       │
│                                  │       │               │
│                                  │   IK + GPU Skinning   │
└─────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────┐
│              Training (Python/Isaac Gym)                  │
│                                                          │
│  Motion Dataset → Encoder + LLC + Discriminators → Model │
│  Task Rewards   → HLC (on frozen LLC)            → Model │
│                                                          │
│  Export: ONNX or raw weight tensors → C++ inference      │
└─────────────────────────────────────────────────────────┘
```

---

## Phase 1: Neural Network Inference Engine

A lightweight MLP inference system in C++. No heavy ML framework dependency — CALM networks are simple feedforward MLPs.

### 1.1 Core MLP Inference (`src/ml/`)

**Files:**
- `src/ml/Tensor.h` — Lightweight tensor (1D/2D float storage, no-copy views)
- `src/ml/MLPNetwork.h` — Feedforward MLP: linear layers + ReLU/Tanh activations
- `src/ml/ModelLoader.h` — Load weight files exported from PyTorch

**Tensor**: Flat `std::vector<float>` with shape metadata. Support matrix-vector multiply, element-wise ops, L2 normalization. No dynamic computation graph needed — inference is a fixed sequence of matmuls + activations.

**MLPNetwork**: Chain of layers, each being `output = activation(W * input + bias)`.
```cpp
struct LinearLayer {
    std::vector<float> weights;  // [out_features × in_features], row-major
    std::vector<float> bias;     // [out_features]
    int inFeatures, outFeatures;
};

class MLPNetwork {
    std::vector<LinearLayer> layers_;
    std::vector<Activation> activations_;  // ReLU, Tanh, None

    void forward(std::span<const float> input, std::span<float> output) const;
};
```

**ModelLoader**: Read weight files exported from PyTorch in a simple binary format (header + flat float arrays). Each file contains layer dimensions and weight/bias data.

**Export format** (from Python):
```python
# Export script writes: [num_layers][in0,out0,weights0,bias0][in1,out1,weights1,bias1]...
```

### 1.2 Batch Inference via Compute Shader (Optional, for NPC crowds)

For 50+ NPCs running CALM simultaneously, CPU inference becomes a bottleneck. A Vulkan compute shader can evaluate MLPs in parallel.

**Files:**
- `shaders/ml_inference.comp` — Generic MLP forward pass in compute
- `src/ml/GPUInference.h` — Upload weights as SSBOs, dispatch batched inference

This is optional and can be deferred until NPC count demands it.

### Incremental checkpoint
After Phase 1: A standalone test loads a simple MLP from file, runs forward pass, verifies output matches PyTorch reference. Build compiles and runs.

---

## Phase 2: CALM Observation & Action Spaces

Map CALM's observation and action representations to the engine's existing Skeleton/Joint/Physics systems.

### 2.1 CALM Observation Extraction (`src/ml/CALMObservation.h`)

CALM observations per timestep (~60D, character-dependent):

| Feature | Dims | Source in Engine |
|---------|------|------------------|
| Root height | 1 | `CharacterController::getPosition().y` |
| Root rotation (heading-invariant) | 6 | Skeleton root joint quaternion → tan-normalized 6D |
| Local root velocity | 3 | `CharacterController::getVelocity()` rotated into heading frame |
| Local root angular velocity | 3 | Derived from sequential root rotations |
| DOF positions | N | `Skeleton::joints[i].localTransform` → decomposed angles |
| DOF velocities | N | Finite difference of joint angles between frames |
| Key body positions (local frame) | 3×K | Selected joints (hands, feet, head) in root-relative coords |

**Temporal stacking**: Store a ring buffer of the last `numAMPObsSteps` (typically 2-10) observation frames. The encoder uses a wider window than the policy.

```cpp
class CALMObservationExtractor {
    static constexpr int MAX_OBS_HISTORY = 10;
    std::array<std::vector<float>, MAX_OBS_HISTORY> history_;
    int currentIdx_ = 0;

    void extractFrame(const Skeleton& skeleton,
                      const CharacterController& controller,
                      float deltaTime);

    // Single frame for policy
    std::span<const float> getCurrentObs() const;

    // Stacked frames for encoder/discriminator
    void getStackedObs(int numSteps, std::vector<float>& out) const;
};
```

### 2.2 CALM Action Application

CALM outputs PD (proportional-derivative) target angles for each joint DOF. These get converted to torques applied via physics.

**Option A — Physics-driven (faithful to paper):**
Each action is a target joint angle. A PD controller computes torques:
```
torque = kp * (target - current) + kd * (0 - velocity)
```
Applied through Jolt Physics articulated body joints. Requires Jolt ragdoll/articulated body setup.

**Option B — Kinematic pose (pragmatic for rendering):**
Treat CALM outputs as direct joint angle targets. Blend the resulting pose with the existing animation system. Skip physics torques; rely on CharacterController for root movement.

**Recommendation**: Start with Option B for visual results, migrate to Option A when ragdoll physics is needed for reactions/combat.

```cpp
class CALMActionApplier {
    void applyToSkeleton(std::span<const float> actions,
                         Skeleton& skeleton,
                         float blendWeight = 1.0f);

    // Option A: Apply through physics
    void applyAsTorques(std::span<const float> actions,
                        /* Jolt articulated body */);
};
```

### 2.3 Character Configuration (`src/ml/CALMCharacterConfig.h`)

Maps between CALM's DOF ordering and the engine's joint indices:
```cpp
struct CALMCharacterConfig {
    int observationDim;           // Per-frame observation size
    int actionDim;                // Number of action DOFs
    std::vector<int> dofToJoint;  // CALM DOF index → Skeleton joint index
    std::vector<int> keyBodyJoints; // Indices of key bodies (hands, feet, head)
    float pdKp = 40.0f;
    float pdKd = 5.0f;
};
```

### Incremental checkpoint
After Phase 2: Extract observations from an animated character each frame, log dimensions and values. Verify observation vector matches expected structure. Apply a random action vector to skeleton, confirm joints move. Build compiles and runs.

---

## Phase 3: CALM Low-Level Controller

The core inference loop: latent code + observation → character action.

### 3.1 Policy Network (`src/ml/CALMLowLevelController.h`)

```cpp
class CALMLowLevelController {
    MLPNetwork styleMLP_;    // [512, 256] + tanh — processes latent z
    MLPNetwork policyMLP_;   // [1024, 1024, 512] + ReLU — main policy
    LinearLayer muHead_;     // Final linear → action means

    CALMCharacterConfig config_;
    CALMObservationExtractor obsExtractor_;

    // z (64D) + obs → actions
    void evaluate(std::span<const float> latent,
                  std::span<const float> observation,
                  std::span<float> actions) const;
};
```

The forward pass mirrors `AMPStyleCatNet1`:
1. `styleEmbed = tanh(styleMLP(z))` — 64D → 256D
2. `combined = concat(styleEmbed, obs)` — (256 + obsD) input
3. `hidden = relu(policyMLP(combined))` — through [1024, 1024, 512]
4. `actions = muHead(hidden)` — 512 → actionDim

### 3.2 Latent Space (`src/ml/CALMLatentSpace.h`)

```cpp
class CALMLatentSpace {
    static constexpr int LATENT_DIM = 64;

    // Pre-encoded latents from motion clips
    struct EncodedBehavior {
        std::string clipName;
        std::vector<float> latent;  // 64D, L2-normalized
        std::vector<std::string> tags;  // "walk", "run", "crouch", etc.
    };
    std::vector<EncodedBehavior> library_;

    // Encoder network (for encoding new clips at runtime)
    MLPNetwork encoder_;  // [1024, 1024, 512] → 64D + L2 norm

    // Sample a random latent from the library
    const std::vector<float>& sampleRandom(std::mt19937& rng) const;

    // Sample by tag (e.g., "walk" behaviors only)
    const std::vector<float>& sampleByTag(const std::string& tag,
                                           std::mt19937& rng) const;

    // Encode a motion clip window into a latent
    void encode(std::span<const float> stackedObs,
                std::vector<float>& outLatent) const;

    // Interpolate between two latents on the unit hypersphere
    static void interpolate(std::span<const float> z0,
                            std::span<const float> z1,
                            float alpha,
                            std::span<float> out);
};
```

**Latent library pre-computation**: During a build step (or at load time), encode all motion clips through the encoder and store the resulting latent vectors with semantic tags. This avoids running the encoder at runtime for known behaviors.

### 3.3 CALM Controller Integration (`src/ml/CALMController.h`)

Ties observation extraction, latent management, and policy inference into a per-character controller:

```cpp
class CALMController {
    CALMLowLevelController llc_;
    CALMLatentSpace latentSpace_;
    CALMActionApplier actionApplier_;
    CALMObservationExtractor obsExtractor_;

    // Current latent state
    std::vector<float> currentLatent_;
    std::vector<float> targetLatent_;
    float interpolationAlpha_ = 1.0f;
    int stepsUntilResample_ = 0;

    // Per-frame update
    void update(float deltaTime,
                Skeleton& skeleton,
                const CharacterController& physics);

    // External control
    void setLatent(std::span<const float> z);
    void transitionToLatent(std::span<const float> z, int steps);
    void transitionToBehavior(const std::string& tag, int steps);
};
```

**Update loop** each frame:
1. Extract observation from skeleton + physics state
2. Decrement step counter; if expired, optionally resample latent
3. Interpolate currentLatent toward targetLatent
4. Run LLC: `policy(currentLatent, observation) → actions`
5. Apply actions to skeleton (or physics)

### Incremental checkpoint
After Phase 3: Load pre-trained LLC weights. Feed a fixed latent code. Character performs a recognizable motion (e.g., walking). Changing the latent code changes the behavior. Interpolating between two latents produces smooth transitions. Build compiles and runs.

---

## Phase 4: High-Level Controllers

Task-specific policies that output latent codes to command the LLC.

### 4.1 HLC Base (`src/ml/CALMHighLevelController.h`)

```cpp
class CALMHighLevelController {
    MLPNetwork hlcNetwork_;  // Task-specific policy network

    // Compute latent code from task observation
    void evaluate(std::span<const float> taskObs,
                  std::span<float> latent) const;
};
```

### 4.2 Task-Specific HLCs

**HeadingController** — Move in a direction at a target speed:
```cpp
class CALMHeadingController : public CALMHighLevelController {
    void setTarget(glm::vec2 direction, float speed);
    // Task obs: [local_target_dir(2D), locomotion_index(1D)]
};
```

**LocationController** — Navigate to a world position:
```cpp
class CALMLocationController : public CALMHighLevelController {
    void setTarget(glm::vec3 worldPosition);
    // Task obs: [local_offset_to_target(3D)]
};
```

**StrikeController** — Attack a target:
```cpp
class CALMStrikeController : public CALMHighLevelController {
    void setTarget(glm::vec3 targetPosition);
    // Task obs: [local_target_pos(3D), distance(1D)]
};
```

### 4.3 Integration with NPC AI

The existing `NPCSimulation` system uses `NPCActivity` states (Idle, Walk, Run, Attack). Map these to HLC selection:

```cpp
// In NPC update logic:
switch (activity) {
    case NPCActivity::Idle:
        calmController.transitionToBehavior("idle", 30);
        break;
    case NPCActivity::Walking:
        headingHLC.setTarget(targetDir, 1.5f);
        headingHLC.evaluate(taskObs, latent);
        calmController.setLatent(latent);
        break;
    case NPCActivity::Running:
        headingHLC.setTarget(targetDir, 6.0f);
        headingHLC.evaluate(taskObs, latent);
        calmController.setLatent(latent);
        break;
    case NPCActivity::Attacking:
        strikeHLC.setTarget(enemyPos);
        strikeHLC.evaluate(taskObs, latent);
        calmController.setLatent(latent);
        break;
}
```

### Incremental checkpoint
After Phase 4: An NPC navigates to a target location using the LocationController HLC. The LLC produces natural walking/running motion. The NPC stops and transitions to idle when it arrives. Build compiles and runs.

---

## Phase 5: Behavior Composition via FSM

Compose complex behavior sequences without additional training.

### 5.1 CALM Finite State Machine (`src/ml/CALMBehaviorFSM.h`)

```cpp
struct CALMFSMState {
    std::string name;
    std::string behaviorTag;           // Which latent behavior to use
    CALMHighLevelController* hlc;      // Optional HLC for task-driven states
    std::function<bool()> exitCondition;
    std::string nextState;
};

class CALMBehaviorFSM {
    std::unordered_map<std::string, CALMFSMState> states_;
    std::string currentState_;
    CALMController& calmController_;

    void addState(CALMFSMState state);
    void update(float deltaTime);
};
```

**Example FSM — "Stealth Attack":**
```
[crouch_walk] ──(distance < 3m)──→ [sprint] ──(distance < 1m)──→ [strike] ──(done)──→ [celebrate]
```

Each state sets the appropriate latent/HLC on the CALMController. Transitions are checked each frame.

### 5.2 Latent Interpolation During Transitions

When the FSM transitions between states, the CALMController smoothly interpolates:
1. FSM enters new state → calls `transitionToLatent(newZ, blendSteps)`
2. Each frame: `currentZ = normalize(lerp(oldZ, newZ, alpha))`
3. L2 normalization keeps z on the unit hypersphere
4. The LLC automatically produces transitional motion

### Incremental checkpoint
After Phase 5: An NPC executes a multi-step FSM sequence: approach target crouching → sprint the last few meters → perform strike → return to idle. Transitions between behaviors are smooth. Build compiles and runs.

---

## Phase 6: Training Pipeline (Python, Separate from Engine)

Training happens externally in Python using Isaac Gym (or equivalent physics sim). The engine only runs inference.

### 6.1 Training Setup

Use the NVlabs/CALM repository as the training base:
- Isaac Gym for GPU-accelerated physics
- PyTorch for neural network training
- rl_games framework for PPO + adversarial training

**Motion data**: Use the Reallusion character dataset provided with CALM, or retarget custom mocap data to the engine's skeleton using `calm/poselib/retarget_motion.py`.

### 6.2 Training Phases

**Phase A — Pre-train LLC** (the most compute-intensive step):
- Train encoder + LLC + conditional discriminator on motion dataset
- Loss: conditional discriminator reward only (no task reward)
- Encoder regularization: alignment (nearby frames encode similarly) + uniformity (spread across hypersphere)
- Output: LLC weights, encoder weights, latent library

**Phase B — Precision-train HLC** (per-task):
- Freeze LLC
- Train HLC on specific tasks (heading, location, strike)
- Reward: task-specific (velocity matching, direction alignment, etc.)
- Style conditioning: cosine distance between HLC output latent and requested style latent
- Output: HLC weights per task

### 6.3 Model Export

Python export script converts PyTorch models to engine-loadable format:

```python
def export_calm_model(checkpoint_path, output_dir):
    model = load_checkpoint(checkpoint_path)

    # Export LLC policy (style MLP + main MLP + mu head)
    export_mlp(model.actor, f"{output_dir}/llc_policy.bin")

    # Export encoder
    export_mlp(model.encoder, f"{output_dir}/encoder.bin")

    # Export HLC (per task)
    export_mlp(model.hlc, f"{output_dir}/hlc_{task}.bin")

    # Export latent library (pre-encoded motion clips)
    export_latent_library(model.encoder, motion_dataset,
                          f"{output_dir}/latent_library.json")
```

The latent library JSON maps clip names to 64D vectors with semantic tags:
```json
{
  "behaviors": [
    {"clip": "walk_forward.npy", "tags": ["walk"], "latent": [0.12, -0.03, ...]},
    {"clip": "run_fast.npy", "tags": ["run"], "latent": [-0.08, 0.15, ...]},
    ...
  ]
}
```

### 6.4 Skeleton Retargeting

The training character (from Isaac Gym) will differ from the engine's character skeleton. A retargeting map is needed:

```json
{
  "training_to_engine_joint_map": {
    "pelvis": "Hips",
    "left_thigh": "LeftUpLeg",
    "left_shin": "LeftLeg",
    ...
  },
  "scale_factor": 1.0
}
```

The `CALMCharacterConfig` uses this mapping to translate between observation/action DOF ordering and engine joint indices.

### Incremental checkpoint
After Phase 6: A Python training run completes on a small motion dataset. Models are exported. The C++ engine loads them and produces correct motion. End-to-end pipeline validated.

---

## Phase 7: Integration with Existing Animation Systems

### 7.1 CALM as an Animation Source

CALM-driven characters should integrate with the existing animation pipeline (IK, layers, GPU skinning) rather than replacing it:

```
CALMController
    ↓ (produces SkeletonPose)
AnimationLayerController
    ↓ (optional additive layers: aim, lean)
IK Solvers
    ↓ (foot placement, look-at)
computeBoneMatrices()
    ↓
GPU Skinning (existing SkinnedMeshRenderer)
```

The CALMController outputs a `SkeletonPose` that feeds into the same pipeline the AnimationStateMachine currently uses.

### 7.2 Hybrid Mode

Allow mixing CALM-driven and clip-driven animation:
- CALM drives locomotion (lower body + root motion)
- Clip-driven animation handles upper body actions (weapon swing, gesture)
- AnimationLayerController blends them with bone masks

### 7.3 NPC Archetype Integration

Extend `AnimationArchetypeManager` to support CALM archetypes:
```cpp
struct CALMArchetype {
    uint32_t id;
    Skeleton skeleton;
    CALMLowLevelController llc;      // Shared across all NPCs of this type
    CALMLatentSpace latentSpace;     // Shared latent library
    CALMCharacterConfig config;
};

struct CALMNPCInstance {
    uint32_t archetypeId;
    CALMController controller;  // Per-NPC latent state + obs history
};
```

The LLC weights and latent library are shared (read-only). Each NPC only owns its current latent state, observation history, and step counters — lightweight per-instance data.

### 7.4 LOD Integration

Use the existing NPC LOD system:
- **Real** (<25m): Full CALM inference every frame
- **Bulk** (25-50m): CALM inference every N frames, interpolate poses between
- **Virtual** (>50m): Freeze pose or use simple clip playback

### Incremental checkpoint
After Phase 7: Multiple NPCs with different CALM behaviors coexist with clip-animated NPCs. Foot IK works on CALM-driven characters. LOD transitions are smooth. Build compiles and runs.

---

## Phase 8: GPU Batch Inference for Crowds

When NPC count exceeds ~50, move MLP inference to a Vulkan compute shader.

### 8.1 Compute Shader MLP (`shaders/calm_inference.comp`)

```glsl
// SSBO: weights for each layer
// SSBO: input observations (batched, one row per NPC)
// SSBO: output actions (batched)

layout(local_size_x = 64) in;

void main() {
    uint npcIdx = gl_GlobalInvocationID.x;
    // Read observation for this NPC
    // Forward through MLP layers (matmul + ReLU)
    // Write action output
}
```

### 8.2 Integration

- Upload all NPC observations to an SSBO each frame
- Dispatch compute shader in the ComputeStage (alongside terrain LOD, grass sim)
- Read back actions and apply to skeletons
- Fits naturally into the existing FrameGraph pipeline

### Incremental checkpoint
After Phase 8: 100+ NPCs running CALM with GPU inference. Frame time for ML inference is under 1ms. Build compiles and runs.

---

## File Structure Summary

```
src/ml/
    Tensor.h                    — Lightweight tensor operations
    MLPNetwork.h/cpp            — Feedforward MLP inference
    ModelLoader.h/cpp           — Load exported PyTorch weights
    GPUInference.h/cpp          — Vulkan compute shader inference (Phase 8)
    CALMObservation.h/cpp       — Observation extraction from skeleton/physics
    CALMCharacterConfig.h       — DOF mapping, PD gains, joint config
    CALMActionApplier.h/cpp     — Convert actions to skeleton pose or torques
    CALMLowLevelController.h/cpp — LLC policy network
    CALMLatentSpace.h/cpp       — Encoder, latent library, interpolation
    CALMController.h/cpp        — Per-character CALM state + update loop
    CALMHighLevelController.h/cpp — HLC base + task-specific controllers
    CALMBehaviorFSM.h/cpp       — FSM for behavior composition

shaders/
    calm_inference.comp         — GPU batched MLP forward pass (Phase 8)

tools/
    calm_export.py              — Export PyTorch CALM models to engine format
    calm_encode_library.py      — Pre-encode motion clips into latent library

data/
    calm/
        models/                 — Exported LLC, encoder, HLC weight files
        latent_library.json     — Pre-encoded behavior latents with tags
        retarget_map.json       — Training skeleton → engine skeleton mapping
```

## Dependencies

- No new C++ library dependencies (MLP inference is hand-written)
- Python training: Isaac Gym, PyTorch, rl_games (existing NVlabs/CALM repo)
- Optional: ONNX Runtime as alternative to hand-written inference (adds dependency but simplifies model loading)

## Key Hyperparameters (from paper defaults)

| Parameter | Value |
|---|---|
| Latent dimension | 64 |
| Latent reset interval | 10–150 steps |
| LLC hidden layers | [1024, 1024, 512] |
| Style MLP layers | [512, 256] (with tanh) |
| Encoder hidden layers | [1024, 1024, 512] |
| PD gains (kp/kd) | 40.0 / 5.0 |
| Interpolation on unit hypersphere | L2 normalize after lerp |

## Testing Strategy

Each phase has an incremental checkpoint. Manual testing approach:

1. **Phase 1**: Load a reference MLP, compare C++ output to Python output on identical input. Print max absolute error — should be < 1e-5.
2. **Phase 2**: Spawn a character, print extracted observation vector each frame. Compare dimensions and value ranges to CALM paper Table 1.
3. **Phase 3**: Load pre-trained LLC, set latent to known "walk" encoding. Visually confirm character walks. Switch to "run" latent — character runs. Interpolate — smooth transition.
4. **Phase 4**: Set a target location 20m away. HLC should output latents that make the character walk/run toward it and stop.
5. **Phase 5**: Define a 3-state FSM (approach → strike → idle). Watch NPC execute the sequence with smooth transitions.
6. **Phase 6**: Full round-trip: train in Python, export, load in C++, verify identical behavior to Isaac Gym rendering.
7. **Phase 7**: Mix CALM NPCs with clip-animated NPCs. Verify foot IK, LOD transitions, and layer blending all work.
8. **Phase 8**: Spawn 100 CALM NPCs. Verify GPU inference produces same results as CPU. Profile frame time.
