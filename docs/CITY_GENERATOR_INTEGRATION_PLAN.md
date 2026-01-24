# City Generator Engine Integration Plan

This document outlines the comprehensive plan for integrating the MFCG-based city generator into the 3D engine, enabling runtime rendering of procedurally generated medieval cities.

## Current State

**City Generator (`tools/town_generator/`):**
- Full MFCG (Medieval Fantasy City Generator) port in C++
- Voronoi-based city layout with wards (Castle, Market, Cathedral, Farm, Harbour, Alleys, Park, Wilderness)
- Street network via A* pathfinding from gates to plaza
- Block subdivision into lots then buildings (4-stage: bisector → LIRA → buildings → courtyard)
- Walls with gates and towers
- Coastlines and canals with bridges
- **Output: SVG only** (no intermediate format)

**Engine:**
- Vulkan rendering with SceneManager, MaterialRegistry, and Renderable system
- Terrain with height queries via `TerrainSystem::getHeightAt()`
- Procedural vegetation (grass, trees, scatter)
- glTF/FBX/OBJ mesh loading
- Build-time procedural content generation

---

## Phase 1: Intermediate Data Format (GeoJSON Export)

### Goal
Replace SVG output with structured GeoJSON that preserves all semantic data for 3D conversion.

### Output Files

```
generated/city/
├── city_metadata.json       # City-level config (seed, dimensions, ward colors)
├── city_streets.geojson     # Road network with width/type
├── city_buildings.geojson   # Building footprints with ward type, lot info
├── city_walls.geojson       # Wall segments with gates and towers
├── city_water.geojson       # Coastline, canals, bridges
├── city_wards.geojson       # Ward boundaries for ground materials
└── city_blocks.geojson      # Block outlines (optional, for debugging)
```

### Building Feature Properties

```json
{
  "type": "Feature",
  "geometry": { "type": "Polygon", "coordinates": [...] },
  "properties": {
    "id": "building_0042",
    "ward_type": "Alleys",
    "block_id": "block_012",
    "lot_index": 3,
    "frontage_edge": [[x1, y1], [x2, y2]],
    "area_sqm": 145.2,
    "is_corner": true,
    "touches_street": true,
    "touches_courtyard": false,
    "building_class": "residential",
    "floors": 2
  }
}
```

### Implementation

1. Add `src/export/GeoJSONWriter.h` to town_generator
2. Traverse City data structure, emitting features with semantic properties
3. Preserve frontage edge info (critical for door placement)
4. Add building classification heuristics based on ward type and size:
   - Castle ward → keep, great hall
   - Cathedral ward → church
   - Market ward → shop, tavern, warehouse
   - Alleys ward → residential, small shop
   - Harbour ward → warehouse, dock building

---

## Phase 2: Partial/Progressive Generation

### Goal
Enable generation at different levels of detail for LOD and streaming.

### Generation Levels

| Level | Contents | Use Case |
|-------|----------|----------|
| 0 | Ward boundaries only | World map, far LOD |
| 1 | + Street network, wall outline | Medium distance |
| 2 | + Block outlines | Approaching city |
| 3 | + Building footprints (no subdivision) | Near city |
| 4 | + Full building detail (LIRA rectangles) | In city |
| 5 | + Architectural features (doors, windows) | Close-up |

### API Changes

```cpp
class City {
public:
    void build(int maxLevel = 5);  // Partial build to specified level
    int getCurrentLevel() const;
    void continueToLevel(int level);  // Resume from current level

    // Level-specific accessors
    const std::vector<WardBoundary>& getWardBoundaries() const;  // Level 0+
    const std::vector<StreetSegment>& getStreets() const;        // Level 1+
    const std::vector<BlockOutline>& getBlocks() const;          // Level 2+
    const std::vector<BuildingFootprint>& getBuildings() const;  // Level 3+
    const std::vector<BuildingDetail>& getDetailedBuildings() const; // Level 4+
};
```

### Implementation

1. Refactor `City::build()` to checkpoint after each major phase
2. Store intermediate state for resumption
3. Add level parameter to GeoJSON export
4. Each level file is self-contained (no dependencies on higher levels)

