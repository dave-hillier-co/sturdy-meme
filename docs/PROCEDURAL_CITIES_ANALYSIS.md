# Procedural Cities - Critical Analysis

A critical review of the existing infrastructure for procedural settlement generation, identifying gaps, limitations, and key design decisions needed before implementation.

---

## Executive Summary

The codebase has **foundational infrastructure** for procedural content:
- Settlement point placement based on terrain analysis
- Road network generation connecting settlements
- Biome classification informing land use

However, **no actual city/building generation exists**. Settlements are abstract points with metadata - there's no geometry, no urban layout, no structures. This document analyzes what exists, what's missing, and poses critical questions to inform a proper implementation plan.

---

## 1. Current State Analysis

### 1.1 Settlement Placement (BiomeGenerator)

**Location**: `tools/biome_preprocess/BiomeGenerator.cpp`

**What it does**:
- Grid-samples terrain at 200m intervals for candidate locations
- Scores locations based on:
  - Proximity to rivers (bonus if 20-200m, penalty if <20m flood zone)
  - Coastal access (bonus if 50-500m from sea)
  - Slope (bonus for flat terrain <0.1)
  - Elevation (bonus for defensible 20-60m)
  - Flow accumulation (potential ford/crossing points)
  - Biome type (agricultural/grassland preferred)
- Greedy placement with minimum distance constraints:
  - Hamlets: 400m apart
  - Villages: 800m apart
  - Towns: 2000m apart

**Settlement Types**:
| Type | Criteria | Features |
|------|----------|----------|
| Town | Score >8, coastal or high flow | market |
| FishingVillage | Coastal, score >5 | harbour |
| Village | Score >5 | - |
| Hamlet | Default | - |

**Output**: `settlements.json` with position, type, score, feature tags

**Limitations**:
1. **No size/footprint** - settlements are dimensionless points
2. **No population model** - no concept of how many buildings
3. **No historical simulation** - settlements don't "grow" organically
4. **Fixed scoring weights** - not data-driven or tunable
5. **No road influence** - placement ignores connectivity (chicken/egg problem)
6. **No defensive considerations** - no walls, castles, strategic positions

### 1.2 Road Network (RoadPathfinder)

**Location**: `tools/road_generator/RoadPathfinder.cpp`

**What it does**:
- A* pathfinding on a 512x512 grid
- Cost function considers:
  - Slope (5x multiplier per unit slope)
  - Water crossing (1000 penalty)
  - Cliff areas (500 penalty, slope >0.5)
- Douglas-Peucker simplification (10m epsilon)
- Road types: MainRoad, Road, Lane, Bridleway, Footpath

**Limitations**:
1. **No road-to-street transition** - roads end at settlement points, no internal streets
2. **No bridges** - water crossings are penalized, not solved
3. **No road hierarchy within settlements** - no main street, market square, etc.
4. **Post-hoc generation** - roads generated after settlements, not co-evolved

### 1.3 Biome System

**What exists**: Zone classification (Sea, Beach, ChalkCliff, SaltMarsh, River, Wetland, Grassland, Agricultural, Woodland)

**Relevance to cities**:
- Agricultural zones could inform farmland plots around settlements
- Woodland could provide timber resources
- No "urban" or "built" zone type exists

---

## 2. Gap Analysis

### 2.1 Missing: Urban Layout Generation

**No system exists to generate**:
- Street networks within settlements
- Building plots/parcels
- Block structure
- Public spaces (squares, markets, churchyards)

**Industry approaches**:
| Approach | Description | Pros | Cons |
|----------|-------------|------|------|
| L-systems | Grammatical road growth | Organic patterns | Hard to control |
| Agent-based | Simulate settlers/builders | Emergent behavior | Slow, unpredictable |
| Template-based | Pre-designed layouts scaled | Fast, predictable | Repetitive |
| Tensor fields | Vector field-guided streets | Smooth, controllable | Complex implementation |
| Voronoi/Delaunay | Geometric subdivision | Fast, irregular | Unnatural without tuning |

### 2.2 Missing: Building Generation

**No system exists for**:
- Building footprint generation
- Building mass/volume
- Architectural style
- LOD representations
- Instancing/batching for performance

