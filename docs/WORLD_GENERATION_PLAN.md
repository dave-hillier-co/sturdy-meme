# Procedural World Generation — State and Phased Plan

This document records the audited state of the procedural world generation systems and
lays out a phased plan for closing the gaps. Each phase leaves the build compiling, the
app running, and the world looking better than the phase before — no phase depends on a
later one to be presentable.

Related documents: `PROCEDURAL_CITIES_REQUIREMENTS.md` (M1–M10 quality ladder),
`ROAD_NETWORK_DESIGN.md` (road system design), `ARCHITECTURE.md` §"Procedural Content
Pipeline", `PLAN.md` (overall engine plan).

---

## Current State

### The pipeline (authoritative: root `CMakeLists.txt`, `terrain_preprocessing` target)

```text
assets/terrain/isleofwight-0m-200m.png   (single authored input, gitignored)
        |
        +--> terrain_preprocess  -> LOD height tile pyramid (tile_x_z_lodN.png)
        +--> watershed           -> flow dir/accum, rivers.geojson, lakes.geojson (stub)
                |
                +--> biome_preprocess -> biome_map.png, settlements.json
                        |
                        +--> road_generator -> roads.geojson ----+
                        +--> town_generator -> towns/town_<id>.geojson
                                                                 |
                                              +--> tile_generator -> vt_tiles/ (VT albedo)
```

A `preview` target runs the same chain (minus VT tiles) into
`generated/terrain_data_preview/` and composites `world_preview.png/svg` via
`tools/world_preview`.

### What works end-to-end

- **Terrain**: streamed height tiles (`TerrainTileCache`/`TerrainTileDiskLoader`),
  coarse-LOD fallback heightmap, feedback-driven virtual texturing of the generated
  albedo tiles.
- **Hydrology analysis**: D8 flow, depression resolution, river tracing, basin labels —
  rivers are baked into the VT albedo by `tile_generator`.
- **Biomes**: 9-zone classification (TWI, stream order, distances) drives vegetation
  density/species and settlement suppression at runtime via `src/world/BiomeMap`.
- **Settlements and towns**: `biome_preprocess` places settlements; `town_generator`
  (Watabou-style, deterministic, CTest-verified) emits per-town GeoJSON;
  `SettlementBlockoutGenerator` + `KitBuildingAssembler` assemble kit-piece buildings
  with facade grammar, gabled roofs, and Jolt colliders at runtime.
- **Roads**: terrain-aware A* network in `roads.geojson`, baked into the VT albedo.
- **Runtime generation**: procedural tree meshes, GPU compute-placed grass, runtime
  flow-map generation for water shading.

### Known gaps (verified in code)

| Gap | Where |
| --- | --- |
| Lakes are a stub — `write_lakes_geojson` always emits an empty FeatureCollection | `tools/watershed/src/river_binary.cpp` |
| Roads cross open water (the Solent) as straight lines; no bridge/ford features | `tools/road_generator/`, visible on `preview` |
| Rivers/lakes/roads GeoJSON loaded at runtime but only feed debug cones (`RoadRiverVisualization`) — no real geometry | `src/terrain/ErosionDataLoader.cpp`, `src/terrain/RoadNetworkLoader.cpp`, `src/core/RendererBuilder.cpp` |
| Town street and wall LineStrings exported but not rendered | `src/world/SettlementBlockoutGenerator.cpp` (extrudes buildings only) |
| `StreetGenerator` (intra-settlement streets/lots) and space colonization implemented in `road_generator` but never invoked by the build | root `CMakeLists.txt` omits `--generate-streets` / `--use-colonization` |
| `dwelling_generator` fully disconnected: SVG-only output, no pipeline invocation, no runtime consumer | `tools/dwelling_generator/` |
| `flow_accumulation.exr` existence-checked but never parsed at runtime; `WaterPlacementData::flowAccumulation/flowDirection` never populated | `src/terrain/ErosionDataLoader.cpp` |
| `preprocess.sh` is stale: passes `--heightmap`/`--flow-threshold` which the watershed CLI rejects, omits town/VT stages, divergent defaults | `preprocess.sh` |
| `material_texture_gen` built but never invoked | `tools/CMakeLists.txt` |
| Biome-driven vegetation covers a limited radius, not the whole island | `src/vegetation/VegetationContentGenerator` |