---

## Phase 3: 3D Building Generation

### Goal
Convert 2D building footprints to 3D meshes with procedural architectural detail.

### Building Components

```
BuildingMesh
├── Foundation      (slight inset, stone material)
├── Walls           (per-floor, material varies by ward)
├── Roof            (gabled, hipped, flat based on footprint shape)
├── Doors           (front door required, back door optional)
├── Windows         (procedural placement on non-door walls)
├── Chimneys        (optional, residential buildings)
└── Decorations     (signs for shops, crosses for churches)
```

### Door Placement Algorithm

```cpp
struct DoorPlacement {
    glm::vec2 position;      // 2D position on footprint
    glm::vec2 direction;     // Outward normal
    DoorType type;           // FRONT, BACK, SIDE
    float width;
};

std::vector<DoorPlacement> placeDoors(const BuildingFootprint& footprint) {
    std::vector<DoorPlacement> doors;

    // Front door: center of frontage edge (faces street)
    Edge frontage = footprint.frontageEdge;
    doors.push_back({
        .position = frontage.midpoint(),
        .direction = frontage.outwardNormal(),
        .type = DoorType::FRONT,
        .width = 1.2f
    });

    // Back door: opposite side if building depth > threshold
    if (footprint.depth > 8.0f) {
        Edge backEdge = findOppositeEdge(footprint, frontage);
        if (!backEdge.touchesCourtyardOrNeighbor()) {
            doors.push_back({
                .position = backEdge.midpoint(),
                .direction = backEdge.outwardNormal(),
                .type = DoorType::BACK,
                .width = 1.0f
            });
        }
    }

    return doors;
}
```

### Window Placement

```cpp
struct WindowPlacement {
    glm::vec2 position;
    int floor;
    WindowType type;  // SMALL, LARGE, ARCHED, SHUTTERED
};

std::vector<WindowPlacement> placeWindows(
    const BuildingFootprint& footprint,
    int numFloors,
    const std::vector<DoorPlacement>& doors
) {
    std::vector<WindowPlacement> windows;

    for (const Edge& edge : footprint.edges) {
        if (edge.length < 3.0f) continue;  // Too short for windows

        // Skip door zones
        float availableLength = edge.length;
        for (const auto& door : doors) {
            if (edge.contains(door.position)) {
                availableLength -= door.width + 1.0f;  // Buffer
            }
        }

        // Place windows with spacing
        int windowCount = static_cast<int>(availableLength / 2.5f);
        float spacing = availableLength / (windowCount + 1);

        for (int floor = 0; floor < numFloors; floor++) {
            for (int w = 0; w < windowCount; w++) {
                windows.push_back({
                    .position = edge.pointAt(spacing * (w + 1)),
                    .floor = floor,
                    .type = selectWindowType(footprint.wardType, floor)
                });
            }
        }
    }

    return windows;
}
```

### Roof Generation

```cpp
enum class RoofStyle { GABLED, HIPPED, FLAT, MANSARD };

RoofStyle selectRoofStyle(const BuildingFootprint& footprint) {
    float aspectRatio = footprint.boundingBox.width / footprint.boundingBox.height;

    if (footprint.wardType == WardType::Castle) return RoofStyle::FLAT;
    if (footprint.vertices.size() > 6) return RoofStyle::HIPPED;
    if (aspectRatio > 2.0f) return RoofStyle::GABLED;
    return RoofStyle::HIPPED;
}

Mesh generateRoof(const BuildingFootprint& footprint, float baseHeight, RoofStyle style) {
    switch (style) {
        case RoofStyle::GABLED:
            return generateGabledRoof(footprint, baseHeight);
        case RoofStyle::HIPPED:
            return generateHippedRoof(footprint, baseHeight);
        case RoofStyle::FLAT:
            return generateFlatRoof(footprint, baseHeight);
        case RoofStyle::MANSARD:
            return generateMansardRoof(footprint, baseHeight);
    }
}
```

### Implementation Files

