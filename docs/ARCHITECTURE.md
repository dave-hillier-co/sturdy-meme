# Architecture

This document describes the current implementation of the Vulkan outdoor rendering engine.
It is the source of truth for existing module boundaries and runtime flow. Planned migrations
are documented separately and must not be read as descriptions of completed work.

Known architectural risks and migration priorities are tracked in
[ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md).

## System Overview

The runtime flow is:

```text
main
  -> Application
       -> gameplay, input, physics, GUI, ECS
       -> Renderer
            -> FrameUpdate
            -> PassScheduler
            -> RendererSystems
            -> VulkanContext
```

The principal responsibilities are:

| Component | Responsibility |
| --- | --- |
| `Application` | SDL lifetime, main loop, input, physics, player/camera state, GUI, ECS ownership |
| `Renderer` | Per-frame rendering entry point, frame execution, pass scheduling, renderer-wide resources |
| `RendererBuilder` | Renderer construction, subsystem initialization, dependency wiring, resize registration |
| `RendererSystems` | Ownership and typed lookup of rendering subsystems and GUI-facing control adapters |
| `VulkanContext` | Vulkan instance, surface, device, queues, allocator, swapchain, command pools, core render targets |
| `FrameUpdate` | Per-frame CPU-side renderer state updates before command recording |
| `PassScheduler` | Dependency ordering and execution of render passes |
| `SceneManager` / `SceneBuilder` | Scene assets, scene entities, legacy render-data mirror, physics-to-scene synchronization |
| `ecs::World` | Authoritative component storage for entities that have completed ECS migration |

## Application Lifetime

`main.cpp` creates an `Application`, calls `init()`, `run()`, then `shutdown()`.

`Application` owns:

- the SDL window;
- `Renderer`;
- `ecs::World`;
- physics and terrain-physics state;
- camera, player, input, and breadcrumb state;
- GUI;
- cloth, ragdolls, and ML controllers.

`Application::run()` currently coordinates gameplay and presentation directly. Its frame loop:

1. polls SDL events;
2. builds and renders the GUI;
3. updates input and camera/player intent;
4. advances character physics, ragdolls, and world physics;
5. updates terrain physics streaming;
6. synchronizes physics and ECS state;
7. updates dynamic meshes and animation;
8. calls `Renderer::render()`.

This is a working composition root, but it also contains gameplay and simulation behavior.
New gameplay features should be placed in focused systems and invoked from `Application`,
instead of adding more responsibilities to the main loop.

## Renderer Construction

`Renderer::create()` delegates construction to `RendererBuilder`.

Initialization is expressed as eight dependency-ordered tasks in
`RendererBuilder::buildInitTasks()`:

```text
core
  -> terrain
  -> snow_weather
terrain + snow_weather
  -> scene
scene
  -> vegetation
  -> atmosphere
vegetation + atmosphere
  -> water
water
  -> finalize
```

The main task contents are:

- **core**: post-processing targets, graphics pipelines, skinned rendering, global buffers, shadows;
- **terrain**: terrain resources, tile cache, meshlets, virtual texturing;
- **snow_weather**: snow mask, volumetric snow, weather, leaves;
- **scene**: scene assets/entities and material descriptor sets;
- **vegetation**: grass, wind, displacement, trees, scatter content;
- **atmosphere**: sky, LUTs, froxels, cloud shadows;
- **water**: surface rendering, displacement, flow, foam, culling, G-buffer, SSR;
- **finalize**: cross-system descriptor wiring, geometry, GPU culling, profiling, frame execution.

Both synchronous and asynchronous entry points use this task list. At present, the task bodies
are GPU/main-thread work; the async loader does not yet stage heavy CPU work in the background.
See the architecture review for the resulting limitation.

`RendererBuilder` also:

- creates control adapters used by the GUI;
- registers resize callbacks;
- registers temporal systems;
- creates shadow and HDR recorders;
- builds the render-pass dependency graph.

Renderer creation should be treated as successful only when every required subsystem, recorder,
and pass dependency has been initialized.

## Ownership and Resource Lifetime

The intended ownership tree is:

```text
Application
  -> Renderer
       -> VulkanContext
       -> RendererSystems
            -> SystemRegistry
                 -> rendering subsystems
       -> frame executor and synchronization
       -> pipelines and descriptor pool
       -> pass recorders and pass scheduler
```

Subsystems are stored as `std::unique_ptr` instances in `SystemRegistry`. Destruction is performed
in reverse registration order after the device is idle. Many subsystems use RAII members, but some
still expose `init`, `cleanup`, `destroy`, or `shutdown` methods around raw Vulkan/VMA resources.

The current lifetime model is transitional:

