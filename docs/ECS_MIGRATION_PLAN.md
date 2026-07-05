# ECS Migration Plan

A phased plan for completing ECS integration across the codebase. Each phase produces a working, testable state.

> **Status:** Planning history; phases have been implemented out of order and this document is
> not a current-state architecture description. See [ARCHITECTURE.md](ARCHITECTURE.md) for the
> implementation and [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md) for the remaining
> dual-authority problem. Verify each item against the code before treating it as outstanding.

## Current State

Verified against the code on 2026-07-01.

**Completed:**
- EnTT-based ECS with `ecs::World` wrapper
- Core components (Transform, Bounds, Visibility, Tags, LODController)
- Hierarchical transforms with Parent/Children
- Bone attachments (sword/shield on player)
- Frustum culling system
- EntityFactory for converting renderables
- GPU scene-buffer population from ECS render components
- GPU culling and indirect scene rendering path
- ECS physics-body components for migrated scene entities
- **Phase 1.2 (special-object indices):** replaced by entity handles + tag components
  (`PlayerTag`, `CapeTag`, `WeaponTag`, …); no `playerIndex`/`emissiveOrbIndex`/`capeIndex`
- **Phase 2 (NPC migration):** NPCs are ECS entities (`npcEntities_`); the parallel
  `templateIndices`/`positions`/`renderableIndices` arrays are gone
- **Phase 6 (Renderable elimination):** monolithic `Renderable`/`RenderableBuilder.h` removed;
  PBR data in `PBRProperties`, tree typing in dedicated components. `ecs::RenderData` remains
  only as a transient GPU-feed struct produced by `extractRenderData`

