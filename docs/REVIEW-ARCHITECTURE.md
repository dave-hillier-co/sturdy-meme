# Architectural Review — 2026-04-17

Scope: `src/` of the Vulkan outdoor-rendering engine. Focus is architectural
health (not performance, not security). File:line references use the tree at
commit `912c077` on branch `claude/review-game-engine-15t1V`.

## Executive Summary

The engine is well-structured and reflects a lot of deliberate design —
system registries, RAII wrappers in `VulkanRAII.h`, a FrameGraph abstraction,
tiered system groups, shader-reflected UBO generation. It is also visibly
**mid-refactor**. Several migrations are in flight with plans that overstate
completeness:

- Two-phase init: marked `COMPLETE` in `PLAN-eliminate-two-phase-init.md`,
  but 19 headers still expose `void destroy()` / `void shutdown()`.
- FrameGraph: wired as a facade; parallel execution levels advertised in
  `ARCHITECTURE.md` are not realized.
- Vulkan-hpp migration: partial. ~399 raw `vk*` call sites remain.
- ECS: ~3,000 lines of infrastructure, but the render path still iterates
  legacy `Renderable*` from `SceneManager`.

None of this is a crisis. The biggest risk is **drift between the docs and
the code** — readers trust docs marked "complete" and then discover the
pattern they're meant to avoid is still widespread. Fixing that is cheap and
worth doing first.

---

## 1. Orchestration & God-Object Risk

`src/core/Renderer.cpp` (1017 lines) + `src/core/RendererSystems.cpp` (420)
form the rendering hub. `RendererSystems` holds ~60 `unique_ptr` subsystems
via a `SystemRegistry`, grouped into tiered system groups
(`AtmosphereSystemGroup`, `VegetationSystemGroup`, etc.).

**Verdict:** not a true god-object. Registry indirection and system groups
keep direct coupling manageable. `Renderer` itself is a 341-line header that
delegates subsystem ownership out. Pass recorders (`ShadowPassRecorder`,
`HDRPassRecorder`) were already extracted.

**Residual work:** `Renderer::buildFrame()` still assembles lambdas per
pass, and `FrameDataBuilder` still lives in `Renderer`. The in-progress
`RENDERER_REFACTORING_PLAN.md` already names this.

---

## 2. FrameGraph — Facade Rather Than a Scheduler

`src/core/pipeline/FrameGraph.{h,cpp}` exists and `Renderer::setupFrameGraph()`
builds passes via `FrameGraphBuilder`. But:

- `ARCHITECTURE.md` claims levels 1 (Shadow/Froxel/WaterGBuffer) and 3
  (SSR/WaterTileCull/HiZ/BilateralGrid) run in parallel. In practice, passes
  execute in sequence — the FrameGraph orders them, it does not schedule
  them in parallel.
- Secondary command buffer parallelism does exist inside the HDR pass (3
  slots: sky+terrain, scene objects, grass+water), but not between passes.

**Recommendation:** pick one — either drive real dependency-based
scheduling, or downgrade the claim in `ARCHITECTURE.md` and name FrameGraph
as an ordering/builder facade. Today a reader is misled about what's
implemented.

---

## 3. System Lifecycle — RAII Claim vs. Reality

`CLAUDE.md` states: *"do not use init destroy patterns"*, *"prefer to use
RAII and smart pointers"*.

`docs/PLAN-eliminate-two-phase-init.md` states: **Status: COMPLETE ✓**, "All
systems have been converted to RAII factory pattern."

The tree disagrees. 19 headers still declare `destroy()` or `shutdown()`:

