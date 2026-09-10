# Architecture Review

Date: 2026-06-30 (last revised 2026-09-03)

Status: active

This document tracks verified architectural issues in the current implementation. It is not a
feature roadmap. Items should be closed only when their failure mode is removed and their
verification criteria pass.

## Priority Summary

| Priority | Issue | Status |
| --- | --- | --- |
| Critical | Async initialization cannot represent failure to its polling caller | Resolved |
| Critical | Init tasks register into the system registry from concurrent workers | Resolved |
| Critical | Renderer teardown races the async loader | Resolved |
| Critical | Parallel pass levels record into one primary command buffer | Resolved |
| High | Procedural build graph and file formats are inconsistent | Resolved |
| High | Optional subsystem and renderer-readiness contracts are not enforced | Resolved |
| High | Partial initialization and shutdown are not uniformly RAII-safe | Resolved |
| High | `src/core` is the coupling hub, not a foundation | Open |
| High | No frame-global descriptor set; shared resources are hand-wired | Open |
| High | Six GPU culling implementations and three frustum extractors | Open |
| High | No gameplay layer; `Application::run` interleaves everything | Open |
| High | GUI binds concrete systems and owns gameplay state; settings do not persist | Open |
| Medium | Renderer subsystem registry hides dependencies | Open |
| Medium | ECS and legacy render-data mirrors have dual authority | Resolved |
| Medium | Async subsystem loader performs all heavy work on the main thread | Resolved |
| Medium | Build targets do not enforce module boundaries | Partly addressed |
| Medium | Architecture tests do not cover composition and failure paths | Partly addressed |
| Medium | Pass graph orders callbacks but does not know resources | Open |
| Medium | Four streaming systems and hand-rolled uploads share no code | Open |
| Medium | Vulkan conventions are half-applied | Open |
| Medium | ECS components carry behaviour and raw pointers | Open |
| Medium | Build graph and shader pipeline have silent staleness | Open |
| Medium | Runtime validation-layer errors | Open |
| Low | Sync init fallback skips pass-scheduler setup | Open |

Findings dated 2026-09-02 come from a full architectural review of commit f95f6c58; each was
re-verified against the source before being recorded here. Line numbers in those sections refer
to that commit unless stated otherwise.

## Critical: Async Initialization Failure State — RESOLVED (2026-09-02)

Closed by commit f9a14ee7. `Renderer::pollAsyncInit()` returns `AsyncInitStatus`
(`Pending` / `Ready` / `Failed`); `Application::init()` loops only while the status is `Pending`
and exits the loading loop with a startup error on `Failed`. `AsyncSystemLoader` gained an
idempotent `cancel()`; `Renderer::cleanup()` and the `Application` abort path cancel and join the
loader before touching the device. A doctest suite covers loader success, failure, dependency
ordering, and cancellation.

The original evidence and direction are retained below for history.

### (Historical) Evidence

- `Application::init()` waits with `while (!renderer_->pollAsyncInit())`.
- `RendererBuilder::pollAsyncInit()` returns `false` while work is pending.
- The same method sets the failed state and returns `false` when a task fails.
- Once failed, later polls continue returning `false`.

### Impact

Any asynchronous subsystem initialization failure leaves the application in its loading loop
instead of returning a startup error.

### Direction

Replace the boolean polling result with an explicit state:

```text
Pending
Succeeded
Failed(error)
```

The application loading loop must continue only for `Pending`, proceed only for `Succeeded`, and
cleanly unwind for `Failed`.

### Verification

- A deliberately failing initialization task exits the loading loop.
- The process reports the failing task and returns a failure status.
- Renderer and loading-screen resources are released.
- Successful asynchronous startup remains unchanged.

## High: Procedural Build Contract Drift — RESOLVED (2026-09-02)

Verified against the tree: `CMakeLists.txt` declares no `.bin`, `.dat`, or `roads.json` terrain
outputs (the only remaining `.bin` is the UniCon policy-weights input). The tile generator is
passed `${ROADS_GEOJSON}`, `tools/tile_generator/TileCompositor.cpp::loadRoads` parses a GeoJSON
`FeatureCollection` of `LineString` features, and `tools/tile_generator/main.cpp` refuses to
generate tiles when an explicitly supplied roads file fails to load.