**The Gap: CLOSED (2026-07-01).** The persistent `RenderData` mirror
(`SceneBuilder::sceneObjects` + `entityToRenderableIndex_` + `getRenderableForEntity`) and the
index-aligned `scenePhysicsBodies` vector are removed; opacity and transform no longer dual-write;
scatter is a documented immutable instance source; and the CPU-fallback and GPU-indirect paths
enumerate the same entity set. ECS components are the sole authority; `ecs::RenderData` remains
only as a transient GPU-feed struct. See the [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
"ECS and Render-Data Dual Authority" section (marked Resolved) for the per-slice detail and
verification.

---

## Phase 1: Eliminate Parallel Index Arrays

**Goal:** Remove fragile index-based mappings between systems.

### 1.1 Physics Body Component Integration

**Current:** `scenePhysicsBodies` vector in Application with manual index sync to `sceneObjects`.

**Change:**
- `PhysicsBody` component already exists - make it authoritative
- Remove `scenePhysicsBodies` vector from Application
- System queries `PhysicsBody` + `Transform` directly

**Files to modify:**
- `src/scene/Application.h` - Remove `scenePhysicsBodies_` member
- `src/scene/Application.cpp` - Update physics sync to use ECS query
- `src/ecs/Systems.h` - Ensure `syncPhysicsTransforms` is complete

**Testing:** Run game, verify physics objects (player, dynamic props) still move correctly. Check collision detection still works.

### 1.2 Replace Hardcoded Special Object Indices — DONE (verified 2026-07-01)

Implemented: role entity handles (`playerEntity_`, `emissiveOrbEntity_`, `capeEntity_`) and tag
components replace the hardcoded indices. Original plan text retained below for history.

**Current:** `SceneBuilder` tracks `playerIndex`, `emissiveOrbIndex`, `capeIndex`, etc.

**Change:**
- Add tag components: `PlayerTag`, `CapeTag`, `FlagTag`, `WeaponTag`
- Query by tag instead of storing indices
- Remove index members from SceneBuilder

**New components in `Components.h`:**
```cpp
struct PlayerTag {};
struct CapeTag {};
struct FlagTag {};
struct PoleTag {};
struct OrbTag {};
struct WeaponTag { WeaponSlot slot; };
```

**Files to modify:**
- `src/ecs/Components.h` - Add tag components
- `src/scene/SceneBuilder.h/cpp` - Tag entities instead of storing indices
- `src/scene/Application.cpp` - Query by tag where indices were used

**Testing:** Verify player camera follow works, cape physics works, flag animation works, weapon attachment works.

### 1.3 NPC Renderable Index Elimination — DONE (verified 2026-07-01)

Implemented as part of Phase 2: NPCs are ECS entities (`npcEntities_`), no `renderableIndices`.
Original plan text retained below for history.

**Current:** `NPCData::renderableIndices` maps NPC index to scene object index.

**Change:**
- Each NPC becomes an entity with `NPCTag` + `AnimationPlayback` components
- NPC entity directly owns its `Transform`, no indirection
- Remove `renderableIndices` from NPCData

**Files to modify:**
- `src/npc/NPCData.h` - Remove `renderableIndices`
- `src/npc/NPCSimulation.h/cpp` - Create/query NPC entities
- `src/ecs/Components.h` - Add `NPCTag`, verify `AnimationPlayback` exists

**Testing:** NPCs render in correct positions, animations play, LOD transitions work.

---

## Phase 2: NPC System Migration — DONE (verified 2026-07-01)

**Goal:** Move NPCs from parallel arrays to proper ECS entities.

Implemented: NPCs are ECS entities (`src/npc/NPCSimulation.h:173`); the parallel-primitive arrays
below no longer exist and `NPCRenderData` is a transient per-frame draw struct. Original plan text
retained below for history.

### 2.1 NPC Component Design

**Current NPCData arrays to replace:**
```cpp
std::vector<uint32_t> templateIndices;      // → NPCArchetype component
std::vector<glm::vec3> positions;           // → Transform component (exists)
std::vector<float> yawDegrees;              // → Transform component rotation
std::vector<uint8_t> lodLevels;             // → LODController component (exists)
std::vector<uint16_t> framesSinceUpdate;    // → LODController.frameCounter
std::vector<AnimState> animStates;          // → AnimationPlayback component
std::vector<std::vector<glm::mat4>> cachedBoneMatrices;  // → BoneCache component
```

**New/updated components:**
```cpp
struct NPCArchetype {
    uint32_t templateIndex;  // Index into shared character templates
};

struct AnimationPlayback {
    AnimState state;
    float blendWeight;
    float playbackTime;
};

struct BoneCache {
    std::vector<glm::mat4> matrices;
    uint32_t lastUpdateFrame;
};
```

**Files to modify:**
- `src/ecs/Components.h` - Add/update components
- `src/npc/NPCData.h` - Deprecate parallel arrays

### 2.2 Shared Animation Archetypes

**Current:** Each NPC owns full `AnimatedCharacter` instance.

**Change:**
- Extract shared `AnimationArchetype` (skeleton + clips + state machine)
- NPCs reference archetype by ID, own only playback state
- Dramatically reduces memory for 50+ NPCs

**New structure:**
```cpp
// Shared (10 character types, not 54 instances)
struct AnimationArchetypeData {
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
    AnimationStateMachine stateMachine;
};

class AnimationArchetypeManager {
    std::vector<AnimationArchetypeData> archetypes_;
public:
    ArchetypeId loadArchetype(const std::string& characterType);
    const AnimationArchetypeData& get(ArchetypeId id) const;
};
```

**Files to modify:**
- `src/animation/AnimationArchetype.h` (new)
- `src/npc/NPCSimulation.h/cpp` - Use archetypes instead of per-NPC AnimatedCharacter

**Testing:** All NPC types animate correctly, memory usage reduced (profile with 50+ NPCs).

### 2.3 NPC System Rewrite

**Replace NPCSimulation internals:**
- `spawnNPCs()` creates ECS entities with components
- `update()` runs ECS systems instead of iterating arrays
- Remove all parallel arrays from NPCData

**Systems to implement:**
```cpp
void updateNPCAnimations(World& world, AnimationArchetypeManager& archetypes, float dt);
void updateNPCBoneCache(World& world, AnimationArchetypeManager& archetypes);
void syncNPCTransformsToRenderables(World& world, std::vector<Renderable>& renderables);
```

**Files to modify:**
- `src/npc/NPCSimulation.h/cpp` - Complete rewrite
- `src/ecs/Systems.h` - Add NPC systems
- `src/scene/Application.cpp` - Update NPC initialization

**Testing:**
- NPCs spawn at correct positions
- Walk/idle animations play
- LOD transitions (Real/Bulk/Virtual) work at distance
- Performance equal or better with 50+ NPCs

---

## Phase 3: GPU-Driven Rendering Foundation

**Goal:** Enable indirect draw calls driven by ECS component data.

### 3.1 Transform SSBO Upload

**Current:** Transforms in Renderable struct, uploaded per-draw via push constants.

**Change:**
- Bulk upload `Transform` components to GPU SSBO
- Shader reads transform by instance ID
- Reduce per-draw data to just indices

**New infrastructure:**
```cpp
class TransformBuffer {
    vk::Buffer ssbo_;
    std::vector<glm::mat4> staging_;
public:
    void upload(const World& world);  // Query all Transform, bulk copy
    vk::Buffer buffer() const { return ssbo_; }
};
```

**Files to modify:**
- `src/core/TransformBuffer.h/cpp` (new)
- `src/scene/Application.cpp` - Upload transforms each frame
- Shaders - Read from SSBO instead of push constant

**Testing:** Scene renders identically, measure upload time vs previous iteration.

### 3.2 GPU Visibility Compute Pass

**Current:** CPU frustum culling with `Visible` tag.

**Change:**
- Port culling to compute shader
- Read `BoundingSphere` SSBO, frustum planes uniform
- Output visibility bitmask or compacted visible indices

**Leverage existing:** `HiZSystem` already has GPU culling infrastructure for tree impostors.

**Files to modify:**
- `shaders/visibility_cull.comp` (new)
- `src/culling/GPUCullPass.h/cpp` (new)
- `src/scene/Application.cpp` - Add compute pass before rendering

**Testing:** Render count matches CPU culling, GPU culling time < CPU time.

### 3.3 Indirect Draw Integration

**Current:** Loop over renderables, call `vkCmdDrawIndexed` per object.

**Change:**
- Generate `VkDrawIndexedIndirectCommand` buffer from visibility results
- Use `vkCmdDrawIndexedIndirectCount` for variable-length draws
- Single submission for all visible scene objects

**Files to modify:**
- `src/core/IndirectDrawBuffer.h/cpp` (new)
- `src/core/passes/HDRPassRecorder.cpp` - Add indirect draw path
- Shaders - Use gl_InstanceIndex to fetch transform

**Testing:**
- Scene renders correctly with indirect draws
- Draw call count reduced (measure with RenderDoc)
- Add 1000+ objects, verify performance scales

---

## Phase 4: Sparse Render Features

**Goal:** Add render features without modifying core structs.

### 4.1 Material Component Migration

**Current:** `Renderable` has `roughness`, `metallic`, `emissiveIntensity` for every object.

**Change:**
- `PBRProperties` component already exists
- Make it the source of truth, remove from Renderable
- Only entities needing non-default values have the component

**Files to modify:**
- `src/core/RenderableBuilder.h` - Remove PBR fields
- `src/core/passes/HDRPassRecorder.cpp` - Query PBRProperties component
- `src/ecs/EntityFactory.cpp` - Set PBRProperties only when non-default

### 4.2 Tree-Specific Component Cleanup

**Current:** `Renderable` has `barkType`, `leafType`, `leafInstanceIndex`, `treeInstanceIndex` for every object.

**Change:**
- `TreeData` component already exists
- Remove tree fields from Renderable
- Only tree entities have TreeData component

**Files to modify:**
- `src/core/RenderableBuilder.h` - Remove tree fields
- Tree rendering code - Query TreeData component

### 4.3 Dynamic Effect Components

**Implement new sparse components for runtime effects:**

```cpp
struct WetnessOverlay {
    float wetness;  // 0.0 = dry, 1.0 = soaked
};

struct DamageOverlay {
    float damage;   // 0.0 = pristine, 1.0 = destroyed
};

struct SelectionOutline {
    glm::vec3 color;
    float thickness;
};
```

**Files to modify:**
- `src/ecs/Components.h` - Add effect components
- Shaders - Sample effect overlays
- `src/core/passes/` - Outline pass for selected entities

**Testing:**
- Add WetnessOverlay to some entities, verify visual change
- Add SelectionOutline to entity, verify outline renders
- Verify entities without components render unchanged

---

## Phase 5: Lighting System Integration

**Goal:** Lights as ECS entities with spatial queries.

### 5.1 Light Components

**New components:**
```cpp
struct PointLight {
    glm::vec3 color;
    float intensity;
    float radius;
};

struct SpotLight {
    glm::vec3 color;
    float intensity;
    float innerAngle;
    float outerAngle;
    glm::vec3 direction;
};

struct DirectionalLight {
    glm::vec3 color;
    float intensity;
    glm::vec3 direction;
};

struct LightFlicker {
    float frequency;
    float amplitude;
};
```

**Files to modify:**
- `src/ecs/Components.h` - Add light components
- `src/lighting/LightManager.h/cpp` - Query light entities

### 5.2 Light Culling Integration

**Change:**
- Light entities have Transform + BoundingSphere (for point/spot)
- Reuse visibility system for light culling
- Only visible lights uploaded to GPU

**Files to modify:**
- `src/lighting/` - Use ECS queries for light collection
- Light shaders - Read from light SSBO

**Testing:**
- Add point lights as entities, verify lighting works
- Move camera, verify off-screen lights culled
- Add LightFlicker to torch, verify flickering

---

## Phase 6: Full Renderable Elimination — LARGELY DONE (verified 2026-07-01)

**Goal:** Remove Renderable struct entirely. All render data in components.

Implemented: the monolithic `Renderable` struct and `RenderableBuilder.h` are removed; PBR data
lives in `PBRProperties` and tree typing in dedicated components. The residual work is not this
struct but the persistent `ecs::RenderData` mirror in `SceneBuilder` — tracked under the
dual-authority issue in [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md). `ecs::RenderData` itself
is retained as a legitimate transient GPU-feed struct. Original plan text retained below for
history.

### 6.1 Component Completeness Audit

**Ensure all Renderable fields have component equivalents:**

| Renderable Field | Component |
|-----------------|-----------|
| transform | Transform |
| mesh | MeshRef |
| materialId | MaterialRef |
| roughness/metallic/emissive | PBRProperties |
| barkType/leafType/etc | TreeData |
| hueShift | HueShift |
| boundingSphere | BoundingSphere |
| isAnimated/boneMatrices | SkinnedMesh + BoneCache |

### 6.2 HDRPassRecorder Migration

**Current:** Iterates `std::vector<Renderable>`.

**Change:**
- Query ECS for renderable entities
- Group by mesh+material for instancing
- Use indirect draws for batch submission

**Files to modify:**
- `src/core/passes/HDRPassRecorder.h/cpp` - Query-based rendering
- `src/scene/Application.cpp` - Remove renderable vector maintenance

### 6.3 Remove Renderable Struct

**Final cleanup:**
- Delete `RenderableBuilder.h`
- Remove `sceneObjects_` vector from Application
- All entity creation through EntityFactory

**Testing:**
- Full scene renders correctly
- All entity types work (static, animated, trees, NPCs)
- Performance equal or better

---

## Validation Checkpoints

After each phase:

1. **Build check:** `cmake --preset debug && cmake --build build/debug`
2. **Run check:** `./run-debug.sh` - no crashes
3. **Visual check:** Scene looks correct, no missing objects
4. **Performance check:** Frame time stable or improved

---

## Dependencies Between Phases

```
Phase 1 (Index Elimination)
    ↓
Phase 2 (NPC Migration)
    ↓
Phase 3 (GPU-Driven) ← Can start after Phase 1
    ↓
Phase 4 (Sparse Features) ← Can start after Phase 1
    ↓
Phase 5 (Lighting) ← Can start after Phase 3
    ↓
Phase 6 (Renderable Elimination) ← Requires all above
```

Phases 3, 4, and 5 can proceed in parallel after Phase 1.

---

## Risk Mitigation

**Risk:** Breaking existing functionality during migration.

**Mitigation:**
- Keep legacy path working alongside ECS path
- Feature flags to switch between old/new
- Remove legacy code only after new path validated

**Risk:** Performance regression from ECS overhead.

**Mitigation:**
- Profile before/after each phase
- EnTT is cache-friendly by design
- GPU-driven rendering in Phase 3 should offset any overhead

**Risk:** Animation system complexity during NPC migration.

**Mitigation:**
- Phase 2.2 (archetypes) can be deferred if needed
- Per-NPC AnimatedCharacter still works, just less efficient
- Migrate incrementally by NPC type
