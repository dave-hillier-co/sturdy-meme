# Development Plan

Current state and next steps for the Vulkan outdoor rendering engine.

---

## Completed Systems

These major systems are fully implemented and feature-complete:

### Water Rendering ✅
Core water system complete: flow maps, foam (Jacobian + temporal + intersection + wakes), mini G-buffer, vector displacement, FBM surface detail, screen-space tessellation, PBR light transport, refraction, caustics, SSR, dual depth, material blending, enhanced SSS. Further enhancements tracked in `WATER_AAA_MIGRATION_PLAN.md` (underwater volume renderer, planar reflections, breaking waves, flow networks).

### Tree Rendering ✅
All 6 phases complete:
- Spatial partitioning (uniform grid cells)
- Hi-Z occlusion culling
- Two-phase tree-to-leaf culling
- Screen-space error LOD selection
- Temporal coherence caching
- Octahedral impostor mapping

### Grass System ✅
All 7 phases complete (see `plans/grass-system-improvements.md`):
- DisplacementSystem extraction
- VegetationRenderContext
- ParticleSystem decoupling
- TileManager split (tracker + resource pool)
- LOD strategy interface (4 presets)
- Async tile loading with priority queue
- Debug visualization

---

## In Progress: Procedural Cities

Multi-phase project for generating medieval settlements. Two separate lineages exist; do
not confuse them:

### What is built: the 2D layout generator (`tools/town_generator`)

A standalone C++ town-layout generator (Watabou-style) that is the working 2D concept tool:
- Geometry core: `Point`/`Segment`/`Polygon`/`Graph`, Voronoi, DCEL, polygon boolean ops, splines
- A full ward system: Cathedral, Market, Castle, Harbour, Alleys, Farm, Park, Wilderness
- City structure: curtain walls, canals, districts, blocks, buildings, landmarks
- SVG output (`src/svg/SVGWriter.cpp`) and a CLI (`--seed`, `--size`, `--cells`, `--coast`)
- Unit tests (`tools/town_generator/tests/`) registered with the top-level CTest

Known gaps in this lineage:
- **Standalone build is broken**: `src/main.cpp` uses `SDL_Log` but the tool's
  `CMakeLists.txt` does not link SDL3, so the CLI fails to compile (`'SDL3/SDL.h' not found`).
  The library sources compile; only the entry point is affected.
- **Not engine-integrated**: nothing under `src/` consumes `town_generator` output. The
  runtime's existing `generated/terrain_data/roads/settlement_*` data comes from a different
  pipeline (BiomeGenerator / erosion road network), not this tool.

### Aspirational design (`docs/procedural_cities/`)

The `procedural_cities/` docs and `IMPLEMENTATION_CHECKLIST.md` describe a *different,
unbuilt* architecture (frontage/burgage lot subdivision, space-colonization street growth,
runtime streaming with impostor LOD). That checklist is aspirational, not a status of
`town_generator`, and its items do not map onto the tool above.

### Next steps

1. **Fix the `town_generator` standalone build** — link SDL3 in `tools/town_generator/CMakeLists.txt`
   so the CLI compiles and layouts can be iterated again.
2. **Bridge 2D → 3D in-engine** — consume the layout (wards, walls, building footprints) and
   instantiate it on the terrain through the GPU-indirect scene path: markers → footprints →
   blockout volumes. This is the path to milestone M2.5 (roamable settlements).
3. **Reconcile the design docs** with the tool that actually exists.

### Visual Milestones
Target checkpoints (M2.5 is the key roamable-world milestone):

| Milestone | Description |
|-----------|-------------|
| M1 | World markers - visualize settlement positions |
| M2 | Footprints - 2D layout with colored quads |
| **M2.5** | **Roamable world - character can walk between settlements** |
| M3 | Blockout volumes - extruded boxes with collision |
| M4 | Silhouettes - pitched roofs, towers, crenellations |
| M5 | Structural articulation - timber frames, openings |
| M6 | Material assignment - base colors, weathering |
| M7 | Facade detail - windows, doors, chimneys |
| M8 | Props and ground detail - carts, fences, gardens |
| M9 | Interiors - floor plans, furniture |
| M10 | Polish - full PBR, baked AO |

---

## Backlog: Future Work

From `FUTURE_WORK.md` - features not yet implemented:

### Camera Improvements
- Smoothing (interpolated yaw/pitch/distance)
- Occlusion handling (fade instead of clip)
- Orientation lock (strafe mode)
- Dynamic FOV during sprint

### Procedural Trees
GPU-driven tree generation:
- L-systems or Space Colonization
- Bark textures with normal mapping
- Branch sway with WindSystem

### Painterly Tree Rendering
Stylized approach inspired by The Witness:
- Spherical normals for leaf clumps
- Edge fade at perpendicular angles
- Shadow proxy spheres
- Interior shading with SSS
- Layered wind animation

### Atmosphere - Missing Features
- Paraboloid cloud maps (triple-buffered)
- Cloud temporal reprojection
- Improved Perlin-Worley 3D noise
- Irradiance LUTs

### Post-Processing - Missing Features
- Local tone mapping (bilateral grid)
- Color grading (LUT support)
- Additional tone mappers (GT, AgX)
- Vignette
- Full Purkinje effect (LMSR)

### Wet Surfaces
- Material changes (roughness, albedo, normals)
- Puddle formation in concave areas
- Drying simulation

### Advanced Threading
- Physics on worker threads
- AI/gameplay parallelization
- Animation updates parallel to rendering

---

## Maintenance Tasks

### Tree Rendering Cleanup
Legacy code that can be removed once tested:
- Debug flags (elevation override, cell index display)
- Single-phase leaf culling shader
- Distance-based LOD code paths
- 17-view impostor atlas code

### Code Quality
- Renderer decomposition: passes as self-describing `IRenderPass` objects registered with the pass scheduler (removes callback trampolines), split frame update from command recording, extract scene composition and bootstrap from `Renderer`

---

## Priority Recommendations

### High Priority (Major User-Visible Impact)
1. **Procedural Cities M2.5** - Roamable world is the key milestone
2. **Camera smoothing** - Low effort, high polish

### Medium Priority (Quality Improvements)
3. **Painterly tree rendering** - Visual differentiation
4. **Wet surfaces** - Weather immersion

### Lower Priority (Technical Debt)
5. **Tree rendering cleanup** - Remove legacy toggles
6. **Atmosphere improvements** - Cloud quality
7. **Post-processing additions** - Color grading, vignette

---

## Testing Reminders

From CLAUDE.md:
- Build: `cmake --preset debug && cmake --build build/debug`
- Run: `./run-debug.sh`
- Shaders compile via cmake
- Always ensure build compiles AND runs without crashing
