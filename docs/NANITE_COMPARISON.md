# Engine vs. Nanite: Architectural Review

A comparison of this engine's rendering architecture against Unreal Engine 5's Nanite virtualized geometry system, identifying strengths, gaps, and what adoption of Nanite-like techniques would require.

## Overview

Nanite is UE5's GPU-driven virtualized geometry system that renders pixel-scale detail for scenes with billions of triangles. Its core idea: decompose all static geometry into ~128-triangle **clusters** organized in a **DAG** (directed acyclic graph), then perform **per-cluster LOD selection, culling, and rasterization entirely on the GPU**. Materials are evaluated after visibility is determined, via a **visibility buffer** that decouples geometry from shading.

This engine uses a more traditional architecture with some modern GPU-driven elements. The comparison below evaluates each major subsystem.

---

## 1. Geometry Representation

| Aspect | This Engine | Nanite |
|---|---|---|
| **Mesh format** | Traditional vertex/index buffers (60 bytes/vertex) | ~128-triangle clusters in a DAG (~14.4 bytes/triangle) |
| **Organization** | Per-object mesh with AABB | Clusters grouped hierarchically via METIS graph partitioning |
| **Compression** | None — raw vertex data | Quantized positions, no stored tangents, specialized encoding |

**Gap**: The engine uses conventional per-object meshes. Nanite's fundamental innovation is the cluster DAG — everything else (per-cluster LOD, per-cluster culling, software rasterization) flows from this representation. This is the single largest architectural difference.

**What it would take**: An offline mesh preprocessing pipeline that partitions meshes into clusters via graph partitioning (METIS), builds a simplification hierarchy with boundary-locked edge collapse, and stores the result as a DAG with monotonic error metrics. This is a substantial tooling effort.

---

## 2. LOD System

| Aspect | This Engine | Nanite |
|---|---|---|
| **Terrain LOD** | CBT adaptive subdivision (depth 6–28), screen-space metric, temporal spreading | N/A (Nanite targets meshes, not heightmap terrain) |
| **Mesh LOD** | `CharacterLODSystem` (distance-based discrete), `TreeLODSystem` (geometry→billboard) | Continuous per-cluster LOD via DAG cut; ~1px error threshold |
| **Transitions** | IGN dithering + temporal variation for cross-fade | Seamless — boundary vertices locked during simplification, no popping |
| **Granularity** | Per-object | Per-cluster (~128 triangles) |

**Engine strength**: The terrain CBT system is excellent and more appropriate for heightmap terrain than Nanite would be. Nanite's cluster hierarchy assumes arbitrary mesh topology. Screen-space adaptive subdivision with split/merge hysteresis is well-engineered.

**Gap**: For non-terrain meshes, the engine uses traditional discrete LOD with per-object swaps. Nanite's continuous per-cluster LOD means different parts of the same object can be at different detail levels simultaneously, and transitions are crack-free by construction (boundary vertices are locked during simplification). The dithering cross-fade is a reasonable approximation but is perceptible as noise.

---

## 3. Culling

| Aspect | This Engine | Nanite |
|---|---|---|
| **Frustum culling** | GPU compute (`GPUCullPass`), per-object AABB/sphere | GPU compute, per-instance then per-cluster |
| **Occlusion culling** | Hi-Z pyramid, single-pass | Two-pass HZB: previous-frame HZB → rasterize → new HZB → re-test |
| **Backface culling** | Standard pipeline | GPU compute per-cluster (entire cluster rejected if back-facing) |
| **Granularity** | Per-object (~8K object limit) | Per-cluster (~128 triangles) |

**Engine strength**: Working GPU-driven Hi-Z occlusion culling is the right foundation. Tree impostor culling via Hi-Z is a nice specialization.

**Gaps**:
1. **Granularity**: A large building 90% behind a wall still renders fully in this engine. Nanite culls individual clusters, saving the 90%.
2. **Two-pass HZB**: Nanite uses last frame's HZB as a starting point, rasterizes against it, builds a new HZB, then re-tests previously-occluded objects. This catches significantly more occlusion than a single-pass approach. Could be implemented independently of the cluster system.

---

## 4. Rasterization

| Aspect | This Engine | Nanite |
|---|---|---|
| **Approach** | Standard hardware rasterization (`vkCmdDrawIndexed`, `vkCmdDrawIndexedIndirectCount`) | Hybrid: software rasterizer (compute) for small triangles + hardware for large |
| **Visibility buffer** | None — writes directly to render targets | 64-bit per pixel (depth + cluster ID + triangle ID) |
| **Material evaluation** | Coupled with rasterization in the same draw call | Fully decoupled: rasterize visibility, then evaluate materials per-pixel |

**Gap**: This is the second major architectural difference.

Nanite's **software rasterizer** (a compute shader doing scan-line rasterization with 64-bit atomics) is ~3x faster than hardware for sub-pixel triangles because it avoids the GPU's 2x2 quad overhead. Over 90% of Nanite clusters are software-rasterized.

The **visibility buffer** decouples geometry from materials entirely. The engine's traditional approach binds a material and rasterizes geometry together per draw. A visibility buffer pass would be a significant pipeline change but would eliminate overdraw and enable material evaluation only for visible pixels.

---

## 5. Draw Call Architecture

| Aspect | This Engine | Nanite |
|---|---|---|
| **CPU involvement** | Material sorting + descriptor set binding per draw; GPU indirect optional | Zero CPU draw calls — entirely GPU-driven |
| **Indirect draws** | `vkCmdDrawIndexedIndirectCount` for GPU-culled objects | Compute dispatch lists per raster bin |
| **Batching** | Material-sorted, per-(mesh, material) pair | Raster bins by material; one dispatch per bin |
| **Scale** | ~8K objects before degradation | 16M instances, billions of triangles |

