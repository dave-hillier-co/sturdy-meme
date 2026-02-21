# UniCon Implementation Plan

[UniCon: Universal Neural Controller for Physics-based Character Motion](https://research.nvidia.com/labs/toronto-ai/unicon/) — a target-frame-tracking approach to physics-based character animation using reinforcement learning.

UniCon is implemented as a strategy within the shared `src/ml/` framework, alongside CALM. Both share the general-purpose ML infrastructure (Tensor, MLPNetwork, ModelLoader, CharacterConfig) and training environment (VecEnv, CharacterEnv).

## Architecture

```
src/ml/                     # Shared ML infrastructure
├── Tensor.h                # Lightweight tensor for inference
├── MLPNetwork.h            # Generic MLP (ReLU/Tanh/ELU activations)
├── ModelLoader.h           # Binary weight loading (.bin format)
├── CharacterConfig.h       # DOF mappings, observation/action dims
├── ObservationExtractor.h  # AMP-style temporal observation stacking
├── ActionApplier.h         # Policy actions → skeleton poses
├── calm/                   # CALM strategy (latent-space behaviors)
│   ├── Controller.h        # Latent management + LLC inference
│   └── LowLevelController.h
└── unicon/                 # UniCon strategy (target-frame tracking)
    ├── Controller.h        # Obs → MLP → torques loop + perturbation testing
    ├── StateEncoder.h      # UniCon Eq.4 observation encoding
    ├── MotionScheduler.h   # Target frame production (MocapScheduler, KeyboardScheduler)
    └── RagdollRenderer.h   # Physics → bone matrices → GPU skinning

src/training/               # Shared training infrastructure
├── CharacterEnv.h          # Single-character RL environment
├── VecEnv.h                # Vectorized N-character training
├── RewardComputer.h        # Task-based reward functions
├── MotionLibrary.h         # FBX motion loading for resets
└── bindings.cpp            # pybind11 Python bindings
```

## Phase Summary

| Phase | Topic | Status |
|-------|-------|--------|
| 1 | Extended Physics API | **Complete** |
| 2 | Articulated Body System | **Complete** |
| 3 | Humanoid State Encoding | **Complete** |
| 4 | MLP Inference Engine | **Complete** (shared `ml::MLPNetwork` with ELU support) |
| 5 | Low-Level Motion Executor | **Complete** (`ml::unicon::Controller`) |
| 6 | High-Level Motion Schedulers | **Complete** (MocapScheduler, KeyboardScheduler) |
| 7 | Rendering Integration | **Complete** (RagdollRenderer, PhysicsBased mode, debug overlay) |
| 8 | Training Pipeline | **Complete** (shared VecEnv + pybind11) |
| 9 | NPC Integration | **Complete** (PhysicsBased LOD tier, transition blending) |
| 10 | Polish & Advanced | **Complete** (perturbation, terrain via physics) |

## Testing

### Ragdoll Rendering
1. Run the application
2. Press **R** to spawn a ragdoll — it should appear as a skinned character falling under gravity
3. The UniCon controller applies torques to try to maintain the target pose
4. Spawn multiple ragdolls (press R repeatedly) to test multi-character rendering

### Motion Schedulers
To test motion schedulers, attach one to the controller:
```cpp
auto scheduler = std::make_unique<ml::unicon::MocapScheduler>();
scheduler->configure(skeleton, 1, 1.0f/30.0f);
scheduler->setClip(&walkClip);
uniconController_.setScheduler(std::move(scheduler));
```

### Perturbation Testing
Enable auto-perturbation for stress-testing ragdoll recovery:
```cpp
uniconController_.setAutoPerturbation(3.0f, 300.0f); // every 3s, 300N max
```

### Debug Overlay
Enable debug visualization to see ragdoll body parts and target poses:
```cpp
ragdollRenderer_.setDebugEnabled(true);
ragdollRenderer_.drawDebugOverlay(ragdolls_, physics(), debugLines, &controller.getTargetFrames());
```

## Next Steps

1. **Train a policy**: Use pybind11 VecEnv with PPO to train a UniCon policy
2. **Convert motion data**: `python tools/unicon_training/fetch_training_data.py assets/characters/fbx/ -o assets/motions/`
3. **Deploy trained weights**: Export via `tools/ml/export.py` → `generated/unicon/policy_weights.bin`

## Key References

- [UniCon Paper](https://nv-tlabs.github.io/unicon/resources/main.pdf)
- [UniCon Project Page](https://research.nvidia.com/labs/toronto-ai/unicon/)