The original evidence and direction are retained below for history.

### (Historical) Evidence

The generator and runtime formats are:

- `flow_accumulation.exr`;
- `flow_direction.png`;
- `watershed_labels.png`;
- `rivers.geojson`;
- `lakes.geojson`;
- `roads.geojson`.

The top-level CMake graph still declares legacy `.bin`, `.dat`, and `roads.json` outputs and
dependencies. The virtual-texture tile compositor also parses the legacy road JSON shape rather
than a GeoJSON `FeatureCollection`.

An immediate second debug build reruns watershed and road commands even though both tools report
that their outputs are current. The missing declared outputs prevent the graph from becoming
stable. The tile generator is passed a missing `roads.json` path and does not fail the build when
road loading fails.

### Impact

- Incremental builds perform unnecessary preprocessing checks.
- Clean and incremental builds have different hidden inputs.
- Virtual textures can be generated without roads while the build still succeeds.
- CMake no longer accurately describes the artifacts consumed by later stages.

### Direction

- Declare only the standard output formats actually written by each tool.
- Make downstream dependencies refer to those files.
- Pass `roads.geojson` to the tile generator.
- Parse GeoJSON in the tile compositor, preferably through shared road-loading code.
- Treat failure to load an explicitly supplied input as a tool failure.
- Use a stamp only for multi-file outputs, and write it after validating every required output.

### Verification

- A clean full build generates all declared files.
- A second build reports no watershed, biome, road, or tile generation work.
- Removing one generated artifact rebuilds only that artifact and its dependents.
- Generated virtual-texture previews visibly contain roads.
- A missing or malformed roads file makes tile generation fail.

## High: Renderer Readiness and Optional Systems — RESOLVED (2026-09-03)

Closed by commit f798986e. Hi-Z is a required system: the init task fails with a logged error when
it cannot be created, and the unconditional dereferences are therefore valid. `setupPassScheduler`
returns `bool`; a `PassSchedulerBuilder::build()` failure fails `Renderer::create()` on the
synchronous path and reports `Failed` through the async loader. The unimplemented public
`RendererSystems::init()` is deleted. Verified by a debug build, ctest, a smoke run, and the
`tests/test_async_system_loader.cpp` failure case.

Remaining gap (tracked as "Sync init fallback skips pass-scheduler setup" below): the fallback
taken when `AsyncSystemLoader::create` fails runs subsystem init synchronously but never runs the
finalize sequence.

### (Historical) Evidence

- Hi-Z creation is described and implemented as optional.
- GPU-culling setup, debug controls, resize registration, and post passes dereference Hi-Z
  unconditionally in several paths.
- `PassSchedulerBuilder::build()` failure is logged, but `RendererBuilder` still reports success.
- `RendererSystems::init()` is public but deliberately unimplemented.

### Impact

The renderer can be returned in a partially initialized state, and an optional-system failure can
become a later assertion, exception, or invalid access.

### Direction

- Define a single renderer readiness boundary.
- Return a structured initialization error from every required construction phase.
- Mark systems as either required or optional once, at their ownership boundary.
- Pass optional systems as nullable/optional dependencies to consumers.
- Do not expose public initialization entry points that cannot succeed.

### Verification

- Each optional subsystem can be disabled or made to fail without crashing startup.
- Required pass-graph failure makes `Renderer::create()` fail.
- Rendering cannot begin unless the pass scheduler is compiled.
- Tests cover the required/optional system matrix.

## High: Ownership and Failure-Safe Teardown — RESOLVED (2026-09-03)

Closed by commits f9a14ee7, f798986e and 96d76016.

- `Application::shutdown()` is idempotent and safe after partial init; `~Application()` calls it,
  and every failure return in `Application::init()` releases what was created (loading renderer,
  renderer, task scheduler, window, SDL).
- `Renderer::cleanup()` first cancels and joins the async loader, then waits for the device under
  `GraphicsQueueLock`, then tears down in the documented order.
- `VulkanContext` no longer has `shutdown()`; its destructor performs the complete, idempotent
  teardown through RAII members.
