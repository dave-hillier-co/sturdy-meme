# DI Migration Plan: Eliminating the RendererSystems Service Locator

## Overview

**Goal:** Replace the `RendererSystems` service locator with proper dependency injection where each consumer declares its specific dependencies.

**Current State:**
- `RendererSystems` holds ~50 systems, passed around as `RendererSystems&`
- 254 occurrences across 57 files
- Consumers can access any system - no explicit dependency declaration

**Target State:**
- Each consumer takes only the systems it needs
- System ownership stays in one place (not passed around)
- Dependencies are explicit and compile-time checked

---

## Phase 1: Expand Resources Pattern (Pass Recorders)

The codebase already has `HDRPassResources` and `ShadowPassResources` that declare specific dependencies. Expand this pattern.

### 1.1 Complete HDRPassResources Migration

**Current:** `HDRPassRecorder` accepts both `HDRPassResources` and `RendererSystems&`

**Change:** Remove `RendererSystems&` constructor overload

```cpp
// Remove this constructor:
explicit HDRPassRecorder(RendererSystems& systems);

// Keep only this:
explicit HDRPassRecorder(const HDRPassResources& resources);
```

**Call site change (Renderer.cpp):**
```cpp
// Before:
hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(*systems_);

// After:
HDRPassResources hdrResources;
hdrResources.profiler = &systems_->profiler();
hdrResources.postProcess = &systems_->postProcess();
// ... explicitly populate all fields
hdrPassRecorder_ = std::make_unique<HDRPassRecorder>(hdrResources);
```

**Files:** `HDRPassRecorder.h/cpp`, `Renderer.cpp`

### 1.2 Complete ShadowPassResources Migration

Same pattern as HDRPassResources.

**Files:** `ShadowPassRecorder.h/cpp`, `Renderer.cpp`

### 1.3 Create WaterPassResources

**Systems needed by WaterPasses:**
- `profiler` - GPU profiling
- `waterGBuffer` - G-buffer pass
- `water` - Water mesh
- `waterTileCull` - Optional culling
- `ssr` - Screen-space reflections
- `postProcess` - HDR color/depth views

```cpp
struct WaterPassResources {
    Profiler* profiler = nullptr;
    WaterGBuffer* waterGBuffer = nullptr;
    WaterSystem* water = nullptr;
    WaterTileCull* waterTileCull = nullptr;  // Optional
    SSRSystem* ssr = nullptr;
    PostProcessSystem* postProcess = nullptr;

    bool isValid() const;
    bool hasWaterTileCull() const { return waterTileCull != nullptr; }
};
```

**Files:** New `WaterPassResources.h/cpp`, update `WaterPasses.h/cpp`

### 1.4 Create ComputePassResources

**Systems needed by ComputePasses (most complex):**
- `profiler`, `terrain`, `catmullClark`, `displacement`, `grass`
- `weather`, `snowMask`, `volumetricSnow`
- `treeSystem`, `treeLOD`, `impostorCull`
- `hiZ`, `flowMap`, `foam`, `froxel`
- `atmosphereLUT`, `cloudShadow`, `ssr`, `waterTileCull`

**Files:** New `ComputePassResources.h/cpp`, update `ComputePasses.h/cpp`

### 1.5 Create PostPassResources

**Systems needed:**
- `profiler`, `hiZ`, `bloom`, `postProcess`, `bilateralGrid`

**Files:** New `PostPassResources.h/cpp`, update `PostPasses.h/cpp`

---

## Phase 2: Updater Dependencies

Updaters modify state and need write access. Create focused input structs.

### 2.1 AtmosphereUpdaterDeps

```cpp
struct AtmosphereUpdaterDeps {
    Profiler& profiler;
    WindSystem& wind;
    WeatherSystem& weather;
    SnowMaskSystem& snowMask;
    VolumetricSnowSystem& volumetricSnow;
    EnvironmentSettings& environmentSettings;
};
```

**Files:** Update `AtmosphereUpdater.h/cpp`

### 2.2 EnvironmentUpdaterDeps

```cpp
struct EnvironmentUpdaterDeps {
    Profiler& profiler;
    WeatherSystem& weather;
    TerrainSystem& terrain;
    WaterSystem& water;
    VolumetricSnowSystem& volumetricSnow;
    PostProcessSystem& postProcess;
    FroxelSystem& froxel;
};
```

**Files:** Update `EnvironmentUpdater.h/cpp`

### 2.3 VegetationUpdaterDeps

```cpp
struct VegetationUpdaterDeps {
    Profiler& profiler;
    DisplacementSystem& displacement;
    GrassSystem& grass;
    TreeSystem* tree;  // Optional
    TreeRenderer* treeRenderer;  // Optional
    TreeLODSystem* treeLOD;  // Optional
    GlobalBufferManager& globalBuffers;
    LeafSystem& leaf;
};
```

**Files:** Update `VegetationUpdater.h/cpp`

### 2.4 UBOUpdaterDeps

```cpp
struct UBOUpdaterDeps {
    TimeSystem& time;
    UBOBuilder& uboBuilder;
    CelestialCalculator& celestial;
    WaterSystem& water;
    ShadowSystem& shadow;
    IEnvironmentControl& environmentControl;
    WeatherSystem& weather;
    GlobalBufferManager& globalBuffers;
    SceneManager& scene;
    PostProcessSystem& postProcess;
};
```

**Files:** Update `UBOUpdater.h/cpp`

### 2.5 FrameUpdater Refactor

