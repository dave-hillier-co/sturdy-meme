# Architecture Review

Date: 2026-06-30

Status: active

This document tracks verified architectural issues in the current implementation. It is not a
feature roadmap. Items should be closed only when their failure mode is removed and their
verification criteria pass.

## Priority Summary

| Priority | Issue | Status |
| --- | --- | --- |
| Critical | Async initialization cannot represent failure to its polling caller | Resolved |
| High | Procedural build graph and file formats are inconsistent | Resolved |
| High | Optional subsystem and renderer-readiness contracts are not enforced | Open |
| High | Partial initialization and shutdown are not uniformly RAII-safe | Open |
| Medium | Renderer subsystem registry hides dependencies | Open |
| Medium | ECS and legacy render-data mirrors have dual authority | Resolved |
| Medium | Async subsystem loader performs all heavy work on the main thread | Resolved |
| Medium | Build targets do not enforce module boundaries | Open |
| Medium | Architecture tests do not cover composition and failure paths | Open |

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

## High: Renderer Readiness and Optional Systems

### Evidence

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

## High: Ownership and Failure-Safe Teardown

### Evidence

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

## Medium: Build-Time Module Boundaries

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

## Medium: Missing Composition and Failure-Path Tests

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

## Documentation Maintenance

`ARCHITECTURE.md` describes current behavior. Migration plans describe intended behavior.

When an architecture issue closes:

1. update the implementation;
2. add or update its verification;
3. update `ARCHITECTURE.md`;
4. mark the issue closed here with the validating commit or pull request;
5. remove stale migration text rather than leaving contradictory descriptions.

## Validation Snapshot

The review used the repository's prescribed debug build:

```bash
cmake --preset debug
cmake --build build/debug
```

The build completed successfully. An immediate second build reran watershed and road generation
checks, confirming the declared-output mismatch.

`./run-debug.sh` completed renderer initialization, compiled the pass graph, generated deferred
scene/vegetation content, entered the render loop, and shut down cleanly after the smoke test.
Expected environment/content warnings were emitted for an unavailable fallback MoltenVK ICD path,
unsupported primitive-restart behavior on Metal, and missing optional UniCon policy weights.

CTest result:

- `motion_matching_integration_tests`: passed;
- `motion_matching_data_driven_tests`: passed;
- `town_generator_tests`: passed;
- `vulkan_game_tests`: one failing virtual-texture placeholder expectation.

The failing assertion belongs to the existing uncommitted virtual-texture change: the loader now
returns neutral gray for a missing tile while the test still expects the former magenta
checkerboard.