| File | Line | Method |
|------|------|--------|
| `src/core/FrameExecutor.h` | 51 | `void destroy()` |
| `src/core/FrameIndexedBuffers.h` | 73 | `void destroy()` |
| `src/core/RenderingInfrastructure.h` | 56 | `void shutdown()` |
| `src/core/TripleBuffering.h` | 82 | `void destroy()` |
| `src/core/pipeline/PipelineCache.h` | 40 | `void shutdown()` |
| `src/core/threading/TaskScheduler.h` | 76 | `void shutdown()` |
| `src/core/vulkan/AsyncTransferManager.h` | 60 | `void shutdown()` |
| `src/core/vulkan/ThreadedCommandPool.h` | 53 | `void shutdown()` |
| `src/core/vulkan/VulkanContext.h` | 48 | `void shutdown()` |
| `src/loading/AsyncStartupLoader.h` | 121 | `void shutdown()` |
| `src/loading/AsyncSystemLoader.h` | 157 | `void shutdown()` |
| `src/loading/LoadJobFactory.h` | 196 | `void shutdown()` |
| `src/loading/LoadJobQueue.h` | 230 | `void shutdown()` |
| `src/material/ComposedMaterialRegistry.h` | 188 | `void destroy()` |
| `src/material/DescriptorManager.h` | 181 | `void destroy()` |
| `src/ml/GPUInference.h` | 87 | `void destroy()` (paired with `init()` at 64) |
| `src/scene/Application.h` | 33 | `void shutdown()` |
| `src/vegetation/GrassTileManager.h` | 77 | `void destroy()` |
| `src/vegetation/GrassTileResourcePool.h` | 50 | `void destroy()` |

Some of these are legitimate (e.g. `TaskScheduler::shutdown()` is a
join-threads operation, not a destructor stand-in), but several — notably
`GPUInference`, `DescriptorManager`, `ComposedMaterialRegistry`,
`GrassTileManager`, `GrassTileResourcePool`, `TripleBuffering`,
`FrameIndexedBuffers`, `FrameExecutor` — look like two-phase patterns the
plan claimed to eliminate.

**Recommendation:** re-open `PLAN-eliminate-two-phase-init.md` with a
residual-work section distinguishing:

- Thread-join / drain APIs that happen to be named `shutdown()` (keep as-is,
  document the intent).
- Genuine two-phase holdovers (convert to RAII destructors).

Fix C in this review updates the plan file accordingly.

---

## 4. Vulkan-hpp Migration

`VULKAN_HPP_MIGRATION_PLAN.md` is in progress. Concrete remaining offenders
spotted in the tree:

- `src/ml/GPUInference.cpp` — pure C API, init/destroy pattern.
  Self-contained. Strong candidate for the **next** migration unit.
- `src/postprocess/BloomSystem.cpp:285-286` — `vkDestroyFramebuffer`,
  `vkDestroyImageView`.
- `src/postprocess/GodRaysSystem.cpp:163,167,183` — `vkDestroyShaderModule`,
  `vkDestroyImageView`.
- `src/postprocess/ComputeBloomSystem.cpp:221,225,260` —
  `vkDestroyShaderModule`.
- `src/gui/GuiSystem.cpp:152` — `vkDestroyDescriptorPool`.
- `src/debug/GpuProfiler.cpp:138` — `vkDestroyQueryPool`.

**Recommendation:** pick `GPUInference` next (smallest blast radius,
eliminates init/destroy at the same time). Leave the one-line `vkDestroy*`
calls until those systems adopt `VulkanRAII` wrappers — migrating them in
isolation adds churn without structural benefit.

---

## 5. ECS — Parallel Infrastructure, Dormant in Rendering

`src/ecs/` is ~3,000 lines of EnTT-backed infrastructure (`Components.h`
959, `Systems.h` 656, `EntityFactory.h` 617). There are 237 references to
`ecsWorld` / `ecs::World` across the codebase, but the hot concentrations
are in `NPCSimulation`, `SceneControlSubsystem`, and `ECSMaterialDemo`.

The render path does **not** iterate ECS components. `Renderer` stores
`ecsWorld_` via `setECSWorld()`, but passes still walk legacy `Renderable*`
collections from `SceneManager`. `Application` even keeps a
`scenePhysicsBodies` vector alongside ECS.

**Recommendation:** `ECS_MIGRATION_PLAN.md` Phase 1 is the blocker. Either
commit to migrating one render pass (e.g. static scene objects) end-to-end
as a proof point, or be explicit that ECS is scoped to NPC/gameplay and
will not drive rendering. The worst outcome is leaving both systems in
place indefinitely.

---

## 6. Convention Violations (Concrete)