- Subsystems are destructor-driven: raw handle members were replaced by `vk::raii` optionals,
  `VmaBuffer`, `ManagedBuffer` and per-frame wrappers declared in dependency order, and public
  init/cleanup pairs became `create()` factories plus destructors. Header counts fell from 59 to
  23 lifecycle methods and from 386 to 295 raw handle members; `scripts/analyze-raii.sh --check`
  runs in CI against `scripts/raii-baseline.txt` and fails if either count grows.
- A run under the Khronos validation layer reported zero errors after teardown began.

Not done: `Renderer::create(const InitInfo&)` still moves the `VulkanContext` out of a const
object by `const_cast`; the registry still uses registration order as destruction order.

### (Historical) Evidence

- `Renderer::create(const InitInfo&)` moves a `unique_ptr<VulkanContext>` out of a const object by
  `const_cast`.
- `VulkanContext` has a default destructor but requires explicit `shutdown()`.
- `Application` has a default destructor but requires explicit `shutdown()`.
- Several `Application::init()` failure returns occur after SDL, window, scheduler, Vulkan, or
  renderer creation without a common unwind path.
- subsystem registration order is also used as destruction dependency order.

### Impact

Ownership transfer is surprising and potentially undefined for genuinely const input. Partial
startup failures can leak raw resources or leave global facilities initialized until process exit.

### Direction

- Accept move-only initialization state by value or rvalue reference.
- Make `VulkanContext` destruction perform complete, idempotent teardown.
- Wrap SDL initialization and `SDL_Window` in scoped owners.
- Make `Application` destruction safe whether initialization completed or not.
- Prefer member ownership order or explicit aggregate owners over registry-order teardown.

### Verification

- Failure can be injected after each initialization phase without leaks or validation errors.
- `Application` can be destroyed without calling `shutdown()` explicitly.
- `VulkanContext` can be destroyed safely after either initialization phase.
- Sanitizers and Vulkan validation report clean teardown.

## Medium: RendererSystems Service Locator

### Evidence

`RendererSystems` combines:

- type-indexed ownership;
- typed getters and setters;
- feature-group views;
- control-adapter construction;
- ECS pointer distribution;
- temporal-system registration;
- scene-material bookkeeping.

Construction dependencies are separately expressed in task names, factory calls, `SystemWiring`,
and registration order.

### Impact

Dependencies are easy to access but difficult to reason about. Invalid combinations remain
representable, and adding a system often changes the composition root, registry API, wiring, resize
registration, temporal registration, and pass graph.

### Direction

- Make water, vegetation, atmosphere, snow, and geometry aggregates actual owners.
- Give aggregate factories explicit dependency structs.
- Expose narrow interfaces or resource views to passes.
- Reserve registry lookup for optional extensions and composition-root diagnostics.

### Verification

- A feature aggregate can be constructed and tested without the complete renderer.
- Internal feature dependencies no longer require `RendererSystems`.
- Removing a feature does not require unrelated modules to compile against its concrete types.

## Medium: ECS and Render-Data Dual Authority — RESOLVED (2026-07-01)

Closed by the dual-authority gap-closing work. ECS components are now the sole authority for
scene objects; `ecs::RenderData` remains only as a transient GPU-feed struct produced by
`extractRenderData`. What changed, by slice:

1. **Opacity** — the `renderable->opacity` mirror write in `Application::updateCameraOcclusion`
   is gone; `ecs::Opacity` is authoritative and read via `extractRenderData`.
2. **Transform** — the physics scale-recovery (`SceneManager::updatePhysicsToScene`), player /
   weapon / cape transforms (`SceneBuilder`), and the skinned + GUI readers now read/write only
   `ecs::Transform`. No dual transform writes remain.
3. **Physics bodies** — the index-aligned `scenePhysicsBodies` vector and the pointer-identity
   re-link loop are removed. Bodies live as `ecs::PhysicsBody` components, created by an
   idempotent `SceneManager::initializeScenePhysics` (via `ensureScenePhysics()` and the
   deferred callback).
4. **Scatter** — formalized as an immutable instance source (read-only public accessor +
   documented one-time bake), explicitly separate from the scene-object authority.
5. **Enumeration parity** — the CPU-fallback (`SceneObjectsDrawable`) now iterates
   `getSceneEntities()` with the same invalid+`GPUSkinned` filter as the GPU-indirect feed
   (`FrameUpdater`), so both paths render the same entity set.