```
src/city/
├── CityLoader.h/cpp           # Loads GeoJSON city data
├── BuildingGenerator.h/cpp    # 2D → 3D building conversion
├── RoofGenerator.h/cpp        # Procedural roof meshes
├── WallGenerator.h/cpp        # Procedural wall meshes with openings
├── DoorPlacer.h/cpp           # Door placement algorithm
├── WindowPlacer.h/cpp         # Window placement algorithm
├── BuildingDecorator.h/cpp    # Signs, chimneys, details
└── CityMaterialSet.h/cpp      # Ward-based material assignment
```

---

## Phase 4: Street and Infrastructure

### Goal
Generate 3D geometry for streets, walls, and water features.

### Street Generation

```cpp
struct StreetMesh {
    Mesh surface;          // Cobblestone/dirt road
    Mesh gutters;          // Edge drainage channels
    Mesh curbs;            // Raised edges (if paved)
};

StreetMesh generateStreet(const StreetSegment& segment, float terrainHeight) {
    float width = segment.isArtery ? 6.0f : 3.0f;

    // Extrude path into quad strip
    // Sample terrain height along path
    // Add UV coordinates for tiling cobblestone texture
    // Generate curb geometry for urban streets
}
```

### Wall Generation

```cpp
struct WallMesh {
    Mesh wallSegments;     // Main wall body
    Mesh battlements;      // Crenellations on top
    Mesh towers;           // Cylindrical towers at corners
    Mesh gates;            // Arched gate openings
    Mesh walkway;          // Top walkway for guards
};

WallMesh generateCityWall(const CurtainWall& wall, float terrainHeightFunc) {
    // Follow wall path, sampling terrain height
    // Wall height: 8-12 units above terrain
    // Tower radius: 1.9-2.5 units (from MFCG constants)
    // Gate width: 4-6 units
    // Add battlements every 1.5 units
}
```

### Bridge Generation

```cpp
Mesh generateBridge(const Bridge& bridge, float waterLevel) {
    // Arch bridge for canals
    // Supports on either bank
    // Deck surface with railings
    // UV for stone texture
}
```

---

## Phase 5: Terrain Integration

### Goal
Place city on terrain with proper height adaptation.

### Height Sampling

```cpp
class CityPlacer {
public:
    void placeOnTerrain(CityData& city, TerrainSystem& terrain) {
        // Find base height (average of city center area)
        float baseHeight = terrain.getHeightAt(city.center.x, city.center.z);

        // For each building:
        for (auto& building : city.buildings) {
            // Sample height at footprint corners
            float minHeight = FLT_MAX;
            for (const auto& corner : building.footprint) {
                float h = terrain.getHeightAt(corner.x, corner.z);
                minHeight = std::min(minHeight, h);
            }

            // Set building foundation height
            building.baseHeight = minHeight;

            // Add foundation if terrain varies significantly
            float heightVariance = maxHeight - minHeight;
            if (heightVariance > 0.5f) {
                building.needsFoundation = true;
                building.foundationHeight = heightVariance + 0.2f;
            }
        }

        // Streets follow terrain with slight smoothing
        for (auto& street : city.streets) {
            for (auto& point : street.path) {
                point.y = terrain.getHeightAt(point.x, point.z) + 0.1f;
            }
            smoothStreetPath(street);  // Avoid jarring height changes
        }
    }
};
```

### Terrain Flattening (Optional)

For cities that need flat ground:

```cpp
void flattenTerrainForCity(TerrainSystem& terrain, const CityData& city) {
    // Create height modification mask
    // Blend city area to average height
    // Smooth edges to avoid harsh transitions
    // Update terrain heightmap
}
```

---

## Phase 6: LOD System

### Goal
Render cities efficiently at varying distances.

### LOD Levels

| Distance | Representation | Vertex Count |
|----------|---------------|--------------|
| > 2000m | Billboard/impostor | 4 |
| 1000-2000m | Ward blocks (solid colors) | ~100 |
| 500-1000m | Building boxes (no detail) | ~2000 |
| 200-500m | Simple buildings (flat roofs) | ~10000 |
| 50-200m | Detailed buildings (roofs, doors) | ~50000 |
| < 50m | Full detail (windows, decorations) | ~200000 |

### Implementation