- normal renderer shutdown explicitly orders GPU teardown correctly;
- partial initialization and early-return paths are not uniformly RAII-safe;
- several relationships are non-owning raw pointers whose validity depends on construction order;
- registry registration order also acts as destruction dependency order.

New resource-owning types should be self-cleaning and movable where practical. Do not introduce
new init/destroy pairs.

## Per-Frame Renderer Flow

`Renderer::render()` delegates acquire/submit/present handling to `FrameExecutor`. Once an image
and frame slot are available, `Renderer::buildFrame()` performs:

1. `FrameUpdate::run()`;
2. reset of per-frame threaded command pools;
3. primary command-buffer begin;
4. construction of `FrameData`, `RenderResources`, and execution contexts;
5. `PassScheduler::execute()`;
6. profiler completion and command-buffer end;
7. advancement of ping-pong/buffer-set state.

### FrameUpdate

`FrameUpdate` is the CPU-side update phase:

- processes pending transfers;
- advances renderer time;
- updates global UBOs and light buffers;
- updates skinned bone matrices;
- builds immutable `FrameData`;
- updates atmosphere, environment, and vegetation systems;
- populates `GPUSceneBuffer` from ECS and scatter content.

`FrameData` contains frame-constant scene state such as camera matrices, lighting, player state,
terrain dimensions, wind, weather, snow, and frustum planes.

### Execution Contexts

`FrameContext` is the current pass-scheduler context and contains:

- command buffer;
- frame and swapchain image indices;
- immutable `FrameData`;
- optional render-resource snapshot;
- optional threaded command-pool and secondary-buffer state;
- pass-specific user data.

`RenderContext` remains as a legacy adapter and is currently attached through
`FrameContext::userData`. New pass APIs should use `FrameContext` directly so the legacy context
can be removed.

## Render-Pass Graph

`PassSchedulerBuilder` creates passes in domain modules and wires their dependencies:

```text
Compute
  +--> Shadow --> [ShadowResolve] --+
  +--> Froxel ----------------------+
  +--> WaterGBuffer ----------------+--> HDR
  +--> [GPUCull] -------------------+      |
                                            +--> SSR -----------+
                                            +--> WaterTileCull -+
                                            +--> HiZ --> Bloom --+
                                            +--> BilateralGrid --+--> PostProcess
                                            +--> GodRays --------+
```

Square brackets denote conditionally registered passes.

The graph orders command recording and exposes dependency levels. Most passes currently record
sequentially into the primary command buffer. HDR uses three secondary-command-buffer slots:

| Slot | Drawables |
| --- | --- |
| 0 | Sky, terrain, Catmull-Clark geometry |
| 1 | Scene objects, trees, skinned characters, NPCs, ragdolls |
| 2 | Grass, water, leaves, weather, debug lines |

The scheduler records these HDR slots on worker threads and executes the completed secondary
buffers from the primary HDR render pass.

Pass callbacks capture non-owning pointers to renderer state. `Renderer::cleanup()` clears the
pass graph before destroying captured objects.

## Renderer Systems

`RendererSystems` owns a type-indexed `SystemRegistry`. It exposes typed accessors for existing
callers and lightweight non-owning groups:

- `AtmosphereSystemGroup`;
- `VegetationSystemGroup`;
- `WaterSystemGroup`;
- `SnowSystemGroup`;
- `GeometrySystemGroup`.

`SystemWiring` performs cross-system descriptor and resource connections after construction.
Examples include:

- global/shadow/snow/cloud bindings for terrain and grass;
- wind and terrain bindings for leaves;
- froxel volume bindings for weather;
- cloud-shadow bindings for materials and skinned meshes;
- water caustics bindings for terrain.

GUI code should depend on interfaces from `src/core/interfaces/`, not concrete rendering systems.
Control adapters in `src/controls/` combine concrete systems behind those interfaces.

The registry is currently a service locator as well as an owner. New feature groups should prefer
explicit constructor/factory dependencies and keep registry lookup at the composition boundary.

## Scene and ECS

`Application` owns `ecs::World` and supplies non-owning pointers to renderer and scene systems.

The ECS currently stores:

- transforms and hierarchy;
- mesh/material references;
- bounds and visibility;
- physics body references and shape metadata;
- rendering properties and tags;
- lights;
- player, NPC, weapon, cape, tree, and debug roles.

GPU scene population reads scene entities from ECS and extracts `ecs::RenderData`. Skinned
characters use a separate rendering path. Scatter systems still provide `RenderData` directly.

The ECS migration is incomplete. `SceneBuilder` retains a `std::vector<ecs::RenderData>` mirror
and an entity-to-vector-index map for legacy consumers. Some updates write both ECS components
and this mirror. Until that mirror is removed:

- ECS should be treated as authoritative where a migrated component exists;
- code that updates legacy data must update the corresponding ECS component;
- new systems should query ECS rather than add new index-aligned arrays.