6. **Mirror removal** — `SceneBuilder::sceneObjects`, `entityToRenderableIndex_`,
   `getRenderableForEntity`, `getRenderables`, and `objectRoles_` are deleted. Entities are
   seeded directly from a transient `RenderData` during `createRenderables`.

Verification: debug build is clean; `./run-debug.sh` reaches the render loop, creates deferred
scene entities, and creates 8 scene physics bodies from ECS components with no "No ECS world
during physics init" warning and no new validation errors. Remaining verification is interactive
(visual): physics objects fall with correct scale, player/weapons/cape/IK track, occlusion fade,
GUI inspector shows live values, and CPU-fallback parity with the GPU path.

The original evidence and direction are retained below for history.

### (Historical) Verified against the code on 2026-07-01

Earlier NPC, special-object-index, and monolithic `Renderable` concerns are resolved (see notes
below); the live problem was confined to the persistent `RenderData` mirror, the dual
physics/opacity writes, the scatter feed, and the index-aligned physics vector.

### Evidence

- `Application` owns `ecs::World` by value (`src/scene/Application.h:81`).
- `SceneBuilder` retains a persistent `std::vector<ecs::RenderData> sceneObjects`
  (`src/scene/SceneBuilder.h:294`) and an entity-to-index mapping
  `entityToRenderableIndex_` (`src/scene/SceneBuilder.h:310`).
- Physics writes both authorities: `renderable->transform` (`src/scene/SceneManager.cpp:228`)
  and `ecs::Transform.matrix` (`src/scene/SceneManager.cpp:235`). Opacity writes both
  `ecs::Opacity` (`src/scene/Application.cpp:1365`) and `renderable->opacity`
  (`src/scene/Application.cpp:1370`). Both are commented as migration-time dual writes.
- GPU scene population extracts scene objects from ECS via `extractRenderData`
  (`src/core/FrameUpdater.cpp:54-57`), but scatter content still supplies `RenderData` directly
  from its own vector (`src/core/FrameUpdater.cpp:69-70`, built at `src/terrain/ScatterSystem.cpp:196`).
- `SceneManager` retains an index-aligned physics-body vector `scenePhysicsBodies`
  (`src/scene/SceneManager.h:98`), sized to the scene-object count
  (`src/scene/SceneManager.cpp:111`) and indexed positionally, even though `ecs::PhysicsBody`
  components are now also populated (`src/scene/SceneManager.cpp:146`).

`ecs::RenderData` (`src/ecs/Components.h:903`) is legitimately retained as the transient GPU-feed
struct produced by `extractRenderData`; its PBR fields are populated from a separate
`PBRProperties` component. The goal is to stop *persisting and mirror-writing* it, not to delete
the type.

### Impact

Every migrated property requires synchronization rules. Missed writes create frame-dependent
differences between physics, editor inspection, CPU rendering paths, and GPU-driven rendering.

### Direction

Continue the migration in working slices:

1. make ECS transform and physics-body components authoritative;
2. remove the index-aligned physics storage (`scenePhysicsBodies`);
3. migrate editor/inspector reads to ECS;
4. convert scatter instances to ECS or a clearly separate immutable instance source;
5. remove the persistent render-data mirror (`sceneObjects` + `entityToRenderableIndex_`);
6. build transient GPU records from component queries.

### Verification

- No per-frame code writes both a component and a render-data mirror.
- Entity deletion/reordering cannot invalidate a parallel vector mapping.
- CPU fallback and GPU indirect paths render the same entity set.

### Resolved (verified 2026-07-01)

- NPCs are ECS-entity based (`npcEntities_`, `src/npc/NPCSimulation.h:173`); the parallel
  `templateIndices`/`positions`/`renderableIndices` arrays no longer exist. `NPCRenderData` is a
  transient per-frame draw struct.
- Hardcoded special-object indices (`playerIndex`/`emissiveOrbIndex`/`capeIndex`) are replaced by
  entity handles (`src/scene/SceneBuilder.h:311-318`); tag components (`PlayerTag`, `CapeTag`,
  `WeaponTag`, …) exist in `src/ecs/Components.h`.
- The monolithic `Renderable` struct and `RenderableBuilder.h` are gone. PBR data lives in
  `PBRProperties`; tree typing lives in dedicated components (`BarkType`/`LeafType`,
  `src/ecs/Components.h:300-313`).

