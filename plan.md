# Plan: ECS Migration for Rocks and Detritus

## Current State

Rocks and detritus are managed by `ScatterSystem` + `ScatterSystemFactory`:

- **Config structs**: `RockConfig` / `DetritusConfig` hold generation parameters
- **Mesh generation**: `Mesh::createRock()`, `createBranch()`, `createForkedBranch()` create procedural meshes
- **Placement**: Poisson disk sampling (rocks) and tree-relative scattering (detritus) produce `SceneObjectInstance` vectors
- **Rendering**: `SceneMaterial` builds `Renderable` objects from instances; descriptor sets for textures
- **Physics**: Convex hull colliders created per-instance in `Application.cpp`
- **Ownership**: `RendererSystems` owns `unique_ptr<ScatterSystem>` for rocks/detritus

The existing ECS uses EnTT via `ecs::World`, with ~40 components, `EntityFactory`, and `EntityBuilder`. Trees, NPCs, and lights already have ECS integration.

## Phase 1: Define Scatter ECS Components

**Goal**: Add new component types to `Components.h`. Nothing uses them yet.

**Changes**:
- `src/ecs/Components.h` - Add the following components:

```cpp
// Tag components for scatter object types
struct RockTag {};
struct DetritusTag {};

// Mesh variation index (which mesh variant this instance uses)
struct MeshVariation {
    uint32_t index = 0;
};

// Scatter placement area - defines a region where objects are spawned
struct ScatterArea {
    glm::vec2 center = glm::vec2(0.0f);
    float radius = 80.0f;
    float minSpacing = 3.0f;       // Minimum distance between instances
    float minElevation = 0.5f;     // Minimum terrain height for placement
};

// Rock generation parameters (mirrors RockConfig fields relevant to per-area tuning)
struct RockGenerationParams {
    int variations = 5;
    int perVariation = 8;
    float minRadius = 0.3f;
    float maxRadius = 1.5f;
    float meshRoughness = 0.35f;
    float meshAsymmetry = 0.25f;
    int subdivisions = 3;
};

// Detritus generation parameters
struct DetritusGenerationParams {
    int branchVariations = 8;
    int forkedVariations = 4;
    int branchesPerVariation = 4;
    float minLength = 0.5f;
    float maxLength = 4.0f;
    float minRadius = 0.03f;
    float maxRadius = 0.25f;
    int maxTotal = 100;
};

// PBR material override for scatter systems (shared by all instances in area)
struct ScatterMaterialParams {
    float roughness = 0.7f;
    float metallic = 0.0f;
};
```

**Verification**: Build compiles. No runtime change.

---

## Phase 2: Create Placement Area Entities

**Goal**: During initialization, create ECS entities that represent the rock field and detritus zone. These entities hold the config data as components. The existing `ScatterSystem` still does all the actual work unchanged.

**Changes**:
- `src/ecs/EntityFactory.h` - Add `createScatterArea()` method that creates an entity with `Transform`, `ScatterArea`, `Children`, and the relevant tag + params components
- `src/vegetation/ScatterSystemFactory.cpp` - After `createRocks()` and `createDetritus()`, also create corresponding area entities in the ECS world
- Thread the `ecs::World&` through the creation path (add to `ScatterSystem::InitInfo` or pass separately)

**Example**:
```cpp
// In ScatterSystemFactory after creating the ScatterSystem:
Entity rockArea = factory.createScatterArea(
    rockConfig.placementCenter,
    rockConfig.placementRadius,
    rockConfig.minDistanceBetween);
world.add<RockTag>(rockArea);
world.add<RockGenerationParams>(rockArea, ...from rockConfig...);
world.add<ScatterMaterialParams>(rockArea, rockConfig.materialRoughness, rockConfig.materialMetallic);
```

**Verification**: Build compiles and runs. Area entities exist in ECS but have no effect on rendering. Can verify entities exist via debug logging or scene editor.

---

## Phase 3: Create Per-Instance Entities

**Goal**: Each placed rock/detritus instance gets a corresponding ECS entity as a child of the area entity. `ScatterSystem` still owns rendering.