```cpp
class CityLODManager {
public:
    struct LODLevel {
        float maxDistance;
        std::vector<Renderable> renderables;
        bool loaded = false;
    };

    void update(const glm::vec3& cameraPos) {
        float distance = glm::distance(cameraPos, cityCenter);

        int targetLevel = calculateLODLevel(distance);

        // Async load higher detail if approaching
        if (targetLevel > currentLevel) {
            loadLevelAsync(targetLevel);
        }

        // Unload distant detail
        if (targetLevel < currentLevel - 1) {
            unloadLevel(currentLevel);
        }
    }

private:
    std::array<LODLevel, 6> lodLevels;
    int currentLevel = 0;
};
```

### Mesh Merging for Performance

```cpp
// Merge all buildings in a ward into single draw call
Mesh mergeWardBuildings(const std::vector<BuildingMesh>& buildings) {
    MeshBuilder builder;
    for (const auto& building : buildings) {
        builder.append(building.mesh, building.transform);
    }
    return builder.build();
}
```

---

## Phase 7: Material System

### Goal
Create appropriate materials for medieval city elements.

### Material Categories

```cpp
enum class CityMaterial {
    // Walls
    STONE_WALL,
    TIMBER_FRAME,
    PLASTER_WHITE,
    PLASTER_COLORED,
    BRICK,

    // Roofs
    THATCH,
    CLAY_TILES,
    SLATE,
    WOODEN_SHINGLES,

    // Ground
    COBBLESTONE,
    DIRT_ROAD,
    GRASS_YARD,

    // Details
    WOODEN_DOOR,
    IRON_DOOR,
    GLASS_WINDOW,
    SHUTTERS,
    SIGN_WOOD
};
```

### Ward-Based Material Selection

```cpp
MaterialSet selectMaterials(WardType ward, int buildingClass) {
    switch (ward) {
        case WardType::Castle:
            return { STONE_WALL, SLATE, IRON_DOOR };
        case WardType::Cathedral:
            return { STONE_WALL, SLATE, WOODEN_DOOR };
        case WardType::Market:
            return { TIMBER_FRAME, CLAY_TILES, WOODEN_DOOR };
        case WardType::Alleys:
            return { PLASTER_WHITE, THATCH, WOODEN_DOOR };
        case WardType::Harbour:
            return { TIMBER_FRAME, WOODEN_SHINGLES, WOODEN_DOOR };
        default:
            return { PLASTER_WHITE, THATCH, WOODEN_DOOR };
    }
}
```

### Texture Sources

Per CLAUDE.md, use opengameart.org for placeholder textures:
- Wall textures: Stone, timber, plaster variations
- Roof textures: Thatch, tiles, slate
- Ground textures: Cobblestone, dirt
- Detail textures: Door, window, sign atlases

---

## Phase 8: Runtime Integration

### Goal
Integrate city rendering into the engine's render pipeline.

### New System: CitySystem

```cpp
class CitySystem {
public:
    void init(InitContext& ctx, const CitySystemInitInfo& info);
    void loadCity(const std::string& cityPath);

    void update(const FrameData& frameData);
    void recordCommands(vk::CommandBuffer cmd, const RenderContext& ctx);

    // Terrain integration
    void setTerrainSystem(TerrainSystem* terrain);

private:
    std::unique_ptr<CityLoader> loader;
    std::unique_ptr<BuildingGenerator> buildingGen;
    std::unique_ptr<CityLODManager> lodManager;

    std::vector<Renderable> cityRenderables;
    MaterialRegistry cityMaterials;
};
```

### Integration Points

1. **RendererSystems**: Add CitySystem to geometry tier
2. **HDRStage Slot 1**: Render city alongside scene objects
3. **ShadowStage**: City casts shadows (merged meshes for performance)
4. **SceneManager**: Track city objects for collision/interaction

---

## Phase 9: Build Pipeline Integration

### Goal
Generate city data at build time.

### CMake Integration