## Medium: Async Loader Without Background Staging — RESOLVED (2026-09-02)

Closed by commit f9a14ee7. Every renderer initialization task now performs its heavy work
(pipeline compilation, buffer/image creation, uploads, file IO, mesh/texture generation) in
`cpuWork` on `AsyncSystemLoader` worker threads, staging the built objects in per-task storage.
`gpuWork` runs on the main thread inside `pollCompletions()` and only adopts and registers the
staged systems. `SystemRegistry` is guarded by a `std::shared_mutex` for the reads that can still
overlap with registration.

The original evidence and direction are retained below for history.

### (Historical) Evidence

All renderer initialization tasks define `gpuWork`; none define `cpuWork`. Worker threads therefore
only move ready task IDs to the main-thread completion queue. Heavy factories execute from
`pollCompletions()` on the main thread.

### Impact

The design pays for dependency scheduling, worker management, synchronization, and two startup
paths without moving the expensive work off the main thread. Individual task execution can still
stall the loading screen.

### Direction

For each expensive loader:

1. parse files and generate CPU data in `cpuWork`;
2. store typed staged results owned by the task;
3. create Vulkan resources and register the completed system in `gpuWork`;
4. bound staged memory and support cancellation.

If a subsystem cannot be split safely, keep it synchronous and do not schedule a no-op worker task.

### Verification

- Profiling shows substantive initialization work on worker threads.
- GPU object creation remains on an externally synchronized path.
- The loading screen continues updating during long CPU stages.
- Cancellation joins workers and frees staged data.

## Medium: Build-Time Module Boundaries — PARTLY ADDRESSED (2026-09-03)

Commit f798986e introduced the `engine_compile_options` INTERFACE target that carries compile
definitions and include directories for the application and all three test executables, so the
Jolt and vulkan-hpp definition mismatch is gone; duplicate test sources and nine orphan sources
were removed. Still open: the runtime is one executable target, tests still re-list production
sources, and no library target enforces include direction (see "`src/core` is the coupling hub").

### Evidence

- Runtime sources are compiled into one executable target.
- Most source directories are added to a shared include search path.
- Tests manually list and recompile selected production `.cpp` files.
- Some test source lists contain duplicate production entries.

### Impact

Directory boundaries are conventional rather than enforced. Tests can compile production code with
different definitions and dependencies from the application.

### Direction

Introduce focused CMake library targets incrementally:

- foundational math/data utilities;
- animation and IK;
- ECS and scene data;
- preprocessing data formats;
- renderer core and Vulkan infrastructure;
- feature aggregates.

Tests should link these targets rather than copy source lists.

### Verification

- Production sources are declared once.
- Tests and application use the same target compile definitions.
- Target link dependencies document allowed module direction.
- Unrelated feature headers are not globally visible.

## Medium: Missing Composition and Failure-Path Tests — PARTLY ADDRESSED (2026-09-03)

`tests/test_async_system_loader.cpp` (commit f9a14ee7) covers loader success, task failure with
dependents never running, dependency ordering against finalize, and cancellation. `SystemRegistry`,
`PassScheduler`, the required/optional system matrix and partial teardown remain untested.

### Evidence

Existing tests cover algorithms, loaders, animation, terrain helpers, virtual-texture loading, and
town generation. There are no direct tests for:

- `AsyncSystemLoader` state transitions;
- `SystemRegistry` replacement and destruction behavior;
- `PassScheduler` dependency and failure behavior;
- renderer required/optional system composition;
- partial `Application` or `VulkanContext` initialization teardown.

### Direction

Add tests at architectural seams before changing their implementations. Vulkan-independent state
machines and dependency graphs should be tested without a GPU. GPU lifetime tests can use a small
validation-enabled smoke-test executable.

### Verification

- Async success, task failure, cancellation, and dependency-cycle cases are covered.
- Pass-graph cycle and build failures are observable by callers.
- Partial initialization teardown is exercised under sanitizers.

## Critical: Registry Writes From Concurrent Init Tasks — RESOLVED (2026-09-02)