**Possible approaches**:
| Approach | Description | Suitability |
|----------|-------------|-------------|
| Prefab placement | Pre-modeled buildings | Fast, limited variety |
| Procedural grammar | Shape grammars (CGA) | High variety, complex |
| Modular kit | Snap-together pieces | Good variety/performance |
| Hybrid | Prefab + procedural details | Balanced |

### 2.3 Missing: Historical/Cultural Context

The codebase mentions "south coast of England" terrain types but has no:
- Historical period specification (Medieval? Tudor? Georgian?)
- Building style vocabulary
- Cultural/religious building requirements (church, market cross, etc.)
- Social hierarchy in building placement

### 2.4 Missing: Runtime Integration

No path exists from preprocessed settlement data to rendered geometry:
- No `BuildingSystem` or `SettlementSystem` in renderer
- No building meshes or textures
- No LOD strategy for buildings
- No culling/streaming for urban areas

---

## 3. Critical Design Questions

### 3.1 Scope & Ambition

**Q1**: What is the target scale?
- [ ] A) A few hand-placed hero settlements with procedural details
- [ ] B) Dozens of fully procedural settlements across the terrain
- [ ] C) Hybrid: major towns hand-authored, hamlets procedural

**Q2**: What level of detail is needed?
- [ ] A) Distant silhouettes only (impostor cards)
- [ ] B) Walkable exteriors with simple geometry
- [ ] C) Enterable interiors for some buildings
- [ ] D) Full interior detail throughout

**Q3**: Should settlements be static or dynamic?
- [ ] A) Baked at build time, immutable at runtime
- [ ] B) Runtime procedural generation (streaming)
- [ ] C) Player-modifiable (construction/destruction)

### 3.2 Historical Period & Style

**Q4**: What historical period?
- [ ] A) Medieval (1066-1485) - timber frame, thatch, stone churches
- [ ] B) Tudor (1485-1603) - half-timber, brick chimneys
- [ ] C) Georgian (1714-1830) - symmetrical brick, sash windows
- [ ] D) Fantasy/anachronistic mix
- [ ] E) Other: ________________

**Q5**: What building types are required?
- [ ] Houses (cottages, townhouses, manor)
- [ ] Agricultural (barns, granaries, mills)
- [ ] Commercial (shops, inns, markets)
- [ ] Religious (churches, chapels)
- [ ] Civic (guild halls, town halls)
- [ ] Defensive (walls, gates, castles)
- [ ] Industrial (smithies, tanneries)
- [ ] Other: ________________

### 3.3 Technical Approach

**Q6**: Preferred urban layout algorithm?
- [ ] A) Template-based (fast, predictable, some repetition)
- [ ] B) L-system/grammar (organic, complex to tune)
- [ ] C) Agent simulation (emergent, slow to generate)
- [ ] D) Simple grid with noise (fast, less realistic)
- [ ] E) Other: ________________

**Q7**: Preferred building generation method?
- [ ] A) Prefab library (fast, limited variety)
- [ ] B) Modular kit (snap-together walls/roofs/doors)
- [ ] C) Full procedural (shape grammar)
- [ ] D) Hybrid: prefab base + procedural details

**Q8**: When should generation happen?
- [ ] A) Offline (build-time tool, baked assets)
- [ ] B) Runtime (on-demand as player approaches)
- [ ] C) Hybrid (coarse structure baked, details runtime)

### 3.4 Integration & Performance

**Q9**: LOD strategy for buildings?
- [ ] A) Discrete LOD meshes (LOD0-3)
- [ ] B) Impostor billboards at distance
- [ ] C) HLOD (hierarchical - merge distant buildings)
- [ ] D) Combination of above

**Q10**: How should buildings interact with terrain?
- [ ] A) Flatten terrain under buildings (simple)
- [ ] B) Buildings adapt to slope (complex)
- [ ] C) Retaining walls/foundations (realistic)
- [ ] D) Restrict buildings to flat areas only

**Q11**: Culling and streaming approach?
- [ ] A) Per-building frustum culling
- [ ] B) Cluster-based culling (groups of buildings)
- [ ] C) Streaming tiles (load/unload settlement chunks)
- [ ] D) GPU-driven indirect rendering

### 3.5 Art Direction