```cmake
# tools/town_generator/CMakeLists.txt
add_executable(town_generator ...)

# Generate city data at build time
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/generated/city/city_buildings.geojson
    COMMAND town_generator
        --seed ${CITY_SEED}
        --output-dir ${CMAKE_BINARY_DIR}/generated/city
        --format geojson
        --level 5
    DEPENDS town_generator
    COMMENT "Generating city data"
)

add_custom_target(generate_city
    DEPENDS ${CMAKE_BINARY_DIR}/generated/city/city_buildings.geojson
)
```

### Generated File Handling

```cpp
// At runtime, load pre-generated city data
CitySystem::loadCity("generated/city/");
```

---

## Implementation Order

### Milestone 1: Data Export
1. Add GeoJSONWriter to town_generator
2. Export building footprints with frontage edges
3. Export streets, walls, water features
4. Export ward boundaries

**Test**: Visualize exported GeoJSON in QGIS or geojson.io

### Milestone 2: Basic 3D Buildings
1. Create CityLoader to parse GeoJSON
2. Create BuildingGenerator with simple extrusion
3. Generate flat-roofed box buildings
4. Render in engine with single material

**Test**: See white box buildings in correct positions

### Milestone 3: Architectural Detail
1. Add door placement algorithm
2. Add window placement algorithm
3. Add procedural roof generation
4. Apply ward-based materials

**Test**: Buildings have doors facing streets, varied roofs

### Milestone 4: Infrastructure
1. Generate street meshes
2. Generate wall meshes with towers
3. Generate bridge meshes
4. Add ground materials per ward

**Test**: Complete city with streets, walls, varied ground

### Milestone 5: Terrain Integration
1. Height sampling for building placement
2. Street path height following
3. Foundation generation for sloped terrain
4. Terrain flattening (optional)

**Test**: City sits naturally on varied terrain

### Milestone 6: LOD and Performance
1. Implement LOD levels
2. Add mesh merging per ward
3. Add async LOD streaming
4. Optimize draw calls

**Test**: Maintain 60fps approaching large city

### Milestone 7: Polish
1. Add chimneys, signs, decorations
2. Add lighting (lanterns, torches)
3. Add ambient population hints (laundry, carts)
4. Add interior volumes (for future)

---

## Testing Approach

### Visual Testing
- Load city in engine, fly around to inspect
- Check door placement faces streets
- Check roof variety across wards
- Check material assignment by ward
- Check terrain conformance

### Performance Testing
- Profile draw calls at various distances
- Measure vertex counts per LOD level
- Test LOD transitions for popping
- Verify async loading doesn't stall

### Validation Testing
- Compare GeoJSON export to SVG output
- Verify all buildings have front doors
- Verify no floating buildings
- Verify no building intersections

---

## Open Questions

1. **Interior spaces**: Should buildings have interior geometry for future walkthroughs?
2. **Population**: Add NPC spawn points based on building type?
3. **Destruction**: Support partial building damage/destruction?
4. **Seasonal**: Different appearances (snow-covered roofs, etc.)?
5. **Growth**: Dynamic city expansion over time?

---

## Dependencies

- **Existing**: TerrainSystem (height queries), SceneManager, MaterialRegistry
- **New textures**: Wall, roof, ground, detail textures from opengameart.org
- **Libraries**: nlohmann/json (already in project for GeoJSON)

---

## File Structure Summary

```
tools/town_generator/
├── src/export/
│   └── GeoJSONWriter.cpp     # New: GeoJSON export

src/city/
├── CitySystem.h/cpp          # Main system
├── CityLoader.h/cpp          # GeoJSON loading
├── BuildingGenerator.h/cpp   # 2D → 3D
├── RoofGenerator.h/cpp       # Roof meshes
├── WallGenerator.h/cpp       # Wall meshes
├── DoorPlacer.h/cpp          # Door algorithm
├── WindowPlacer.h/cpp        # Window algorithm
├── StreetGenerator.h/cpp     # Street meshes
├── CityWallGenerator.h/cpp   # Fortification meshes
├── CityLODManager.h/cpp      # LOD management
└── CityMaterialSet.h/cpp     # Material assignment

generated/city/
├── city_metadata.json
├── city_buildings.geojson
├── city_streets.geojson
├── city_walls.geojson
├── city_water.geojson
└── city_wards.geojson
```