Closed by commit f9a14ee7. After the loader began running task bodies on workers, sibling tasks
(`terrain` and `snow_weather`, later `vegetation` and `atmosphere`) each registered systems into
`SystemRegistry` from worker threads with no synchronization. Registration now happens in each
task's main-thread `gpuWork`, and `SystemRegistry` is guarded by a `std::shared_mutex` for the
reads that can still overlap. Verified by build, ctest, smoke run and adversarial review.

## Critical: Renderer Teardown Racing the Async Loader — RESOLVED (2026-09-02)

Closed by commit f9a14ee7. `Renderer::cleanup()` destroyed systems and the `VulkanContext` while
loader workers could still be building them; reachable from quit-during-loading and from any init
failure after the loader started. `AsyncSystemLoader::cancel()` is idempotent, and both
`Renderer::cleanup()` and the `Application` abort path call it before waiting on the device.

## Critical: Parallel Pass Levels Sharing One Primary — RESOLVED (2026-09-02)

Closed by commit f9a14ee7. `PassScheduler` could submit every pass of a dependency level to the
task pool with the same `FrameContext`, whose command buffer is the single frame primary. The
level-parallel branch and the `mainThreadOnly` flag are removed; passes record sequentially on the
calling thread and only the HDR secondary-buffer slots record in parallel.

## High: `src/core` Is the Coupling Hub

### Evidence (2026-09-02)

Header-include analysis puts `src/core` in a two-way cycle with 19 of the other 24 source
directories. The traffic is concentrated in `RendererBuilder.cpp`, `RendererSystems.cpp`,
`Renderer.cpp`, `SystemWiring.cpp` and `core/updaters/*`; `RendererSystems.h` forward-declares
roughly sixty feature classes. Core also includes NPC simulation, settlement registries and
`ECSMaterialDemo` (via `SelectionOutlineRenderer.h`).

| Cycle | core includes it | it includes core |
| --- | --- | --- |
| vegetation | 47 | 92 |
| atmosphere | 62 | 58 |
| postprocess | 29 | 69 |
| water | 25 | 67 |
| terrain | 14 | 58 |
| lighting | 18 | 20 |
| scene | 18 | 18 |
| passes | 10 | 42 |

### Impact

There is no direction in which dependencies flow, so nothing can be built, tested or replaced in
isolation, and every feature change risks a rebuild of everything.

### Direction

Split core into a foundation (Vulkan wrappers, buffers, descriptors, pipeline factory, threading,
frame data) that includes nothing above it, and a composition layer (builder, systems, wiring,
updaters, passes) that sits on top of the features. Make the foundation a CMake library so the
direction is enforced by the compiler. Delete `RenderContext` (every pass casts it out of
`FrameContext::userData`), `ECSMaterialDemo`, and drop `src/training` and unused `ml/calm` from
the game target first.

### Verification

- The foundation library links with no feature directory on its include path.
- The include matrix shows no edge from the foundation to a feature directory.
- `RenderContext.h` no longer exists.

## High: No Frame-Global Descriptor Set

### Evidence (2026-09-02)

Each system binds its own copy of the global UBO, shadow map, snow, cloud shadow and wind into its
own descriptor set. `SystemWiring.cpp:101-198` carries about seventeen "bind X into Y" edges;
`GrassSystem::updateDescriptorSets` takes 18 parameters (`GrassSystem.h:88-104`); adding the
cloud-shadow texture touched six systems. Thirty-six systems declare their own
`createDescriptorSets`. `GlobalBufferManager` only hands out raw buffer vectors.

### Impact

Every shared resource is threaded by hand after construction, and the wiring is the main reason
adding a system touches the composition root, the registry, wiring, resize and the pass graph.

### Direction

One per-frame globals set at binding zero, owned by `GlobalBufferManager`, holding UBO, lights,
shadow, cloud, snow, wind, terrain height and Hi-Z. Systems then own only their private sets.
Convert grass first, then terrain, then the rest; each conversion deletes wiring code.

### Verification

- `SystemWiring` contains no shadow/snow/cloud/wind edges.
- No `updateDescriptorSets` signature takes more than a handful of parameters.
- Adding a global texture changes one place.

## High: Six GPU Culling Implementations

### Evidence (2026-09-02)