---

## Phased Plan

Ordering rationale: fix what is visibly wrong first (broken tooling, roads through the
sea), then promote existing vector data to real geometry (largest visual payoff per unit
of new code), then wire up the finished-but-dormant generators, then extend coverage.

### Phase 0 — Pipeline hygiene

Make the pipeline honest about what it does and runnable from a fresh checkout.

- Fix or retire `preprocess.sh`. Preferred: reduce it to a thin wrapper around
  `cmake --build <dir> --target terrain_preprocessing` so there is exactly one
  pipeline definition; otherwise fix the watershed CLI invocation and add the missing
  town/VT stages and CMake-matching parameters.
- Document the input heightmap requirement (source, expected format, where to place it)
  in `README.md` — the file is gitignored, so a fresh checkout cannot build the world
  without it and nothing currently says so.
- Remove the dead spill-merge code in `tools/watershed/src/d8.cpp` (~lines 405–411).
- Decide the fate of `material_texture_gen`: wire it in or delete it.

**Test**: from a clean `generated/`, `cmake --build build/debug --target
terrain_preprocessing` and `--target preview` both succeed; `world_preview.png` shows
terrain, rivers, biomes, roads, settlements. `./run-debug.sh` runs without crashing.

### Phase 1 — Real lakes

Replace the empty-FeatureCollection stub with actual lake extraction.

- In `tools/watershed`, derive lakes from the depression-resolution pass: interior
  basins whose spill elevation traps water become lake polygons (fill to spill level),
  with `waterLevel`, `area`, and `depth` properties in `lakes.geojson`.
- Rasterize lakes into the biome map (extend the existing River/Wetland zone logic) and
  into the VT albedo via `tile_generator`'s existing spline/polygon rasterization.
- Runtime: `ErosionDataLoader` already parses `lakes.geojson`; place flat water patches
  at lake `waterLevel` (the simplest correct visual — a per-lake water tile using the
  existing `WaterSystem` lake preset).

**Test**: `preview` shows lakes as distinct water bodies where the terrain has closed
depressions; in-game, walk to a lake and see a water surface at the right level with
the existing water shading. Determinism: run the pipeline twice, diff `lakes.geojson`.

### Phase 2 — Roads that respect water: no sea crossings, bridges, fords

- In `RoadPathfinder`, make open sea impassable (currently a finite penalty lets A*
  cross the Solent). Settlements that become unreachable simply drop the connection.
- Detect river crossings on accepted paths using stream order from the watershed data
  (per `ROAD_NETWORK_DESIGN.md` §"Bridge and Ford Detection"): low order → ford, higher
  order → bridge. Emit crossing Point features (`kind: bridge|ford`) in
  `roads.geojson`.
- Runtime: render fords as a widened rocky VT stamp (tile_generator), bridges as simple
  kit/blockout meshes placed by a new small step in the settlement/terrain deferred
  generation path (flat deck spanning the banks, colliders included so the road is
  walkable).

**Test**: `preview` no longer shows roads across the Solent; every remaining
river/road intersection carries a bridge or ford marker. In-game, walk a road across a
river — cross on a deck or ford without falling into the riverbed.

### Phase 3 — Linear features as real geometry

Promote roads, rivers, and town streets/walls from debug cones and texture bakes to
draped meshes. This is the largest visual step and makes the vector data load-bearing.

- **Road meshes**: build a runtime road-ribbon generator that drapes each `RoadSpline`
  over the terrain (sampling `BaseHeightMap`), width from the GeoJSON, with a
  road-surface material. Keep the VT bake as the far-distance representation; fade the
  mesh in near the camera. Reuse the deferred-generation pattern from
  `DeferredTerrainObjects` (wait for terrain heights).