**Q12**: Visual fidelity target?
- [ ] A) Stylized/low-poly (fast, clear silhouettes)
- [ ] B) Realistic PBR (matches terrain quality)
- [ ] C) Painterly (matches tree rendering style mentioned in FUTURE_WORK.md)

**Q13**: Texture approach?
- [ ] A) Unique textures per building (high memory)
- [ ] B) Texture atlas (efficient, some repetition)
- [ ] C) Procedural textures (GPU-generated)
- [ ] D) Trim sheets + tiling materials

---

## 4. Technical Considerations

### 4.1 Consistency with Codebase Patterns

Based on `ARCHITECTURE.md` and `CLAUDE.md`, any implementation should:

- **Use composition over inheritance** for building/settlement systems
- **Follow InitInfo pattern** for system initialization
- **Use SDL_Log** for all logging (not std::cout)
- **Generate content at build time** where possible (per CLAUDE.md)
- **Save generated textures as PNG**
- **Use vulkan-hpp** with builder pattern for new Vulkan code
- **Integrate with existing systems**: TerrainSystem (height queries), ShadowSystem, FroxelSystem (fog)

### 4.2 Existing Systems to Leverage

| System | Relevance |
|--------|-----------|
| `TerrainSystem` | Height queries for building placement |
| `GrassSystem` | Grass suppression in urban areas |
| `TreeLODSystem` | Similar LOD/culling patterns |
| `RockSystem` | Static geometry rendering patterns |
| `HiZSystem` | Occlusion culling |
| `VirtualTextureSystem` | Texture streaming patterns |

### 4.3 Suggested Incremental Milestones

Following CLAUDE.md's emphasis on incremental progress:

1. **Milestone 1**: Single building type (cottage) placed at settlement points
2. **Milestone 2**: Multiple building types with random selection
3. **Milestone 3**: Simple layout (buildings around a central point)
4. **Milestone 4**: Street network within settlements
5. **Milestone 5**: Plot subdivision and building footprints
6. **Milestone 6**: LOD system for buildings
7. **Milestone 7**: Texture variety and weathering
8. **Milestone 8**: Settlement-specific character (coastal vs inland)

---

## 5. Open Questions for Implementation

### 5.1 Data Pipeline

- Should building assets be modeled externally or generated?
- What format for building definitions? (JSON? Custom DSL?)
- How to version/cache generated settlements?

### 5.2 Performance Budget

- Target building count per settlement?
- Target total draw calls for urban areas?
- Memory budget for building textures?

### 5.3 Gameplay Integration

- Do buildings need collision?
- Are buildings destructible?
- Do NPCs need to path within settlements?
- Is there day/night cycle affecting buildings (lights)?

---

## 6. Recommendations

### 6.1 Immediate Next Steps

1. **Answer the design questions above** - many technical decisions depend on scope/ambition
2. **Establish visual target** - find reference images for desired look
3. **Prototype single building** - get one building rendering before scaling up
4. **Define minimum viable settlement** - what's the simplest "town" that feels alive?

### 6.2 Suggested Starting Point

Based on the codebase's existing patterns and the "south coast of England" theme:

**Recommended approach**:
- **Period**: Medieval/Tudor hybrid (matches pastoral setting)
- **Method**: Modular kit system (walls, roofs, doors as pieces)
- **Generation**: Build-time (matches existing tool pipeline)
- **Layout**: Template-based with noise (predictable but varied)
- **LOD**: Impostor billboards for distant, discrete LOD for close

This balances complexity with the existing codebase patterns and tooling.

---

## Appendix: Reference Materials

### Academic Papers
- Parish & Muller, "Procedural Modeling of Cities" (SIGGRAPH 2001)
- Wonka et al., "Instant Architecture" (SIGGRAPH 2003)
- Merrell et al., "Model Synthesis" (SIGGRAPH Asia 2007)

### Game Industry References
- "The Witcher 3" - Novigrad city generation
- "Kingdom Come: Deliverance" - Medieval Bohemia settlements
- "Assassin's Creed" series - Historical city recreation
- "Medieval Dynasty" - Settlement building mechanics

### Tools/Libraries
- CityEngine (ESRI) - Commercial procedural city tool
- Houdini - Procedural generation workflows
- Wave Function Collapse - Constraint-based generation

---

*Document created for review. Please answer the questions in Section 3 to inform the implementation plan.*