Scene cull (`GPUCullPass`), shadow cull (`ShadowCullPass`, a near copy), impostor cull, leaf cull,
branch cull and water tile cull each own a compute pipeline, a uniform struct with the same six
frustum planes plus a Hi-Z flag, and a hand-threaded Hi-Z view. `extractFrustumPlanes` exists in
`CullCommon.h:27`, `GPUCullPass.h:123` and `HiZSystem.h:184`. About 1,700 lines of cull shaders.
The per-frame `waitIdle` calls that some of these carried were removed in commit f798986e.

### Direction

One cull pass parameterised by input layout, one `CullingUniforms` block in the globals set, one
frustum extractor. Do this when the next feature touches culling rather than as a standalone
project.

### Verification

- One compute cull entry point in C++ and one uniform layout in GLSL.
- `extractFrustumPlanes` is defined once.

## High: No Gameplay Layer

### Evidence (2026-09-02)

`Application` is about 2,000 lines; `Application::run()` (`Application.cpp:1083-1416` at f95f6c58)
interleaves input, physics, ragdolls, the UniCon controller, cloth, motion-matching yaw feedback,
settlement and bridge streaming, camera occlusion, terrain diagnostics and GUI sync.
`AnimatedCharacter::update` takes an allocator, device, command pool and queue
(`AnimatedCharacter.h:86`), so animation cannot run without the renderer. Physics-to-ECS sync
lives in four places (`SceneManager.cpp:201`, `Application.cpp:338,1248,1856`). Player facing
logic reads `PlayerSettings` owned by the debug GUI every frame (`GuiPlayerTab.h:17-44`).

### Direction

Extract a gameplay update with a plain-data interface: player controller, character system,
world-streaming scheduler, one physics sync pass. Move skinned-mesh GPU upload to a renderer-side
updater. Move `PlayerSettings` out of the GUI into game state the GUI edits.

### Verification

- `Application::run()` calls a small number of update steps and contains no simulation code.
- No animation or character update takes Vulkan handles.
- Disabling the developer GUI does not change player movement.

## High: GUI Layering and Settings

### Evidence (2026-09-02)

Eight of nineteen panels take interfaces; the rest include terrain, tree, water, post-process,
physics and animation headers directly, and `GuiTreeTab::render` takes the whole
`RendererSystems`. `ISceneControl`, `IPlayerControl` and `IDebugControl` return `SceneBuilder&`,
`ecs::World*` and concrete debug renderers. The settings menu mutates live input fields and
performance toggles with no persistence (`GameMenu.cpp:155-189`). GUI drawing is a callback
threaded from `Application` through `Renderer` into `PostProcessSystem::recordPostProcess`.
`ARCHITECTURE.md` now describes this state accurately (commit f798986e).

### Direction

Narrow read-model interfaces for the four heavy panels; stop returning concrete owners from
control interfaces; a saved `UserSettings` struct that the menu edits and systems apply; make the
GUI a scheduler pass after post-process.

### Verification

- `src/gui` includes only `core/interfaces`, `controls` and ImGui.
- Settings survive a restart.

## Medium: Pass Graph Without Resources

### Evidence (2026-09-02)

`PassScheduler` is a topological sort of `std::function` nodes with hand-wired edges
(`PassSchedulerBuilder.cpp:57-100`). Barriers live in about 120 subsystem call sites and none in
`src/passes`, so image layout ownership is implicit per system. Five systems build render passes
by hand outside `RenderPassBuilder`. The Compute node wraps about fifteen dispatches with no
internal ordering expressed. `RenderContext` is still cast out of `userData` in every pass.

### Direction

Keep the scheduler; add per-pass read/write declarations so debug builds can validate the
hand-written barriers. Delete `RenderContext` now (about twelve edits).

## Medium: Streaming and Upload Duplication

### Evidence (2026-09-02)

Terrain tiles, virtual-texture tiles, grass tiles and tree generation each have a private worker
pool, request queue, completion drain and budget model (`TerrainTileDiskLoader.h:20` says it
mirrors the VT loader). None use the shared `TaskScheduler`. Staging uploads are hand-rolled in
seven feature files rather than going through `AsyncTransferManager`. Resize handling is split
between `resize()` and `setExtent()` with duplicate overloads.

### Direction

One generic priority loader pool and one budgeted main-thread drain, adopted by the next streamer
that needs changing.

## Medium: Vulkan Conventions Half-Applied

### Evidence (2026-09-02)