- **River ribbons**: same drape approach for `RiverSpline`s, but emitting water-shaded
  geometry (flow direction along the spline, width from accumulated flow) so rivers
  read as moving water rather than painted albedo. Feed spline flow into
  `FlowMapGenerator` rather than recomputing from the heightmap alone.
- **Town streets and walls**: extend `SettlementBlockoutGenerator` to consume the
  street and wall LineStrings already present in `town_<id>.geojson` — streets as flat
  draped ribbons with a distinct material, walls as extruded kit/blockout runs with
  colliders.
- Retire the debug-cone-only role of `RoadRiverVisualization` to an optional overlay.

**Test**: stand on a road — visible road surface underfoot, not just tinted terrain;
follow a river — animated water in the channel; enter a town — streets visibly paved
and walls solid (blocked by collider). Toggle the debug overlay to compare geometry
against source splines.

### Phase 4 — Intra-settlement streets and lots

Wire up the dormant `StreetGenerator`.

- Add `--generate-streets` to the `roads_gen` CMake command (and the `preview`
  pipeline); emit per-settlement `streets.geojson` / `lots.geojson` into
  `generated/terrain_data/roads/`.
- Reconcile with `town_generator`: towns/villages already get Watabou street layouts —
  decide per settlement type which generator owns streets (suggested: `town_generator`
  for Town/Village/FishingVillage, `StreetGenerator` for hamlets and farmsteads that
  have no town GeoJSON) so the two lineages do not double-generate.
- Runtime: render `streets.geojson` through the Phase 3 road-ribbon path; use
  `lots.geojson` to place outlying kit buildings on subdivided plots.
- Optionally enable `--use-colonization` and feed its topology into
  `determineConnections()` (closing the `ROAD_NETWORK_DESIGN.md` integration gap).

**Test**: `preview` shows internal street patterns in settlements; in-game, hamlets
have lanes and plot-aligned buildings instead of a bare cluster. Determinism diff on
`streets.geojson`/`lots.geojson` across two runs.

### Phase 5 — Whole-island coverage

- Extend biome-driven vegetation from the current limited radius to island-wide, using
  the existing impostor/LOD systems for distant trees (see `PLAN.md` backlog and the
  tree systems already in place).
- Verify streaming budgets hold when roaming settlement-to-settlement (the M2.5
  "roamable world" bar in `PROCEDURAL_CITIES_REQUIREMENTS.md`).

**Test**: teleport across the island via `SettlementRegistry` markers — every viewpoint
shows appropriate vegetation to the horizon; walk between two settlements without
hitches or bare patches; frame time stays within existing budgets.

### Phase 6 — Dwellings and building interiors

Connect the orphaned `dwelling_generator` toward the M9 interiors milestone.

- Add a JSON/GeoJSON export to `dwelling_generator` (rooms, doors, windows per floor)
  alongside its SVG output, following the standard-formats rule.
- Generate a dwelling plan per building lot (seeded from settlement id + building
  index for determinism) as a build step for a pilot settlement.
- Runtime pilot: for one settlement, assemble interior walls/floors from the plan
  behind the existing kit facades, with door openings and colliders.

**Test**: enter a pilot-settlement building through its door into rooms matching the
generated plan; regenerate and confirm the same seed yields the same interior.

### Phase 7 — Cities quality ladder (M4+)

With the world structurally complete, continue the `PROCEDURAL_CITIES_REQUIREMENTS.md`
ladder: silhouettes and roofscape variety, material weathering, facade detail, props
and ground clutter, ports/defensive structures, agriculture. Sequence within this phase
per that document; each rung is independently shippable.

---

## Standing constraints

- Every phase must leave `cmake --preset debug && cmake --build build/debug` compiling
  and `./run-debug.sh` running without crashing.
- New interchange data uses the standard formats (GeoJSON / EXR / PNG) — no custom
  binary formats.
- Generated content is produced by the build, never committed; determinism is the
  contract (extend the GeoJSON determinism test to new outputs).
- Use `cmake --build <dir> --target preview` as the fast feedback loop before running
  the full app.