See [ECS_MIGRATION_PLAN.md](ECS_MIGRATION_PLAN.md) for the intended incremental migration.

## Vulkan Infrastructure

`VulkanContext` owns or coordinates:

- Vulkan instance and debug messenger;
- SDL surface;
- physical and logical device;
- graphics, present, and transfer queues;
- VMA allocator;
- pipeline cache;
- swapchain and image views;
- swapchain render pass, depth target, and framebuffers;
- primary command pool and command buffers.

The code uses vulkan-hpp and a mixture of `vk::raii` objects and VMA wrappers:

- `VmaBuffer` / `VmaImage`;
- `DescriptorManager`;
- `GraphicsPipelineFactory` and pipeline builders;
- `ImageBuilder`;
- command and render-pass scopes;
- per-frame and dynamically indexed buffer wrappers.

Public interfaces that cross module boundaries may use C Vulkan handle types to keep migrations
localized. Implementation code uses vulkan-hpp builder-style create infos.

## Threading

The main threading facilities are:

- `TaskScheduler`: shared worker pool;
- `ThreadedCommandPool`: per-thread, per-frame command pools for secondary recording;
- `AsyncTransferManager`: transfer submission and completion tracking;
- `AsyncTextureUploader`: staged texture upload support;
- subsystem-specific workers such as virtual-texture and tree loaders.

GPU object creation and descriptor mutation must respect Vulkan external-synchronization rules.
CPU workers should produce immutable staged data; main-thread/GPU work should consume that data
at explicit synchronization points.

## Resize and Temporal State

`ResizeCoordinator` performs resize work in priority order:

1. core swapchain/depth/framebuffer recreation;
2. render targets;
3. culling targets;
4. G-buffers;
5. viewport/extent-only updates;
6. descriptor rebinding callbacks.

Systems with temporal history implement `ITemporalSystem` and are registered with
`RendererSystems`. Focus restoration resets registered history before the next frame.

## Shaders and Generated Code

Shaders live in `shaders/`. Shared GLSL functions live in `*_common.glsl` files.

The CMake build:

1. compiles shaders to SPIR-V;
2. runs `tools/shader_reflect`;
3. generates `generated/UBOs.h` from reflected uniform-buffer layouts;
4. compiles C++ against those generated structs.

Do not hand-write C++ mirrors of reflected UBOs. Terrain height calculations must use
`shaders/terrain_height_common.glsl` or `src/terrain/TerrainHeight.h`.

## Procedural Content Pipeline

The build-time terrain pipeline is intended to be:

```text
heightmap
  +--> terrain tile preprocessing
  +--> watershed
         -> biome classification
              -> road generation
                   -> virtual-texture tile composition
```

Canonical interchange formats are:

| Data | Format |
| --- | --- |
| Rivers and lakes | GeoJSON |
| Roads | GeoJSON |
| Flow accumulation | OpenEXR |
| Flow direction | 8-bit PNG |
| Watershed labels | RGBA PNG encoding `uint32_t` labels |
| Generated textures | PNG |

The generator implementations and runtime loaders use these formats. The current CMake output
declarations and tile-generator road parser have not fully migrated and are tracked as an open
architecture issue.

## Build and Validation

Configure and compile:

```bash
cmake --preset debug
cmake --build build/debug
```

Run:

```bash
./run-debug.sh
```

Run automated tests:

```bash
ctest --test-dir build/debug --output-on-failure
```

Audit Vulkan usage:

```bash
./scripts/analyze-vulkan-usage.sh
```

## Module Map

| Directory | Purpose |
| --- | --- |
| `src/core/` | renderer orchestration, frame state, resources, pipelines, Vulkan infrastructure |
| `src/passes/` | frame-graph pass definitions and recorders |
| `src/scene/` | application composition, scene assets, camera, input |
| `src/ecs/` | ECS wrapper, components, entity factories, systems |
| `src/terrain/` | terrain rendering, streaming, virtual texturing, terrain data loaders |
| `src/vegetation/` | grass, trees, leaves, scatter content |
| `src/atmosphere/` | time, sky, weather, wind, snow, atmospheric LUTs |
| `src/water/` | water surface, displacement, flow, foam, SSR inputs |
| `src/postprocess/` | HDR composition, bloom, Hi-Z, tone mapping, screen-space effects |
| `src/lighting/` | shadows, froxels, ECS light extraction |
| `src/physics/` | Jolt runtime, character, terrain physics, ragdolls, cloth |
| `src/animation/`, `src/ik/`, `src/npc/`, `src/ml/` | character animation and behavior |
| `src/gui/`, `src/controls/` | UI and renderer-facing control interfaces |
| `tools/` | shader reflection and procedural preprocessing tools |
| `tests/` | unit and integration tests |