**Engine strength**: The GPU-driven indirect draw path (`GPUSceneBuffer` → `GPUCullPass` → indirect draw) is the right direction and the foundation that Nanite builds on.

**Gap**: The ~8K object limit reflects per-object granularity. Nanite's cluster-based approach has much smaller work units but the GPU handles dispatch internally. The shift needed: from "GPU culls objects, CPU issues indirect draws" to "GPU culls clusters, GPU dispatches rasterization."

---

## 6. Streaming

| Aspect | This Engine | Nanite |
|---|---|---|
| **Terrain** | Tile-based streaming with async transfer (LOD tiles, load/unload radii) | N/A for terrain |
| **Mesh data** | All mesh data resident in GPU memory | Page-based streaming — only visible cluster pages loaded |
| **Textures** | `AsyncTextureUploader` with non-blocking uploads | Virtual texturing (separate system) |

**Engine strength**: Terrain tile streaming with async transfers on a dedicated queue is well-designed and mirrors virtual texturing concepts.

**Gap**: Mesh geometry is fully resident. Nanite streams cluster pages on demand, keeping only the hierarchy metadata always in memory. Essential for billion-triangle scenes but not needed at the engine's current scale.

---

## 7. Shadows

| Aspect | This Engine | Nanite |
|---|---|---|
| **Approach** | 4-cascade shadow maps with per-cascade frustum culling | Virtual Shadow Maps (16K x 16K virtual, page-based) |
| **Resolution** | Fixed cascades | Per-page allocation matching screen-space detail |
| **Rendering** | Separate shadow pass with depth-only shaders | Reuses Nanite cluster culling/rasterization from light perspective |

**Gap**: Virtual Shadow Maps are tightly coupled with Nanite — they reuse the same infrastructure from the light's perspective. The engine's cascaded shadow maps are traditional but functional. VSMs would only make sense after a cluster-based geometry system is in place.

---

## What the Engine Does Well

1. **Terrain system** — CBT adaptive subdivision is arguably better for heightmap terrain than Nanite. Screen-space LOD with hysteresis and temporal spreading is solid.
2. **GPU-driven foundation** — Hi-Z culling, indirect draws, GPU scene buffers, and compute-driven culling are the infrastructure Nanite builds on.
3. **Async infrastructure** — Dedicated transfer queues, async texture uploads, and tile streaming show the right patterns for a streaming system.
4. **Frame graph** — Dependency-driven parallel render pass execution supports the additional compute passes a Nanite-like system would need.
5. **Multi-threaded command recording** — Per-thread command pools with secondary command buffers demonstrate the parallel recording patterns needed at scale.

---

## Adoption Roadmap

If Nanite-like rendering were a goal, here's the dependency chain:

### Phase 1: Cluster Mesh Representation (prerequisite for everything)
- Offline mesh decomposition into ~128-triangle clusters
- DAG hierarchy with boundary-locked edge-collapse simplification
- Screen-space error metrics per cluster/group
- New on-disk format and asset pipeline

### Phase 2: GPU DAG Traversal and Per-Cluster Culling
- Extend `GPUCullPass` from per-object to per-cluster
- Persistent-thread BVH traversal in compute
- Combined LOD selection + occlusion test in a single dispatch

### Phase 3: Visibility Buffer
- 64-bit atomic visibility buffer (depth + cluster ID + triangle ID)
- Deferred material classification (tile-based)
- Per-material full-screen evaluation pass into GBuffer
- Existing deferred lighting continues unchanged downstream

### Phase 4: Software Rasterizer
- Compute shader scan-line rasterization for small-triangle clusters
- 64-bit atomic compare-and-swap writes to visibility buffer
- Hybrid decision: clusters below ~32px edge size → software path
- Coordinate with hardware path via raster bins

### Phase 5: Two-Pass HZB (can be done independently after Phase 2)
- Extend existing `HiZSystem` to retain previous-frame HZB
- Pass 1: cull against previous HZB, rasterize, build new HZB
- Pass 2: re-test previously-occluded instances/clusters against new HZB

### Phase 6: Cluster Streaming (optimization, only needed at scale)
- Pack clusters into streamable pages
- Keep hierarchy always resident, stream vertex/index data on demand
- Extend `AsyncTransferManager` patterns to mesh pages

### Phase 7: Virtual Shadow Maps (optional, coupled to cluster system)
- Page-based shadow map allocation
- Reuse cluster rasterization from light perspective
- Replace cascaded shadow maps

**Phases 1–2 are the critical path.** The engine's existing GPU culling and indirect draw infrastructure means Phases 3–5 are evolutionary. Phases 6–7 are optimizations relevant only at massive scale.

---

## Conclusion

The engine is well-positioned for selective adoption of Nanite concepts. The terrain system already demonstrates adaptive GPU-driven LOD that in some ways exceeds what Nanite offers for heightmap geometry. The GPU culling pipeline, indirect draws, Hi-Z system, and async transfer infrastructure are the correct foundations.

The largest gaps — cluster mesh representation and the visibility buffer — are architectural choices that would require significant new systems rather than incremental improvements to existing ones. A pragmatic approach would be to adopt the **two-pass HZB** (independent of clusters, immediate culling improvement) and explore **cluster-based representation for static meshes** as the first steps toward Nanite-like capabilities.