About a thousand `VK_*` constant uses outside null handles, concentrated in terrain descriptor
sets, leaf culling, post-process and the pipeline factory; 62 C handle or struct types in headers,
mostly `VkExtent2D` in resize signatures. Three construction conventions coexist (`InitInfo`,
`InitContext`, `Bundle`). Two `vk::Format` to C casts remain at `MipChainBuilder::setFormat` and
`RenderPassConfig`, and `PerFrameBufferBuilder::setUsage` takes the C flags type.

### Direction

Migrate `MipChainBuilder`, `RenderPassConfig` and `PerFrameBufferBuilder` to `vk::` types, then
purge `VkExtent2D`/`VkFormat` from headers, then the enum constants file by file. Pick
`InitContext` and delete the `InitInfo` duplicates.

## Medium: ECS Components Carry Behaviour and Raw Pointers

### Evidence (2026-09-02)

`Components.h` defines 66 types. Several hold logic (NPC blend and LOD controllers), one holds a
`void*` to an `AnimatedCharacter` owned by a parallel vector in `NPCSimulation`, one holds a
`const glm::mat4*` with a "must remain valid" comment, and per-entity bone matrices are heap
vectors. `ECSMaterialDemo` is created and ticked every frame. There are no ECS tests.

### Direction

Split into per-domain component headers; move behaviour to `Systems.h`; replace `void*` and raw
pointer links with typed handles; delete the demo; add World tests.

## Medium: Build Graph and Shader Pipeline Staleness

### Evidence (2026-09-02)

Five preprocessing tools are not listed as inputs to their own outputs, so a tool fix leaves stale
world data until a clean build. Shader include dependencies are a hand-maintained list rather than
depfiles. UBO reflection covers a curated fifteen-shader subset. Twenty-one compiled `.spv` files
have no source. No ASAN job runs in CI despite an `asan` preset.

### Direction

Add `$<TARGET_FILE:...>` to each tool's `DEPENDS`; generate depfiles for shaders; delete orphan
`.spv`; add an ASAN CI job.

## Medium: Runtime Validation-Layer Errors

### Evidence (2026-09-03)

Running under the Khronos validation layer (`VK_LAYER_PATH` pointing at the Vulkan SDK; exporting
`VK_INSTANCE_LAYERS` alone does not load it on this machine) reports 44 distinct VUIDs at runtime,
none during teardown. Categories: device features used without being enabled (sampler anisotropy
and others), descriptor sets updated while bound without update-after-bind, and image layout
mismatches. These predate the RAII migration; `main` before it crashed under the layer in the
shadow pipeline init, which the migration fixed.

### Direction

Enable the features the code uses at device creation, fix the descriptor-update ordering, then run
the layer in CI as a smoke job.

## Low: Sync Init Fallback Skips Pass-Scheduler Setup

### Evidence (2026-09-03)

When `AsyncSystemLoader::create` fails, `RendererBuilder` runs `initSubsystems` synchronously and
marks async init complete, so the control subsystems, resize coordinator, temporal systems,
recorders and `setupPassScheduler` never run and the renderer is returned partially configured.

### Direction

Either drop the fallback or run the same finalize sequence the async path runs.

## Documentation Maintenance

`ARCHITECTURE.md` describes current behavior. Migration plans describe intended behavior.

When an architecture issue closes:

1. update the implementation;
2. add or update its verification;
3. update `ARCHITECTURE.md`;
4. mark the issue closed here with the validating commit or pull request;
5. remove stale migration text rather than leaving contradictory descriptions.

## Validation Snapshot

Last run 2026-09-03 on commit 63677da9 with the prescribed debug build:

```bash
cmake --preset debug
cmake --build build/debug
ctest --test-dir build/debug --output-on-failure
SCREENSHOT_AFTER_FRAMES=300 ./run-debug.sh
```

The build completed with no errors. All five ctest targets passed. The smoke run reached the
render loop in about twelve seconds, captured a correct frame, and shut down in under a second
from `RendererSystems::destroy starting` to `vulkanContext shutdown complete`.
`scripts/analyze-main-thread-stalls.sh` reports zero violations and `scripts/analyze-raii.sh
--check` passes at the recorded baseline. A run under the Khronos validation layer reported zero
errors during teardown; the runtime errors it reported are tracked above.