**Changes**:
- After `ScatterSystem::create()` generates instances, iterate the instances and create child entities:
  - `ecs::Transform` (from the instance's computed transform matrix)
  - `MeshRef` (pointing at the mesh for this variation)
  - `MeshVariation` (the variation index)
  - `CastsShadow` tag
  - `RockTag` or `DetritusTag`
  - `BoundingSphere` (computed from mesh + scale)
  - `Parent` → area entity
- Area entity's `Children` list populated with instance entity handles
- `ScatterSystem` stores the entity handles alongside instances (or in a parallel vector)

**Verification**: Build compiles and runs. Rendering unchanged (still driven by SceneMaterial). Entity count visible in debug. Scene editor can show rock/detritus hierarchy under area entities.

---

## Phase 4: ECS-Driven Physics

**Goal**: Physics collider creation reads from ECS entities instead of `ScatterSystem::getInstances()`.

**Changes**:
- `src/scene/Application.cpp` - Replace the rock/detritus collider creation loops:
  - Query `world.view<ecs::Transform, ecs::MeshRef, ecs::RockTag>()` for rocks
  - Query `world.view<ecs::Transform, ecs::MeshRef, ecs::DetritusTag>()` for detritus
  - Create convex hull colliders from queried entities
- Remove the direct `rockSystem.getInstances()` / `rockSystem.getMeshes()` calls for physics

**Verification**: Build compiles and runs. Physics colliders created correctly. Player collision with rocks/detritus unchanged.

---

## Phase 5: ECS-Driven Renderable Building

**Goal**: `ScatterSystem` builds its `Renderable` list from ECS entity queries instead of its internal `SceneObjectInstance` vector.

**Changes**:
- Add `ecs::World*` reference to `ScatterSystem`
- Add a method `rebuildFromECS()` that:
  - Queries entities with the appropriate tag + Transform + MeshRef
  - Builds `Renderable` objects from entity components (same as `SceneMaterial::rebuildSceneObjects()` does now, but driven by ECS data)
  - Replaces the current renderable vector
- `SceneMaterial` still manages textures and descriptor sets
- Call `rebuildFromECS()` after entity creation (replaces the existing `rebuildSceneObjects()` call)

**Verification**: Build compiles and runs. Rendering visually identical. Rocks and detritus appear correctly.

---

## Phase 6: Remove Redundant Instance Storage

**Goal**: ECS is the source of truth. Remove `SceneObjectInstance` storage from the scatter path.

**Changes**:
- `ScatterSystem` no longer stores `SceneObjectInstance` vectors internally (SceneMaterial instances can be cleared after ECS entities are created)
- Remove `getInstances()` from the public API of `ScatterSystem` (physics already reads from ECS)
- Remove `getMeshes()` public access if physics no longer needs it (mesh data accessible via `MeshRef` on entities)
- Placement algorithms in `ScatterSystemFactory` still produce `SceneObjectInstance` transiently during generation, then convert to ECS entities

**Verification**: Build compiles and runs. Rendering and physics unchanged. Memory usage slightly reduced.

---

## Design Notes

### Area Entity Structure
```
RockFieldArea (entity)
  ├── Components: Transform, ScatterArea, RockGenerationParams, ScatterMaterialParams, Children, RockTag
  ├── Rock_0 (entity): Transform, MeshRef, MeshVariation, CastsShadow, RockTag, BoundingSphere, Parent
  ├── Rock_1 (entity): ...
  └── Rock_N (entity): ...

DetritusZone (entity)
  ├── Components: Transform, ScatterArea, DetritusGenerationParams, ScatterMaterialParams, Children, DetritusTag
  ├── Branch_0 (entity): Transform, MeshRef, MeshVariation, CastsShadow, DetritusTag, BoundingSphere, Parent
  └── Branch_N (entity): ...
```

### Why Area Entities
- Enable multiple rock fields / detritus zones with different parameters
- Natural parent-child grouping for batch operations (select all, delete all, relocate)
- The area entity's `ScatterArea` component can be edited to re-trigger placement
- Foundation for future streaming: load/unload scatter groups by area

### What Stays Unchanged
- Procedural mesh generation (`Mesh::createRock()`, etc.) - no ECS needed
- Texture loading and descriptor set management - stays in `ScatterSystem`/`SceneMaterial`
- Shader pipeline - rocks/detritus use the same material pipeline
- `ScatterSystemFactory` config structs - still used as input, mirrored to ECS components
- Cloud shadow binding wiring in `SystemWiring.cpp`

### Files Modified Per Phase

| Phase | Files |
|-------|-------|
| 1 | `Components.h` |
| 2 | `Components.h`, `EntityFactory.h`, `ScatterSystemFactory.cpp`, `ScatterSystem.h` (InitInfo) |
| 3 | `ScatterSystem.cpp`, `ScatterSystemFactory.cpp`, `ScatterSystem.h` |
| 4 | `Application.cpp` |
| 5 | `ScatterSystem.h`, `ScatterSystem.cpp` |
| 6 | `ScatterSystem.h`, `ScatterSystem.cpp`, `SceneMaterial.h` |