```cpp
class FrameUpdater {
public:
    FrameUpdater(
        Profiler& profiler,
        AtmosphereUpdaterDeps atmoDeps,
        EnvironmentUpdaterDeps envDeps,
        VegetationUpdaterDeps vegDeps
    );

    void updateAllSystems(uint32_t frameIndex, const FrameData& data);
};
```

---

## Phase 3: SystemWiring Dependencies

SystemWiring performs cross-system descriptor updates. Create a focused struct.

```cpp
struct WiringTargets {
    // Core
    TerrainSystem& terrain;
    GlobalBufferManager& globalBuffers;
    ShadowSystem& shadow;

    // Vegetation
    GrassSystem& grass;
    WindSystem& wind;
    ScatterSystem& rocks;
    ScatterSystem* detritus;  // Optional
    LeafSystem& leaf;

    // Atmosphere
    CloudShadowSystem& cloudShadow;
    FroxelSystem& froxel;
    WeatherSystem& weather;

    // Snow
    SnowMaskSystem& snowMask;
    VolumetricSnowSystem& volumetricSnow;
    EnvironmentSettings& environmentSettings;

    // Water
    WaterSystem& water;

    // Animation
    SkinnedMeshRenderer& skinnedMesh;
};
```

**Files:** Update `SystemWiring.h/cpp`

---

## Phase 4: FrameDataBuilder Dependencies

Already mostly read-only. Create focused inputs.

```cpp
struct FrameDataSources {
    const TimeSystem& time;
    const GlobalBufferManager& globalBuffers;
    const TerrainSystem& terrain;
    const WindSystem& wind;
    const WeatherSystem& weather;
    const EnvironmentSettings& environmentSettings;
    const IPlayerControl& playerControl;
    const PostProcessSystem& postProcess;
};
```

---

## Phase 5: FrameGraphBuilder Refactor

FrameGraphBuilder orchestrates all passes. It needs all the Resources structs.

```cpp
struct FrameGraphResources {
    ComputePassResources compute;
    ShadowPassResources shadow;
    WaterPassResources water;
    HDRPassResources hdr;
    PostPassResources post;

    Profiler& profiler;  // Shared
};

class FrameGraphBuilder {
public:
    static bool build(
        FrameGraph& graph,
        const FrameGraphResources& resources,
        const Callbacks& callbacks,
        const State& state
    );
};
```

---

## Phase 6: Control Subsystems

These hold RendererSystems& internally. Refactor to take specific systems.

### 6.1 DebugControlSubsystem

```cpp
class DebugControlSubsystem : public IDebugControl {
public:
    DebugControlSubsystem(
        RoadRiverVisualization& roadRiverVis,
        std::function<PhysicsDebugRenderer*()> getPhysicsDebugRenderer
    );
};
```

### 6.2 TreeControlSubsystem

```cpp
class TreeControlSubsystem : public ITreeControl {
public:
    TreeControlSubsystem(
        TreeSystem* tree,
        TreeRenderer* treeRenderer,
        TreeLODSystem* treeLOD
    );
};
```

---

## Phase 7: Renderer Refactor

After all consumers are migrated, Renderer becomes the sole owner of systems.

```cpp
class Renderer {
private:
    // System ownership (not passed around)
    std::unique_ptr<PostProcessSystem> postProcess_;
    std::unique_ptr<ShadowSystem> shadow_;
    std::unique_ptr<TerrainSystem> terrain_;
    // ... all systems owned directly

    // Resources constructed from owned systems
    HDRPassResources buildHDRResources();
    ShadowPassResources buildShadowResources();
    // etc.

    // No more RendererSystems!
};
```

---

## Phase 8: Remove RendererSystems

Once all consumers use specific dependencies:

1. Delete `RendererSystems.h/cpp`
2. Move system ownership to Renderer or a simple `SystemOwner` class
3. Remove all `#include "RendererSystems.h"`

---

## Migration Order

Execute phases in this order to maintain working builds:

1. **Phase 1** - Pass Recorders (least coupling, Resources pattern exists)
2. **Phase 4** - FrameDataBuilder (read-only, simple)
3. **Phase 2** - Updaters (modifying, but isolated)
4. **Phase 3** - SystemWiring (complex but isolated)
5. **Phase 5** - FrameGraphBuilder (coordinates passes)
6. **Phase 6** - Control Subsystems (small, isolated)
7. **Phase 7** - Renderer refactor (owner becomes direct)
8. **Phase 8** - Delete RendererSystems

---

## Estimated Scope

| Phase | Files Changed | New Files | Complexity |
|-------|--------------|-----------|------------|
| 1.1-1.2 | 4 | 0 | Low |
| 1.3-1.5 | 6 | 6 | Medium |
| 2.1-2.5 | 10 | 0 | Medium |
| 3 | 2 | 0 | Medium |
| 4 | 2 | 0 | Low |
| 5 | 2 | 0 | Medium |
| 6 | 4 | 0 | Low |
| 7 | 2 | 0 | High |
| 8 | 57 | 0 | Low (deletion) |

**Total:** ~90 file touches, 6 new files

---

## Benefits After Migration

1. **Explicit Dependencies** - Each class declares exactly what it needs
2. **Compile-Time Checking** - Missing dependencies caught at compile time
3. **Testability** - Easy to mock specific systems for unit tests
4. **Reduced Coupling** - Changes to one system don't affect unrelated consumers
5. **Clear Architecture** - Dependency graph is explicit in the code