**`src/core/asset/AssetRegistry.h:196` + `AssetRegistry.cpp:181`** — the
method is named `loadShader()`, directly contradicting `CLAUDE.md`'s rule:
*"There is NO `loadShader` method."* No external callers exist. Fix A in
this review renames it to `loadShaderModule` to match
`ShaderLoader::loadShaderModule`.

**`src/water/FlowMapGenerator.cpp:263-283`** — hand-rolled bilinear sample
plus `float worldHeight = h * heightScale;`. Duplicates both the formula
and the bilinear helper from `src/terrain/TerrainHeight.h`. `CLAUDE.md`
explicitly forbids this. Fix B replaces the block with
`TerrainHeight::sampleWorldHeight(...)`.

**`src/controls/*ControlSubsystem.h`** — 7 subsystems inherit from
`IControl*` interfaces. Called out but **not** changed: single-level
interface inheritance for polymorphism is a reasonable exception to
*"prefer composition"* and the churn would outweigh the benefit.

---

## 7. File-Size Outliers

Top six by line count:

| File | Lines |
|------|-------|
| `src/scene/Application.cpp` | 1574 |
| `src/scene/SceneBuilder.cpp` | 1311 |
| `src/animation/MotionDatabase.cpp` | 1179 |
| `src/loaders/FBXLoader.cpp` | 1071 |
| `src/vegetation/TreeRenderer.cpp` | 1064 |
| `src/animation/AnimatedCharacter.cpp` | 1034 |
| `src/vegetation/TreeLeafCulling.cpp` | 1042 |
| `src/core/Renderer.cpp` | 1017 |

`Application.cpp` is the clearest candidate to split — it mixes input
routing, physics sync, GUI toggling, frame submission, and player state.
`FBXLoader` and `MotionDatabase` are naturally large (format/algorithm
code) and less worth splitting unless a specific pain point emerges.

---

## 8. Documentation Sprawl

`docs/` contains 27 markdown files, many overlapping PLAN/ANALYSIS/PROPOSAL
documents. The duplication makes it hard to tell which plans are current:

- `PLAN.md`, `ARCHITECTURE.md` — current reference
- `PLAN-eliminate-two-phase-init.md` — marked complete (disputed, §3)
- `RAII_REFACTOR_PLAN.md` — in progress
- `VULKAN_HPP_MIGRATION_PLAN.md` — in progress
- `ECS_MIGRATION_PLAN.md` — Phase 1 incomplete
- `RENDERER_REFACTORING_PLAN.md` — partial
- `WATER_AAA_MIGRATION_PLAN.md` — phases 1-6 done
- `VIRTUAL_TEXTURING_PLAN.md` — phases 1-6 done
- plus `UNICON_PLAN.md`, `ECS_TREES_PLAN.md`, `SPEEDTREE_ENHANCEMENT_PLAN.md`,
  `STREAMING_COMPARISON_AND_PLAN.md`, `WATER_RENDERING_PLAN.md`, etc.

**Recommendation:** add a one-line `Status:` header to every plan (Active /
Complete / Superseded) and move completed plans to `docs/archive/`. Cheap,
high value for anyone navigating the tree.

---

## Prioritized Punch List

### Applied in this review
- **A.** Rename `AssetRegistry::loadShader` → `loadShaderModule`.
- **B.** Replace `FlowMapGenerator.cpp:263-283` with
  `TerrainHeight::sampleWorldHeight`.
- **C.** Update `PLAN-eliminate-two-phase-init.md` with a residual-work
  section.

### Recommended next (not applied here)
1. Migrate `ml/GPUInference` to vulkan-hpp + RAII — smallest unit that
   resolves init/destroy and raw-C-API in one step.
2. Decide FrameGraph's future: real parallel scheduling, or documentation
   downgrade.
3. ECS: commit one render pass end-to-end, or scope ECS to gameplay only.
4. Split `Application.cpp` along input / physics-sync / frame-submit seams.
5. Audit the 19 `destroy()`/`shutdown()` headers; convert the genuine
   two-phase cases to RAII destructors.
6. Add `Status:` headers to all `docs/*PLAN*.md` and archive completed ones.
