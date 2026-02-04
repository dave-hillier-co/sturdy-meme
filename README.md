# Vulkan Game

A simple Vulkan-based 3D game rendering a textured cube with camera controls.

## Prerequisites

- CMake 3.20+
- C++17 compiler
- Vulkan SDK (includes MoltenVK on macOS)
- vcpkg
- Ninja

### macOS Setup

1. Install MoltenVK (required for Vulkan on macOS):
   ```bash
   brew install molten-vk
   ```

   Or install the full [Vulkan SDK](https://vulkan.lunarg.com/sdk/home)

2. Install vcpkg:
   ```bash
   git clone https://github.com/microsoft/vcpkg.git
   ./vcpkg/bootstrap-vcpkg.sh
   export VCPKG_ROOT=$(pwd)/vcpkg
   ```

## Building

### Debug Build

```bash
cmake --preset debug
cmake --build build/debug
```

### Release Build

```bash
cmake --preset release
cmake --build build/release
```

### Claude Build (self-bootstrapping)

For environments without vcpkg pre-installed:

```bash
./build-claude.sh
```

This script:
1. Clones and builds vcpkg from source if not present
2. Uses a custom triplet optimized for restricted environments
3. Configures and builds the project

Requirements:
- CMake 3.20+, Ninja, GCC/Clang
- Network access to GitHub (for vcpkg and dependencies)
- Network access to freedesktop.org (for dbus), gnu.org (for some tools)

## Running

```bash
# Debug
open build/debug/vulkan-game.app
# or
./build/debug/vulkan-game.app/Contents/MacOS/vulkan-game

# Release
open build/release/vulkan-game.app
```

## Controls

The game supports two camera modes: **Free Camera** and **Third Person**. Press **Tab** (keyboard) or **Right Stick Click** (gamepad) to toggle between them.

### Keyboard - Global Controls

| Key | Action |
|-----|--------|
| Escape | Quit |
| F1 | Toggle GUI visibility |
| Tab | Toggle camera mode (Free Cam ↔ Third Person) |
| M | Toggle mouse look |
| 1 | Set time to sunrise |
| 2 | Set time to noon |
| 3 | Set time to sunset |
| 4 | Set time to midnight |
| + | Speed up time (2x) |
| - | Slow down time (0.5x) |
| R | Reset to real-time |
| 6 | Toggle cascade debug visualization |
| 7 | Toggle snow depth debug visualization |
| 8 | Toggle Hi-Z occlusion culling |
| 9 | Terrain height diagnostic |
| Z | Decrease weather intensity |
| X | Increase weather intensity |
| C | Cycle weather (Clear → Rain → Snow) |
| F | Spawn confetti at player location |
| V | Toggle cloud style (Procedural ↔ Paraboloid LUT) |
| [ | Decrease fog density |
| ] | Increase fog density |
| \ | Toggle fog on/off |
| , | Decrease snow amount |
| . | Increase snow amount |
| / | Toggle snow (0.0 ↔ 1.0) |
| T | Toggle terrain wireframe |

### Keyboard - Free Camera Mode

| Key | Action |
|-----|--------|
| W | Move forward |
| S | Move backward |
| A | Strafe left |
| D | Strafe right |
| Arrow Up | Look up (pitch) |
| Arrow Down | Look down (pitch) |
| Arrow Left | Look left (yaw) |
| Arrow Right | Look right (yaw) |
| Space | Move up (fly) |
| Left Ctrl / Q | Move down (fly) |
| Left Shift | Sprint (5x speed) |

### Keyboard - Third Person Mode

| Key | Action |
|-----|--------|
| W | Move player forward (relative to camera) |
| S | Move player backward (relative to camera) |
| A | Move player left (relative to camera) |
| D | Move player right (relative to camera) |
| Space | Jump |
| Left Shift | Sprint |
| Arrow Up | Orbit camera up |
| Arrow Down | Orbit camera down |
| Arrow Left | Orbit camera left |
| Arrow Right | Orbit camera right |
| Q | Zoom camera in |
| E | Zoom camera out |
| Caps Lock | Toggle orientation lock |
| Middle Mouse | Hold orientation lock |

### Mouse Controls

| Input | Action |
|-------|--------|
| M key | Toggle mouse look mode |
| Mouse motion | Camera look (when mouse look enabled) |
| Mouse wheel | Camera zoom |

### Gamepad (SDL3) - Global Controls

| Input | Action |
|-------|--------|
| Right Stick Click | Toggle camera mode (Free Cam ↔ Third Person) |
| Left Stick Click | Toggle sprint |
| A / Cross | Set time to sunrise |
| B / Circle | Set time to noon |
| X / Square | Set time to sunset |
| Y / Triangle | Set time to midnight |
| Right Trigger | Speed up time |
| Left Trigger | Slow down time |
| Start | Reset to real-time |
| Back / Select | Quit |

### Gamepad - Free Camera Mode

| Input | Action |
|-------|--------|
| Left Stick | Movement (forward/backward, strafe) |
| Right Stick | Camera look (pitch/yaw) |
| Right Bumper (RB/R1) | Move up |
| Left Bumper (LB/L1) | Move down |

### Gamepad - Third Person Mode

| Input | Action |
|-------|--------|
| Left Stick | Player movement (relative to camera) |
| Right Stick | Orbit camera around player |
| A / Cross | Jump |
| B / Circle | Toggle orientation lock |
| Left Trigger | Hold orientation lock |
| Right Bumper (RB/R1) | Zoom camera out |
| Left Bumper (LB/L1) | Zoom camera in |

Gamepads are automatically detected when connected (hot-plug supported).

## Project Structure

```
vulkan-game/
├── CMakeLists.txt          # Build configuration
├── CMakePresets.json       # Debug/Release presets
├── vcpkg.json              # Dependencies
├── README.md
├── src/
│   ├── main.cpp            # Entry point
│   ├── animation/          # Skeletal animation, motion matching, blend spaces
│   ├── atmosphere/         # Sky, weather, clouds, time of day, snow
│   ├── core/               # Renderer, shaders, buffers, materials, passes
│   ├── culling/            # GPU-based visibility culling
│   ├── debug/              # CPU/GPU profiling, debug visualization
│   ├── ecs/                # Entity-component system
│   ├── gui/                # ImGui debug panels
│   ├── ik/                 # Inverse kinematics (foot placement, look-at)
│   ├── lighting/           # Shadows, froxel lighting, dynamic lights
│   ├── loaders/            # FBX, GLTF, OBJ mesh loading
│   ├── npc/                # NPC rendering and simulation
│   ├── physics/            # Jolt physics, character controller, cloth
│   ├── postprocess/        # Bloom, SSR, god rays, Hi-Z
│   ├── scene/              # Application, camera, input, scene management
│   ├── subdivision/        # Catmull-Clark subdivision surfaces
│   ├── terrain/            # Terrain system, virtual texturing, tile cache
│   ├── vegetation/         # Grass, trees, impostors, LOD systems
│   └── water/              # Ocean FFT, water rendering, flow maps
├── shaders/                # GLSL shaders (~150 files)
│   ├── *.vert/*.frag       # Vertex/fragment shaders
│   ├── *.comp              # Compute shaders
│   ├── *_common.glsl       # Shared shader includes
│   └── terrain/            # Terrain-specific shaders
├── tools/                  # Preprocessing tools (see below)
├── assets/
│   ├── characters/         # Character models and animations
│   ├── materials/          # Material definitions
│   ├── presets/            # Tree and vegetation presets
│   ├── textures/           # Texture assets
│   └── trees/              # Tree model data
└── docs/                   # Architecture and design documentation
```

## Preprocessing Tools

The project includes standalone tools for terrain and content preprocessing. Build all tools with:

```bash
cmake --build build/debug --target terrain_preprocess watershed biome_preprocess settlement_generator road_generator vegetation_generator tile_generator town_generator
```

### Terrain Pipeline Tools

#### terrain_preprocess

Generates tile cache from a 16-bit PNG heightmap.

```bash
./build/debug/tools/terrain_preprocess <heightmap.png> <cache_dir> [options]
  --min-altitude <value>     Altitude for height value 0 (default: 0.0)
  --max-altitude <value>     Altitude for height value 65535 (default: 200.0)
  --meters-per-pixel <value> World scale (default: 1.0)
  --tile-resolution <value>  Output tile resolution (default: 512)
  --lod-levels <value>       Number of LOD levels (default: 4)
```

#### watershed

D8 flow direction analysis and river extraction from heightmaps.

```bash
./build/debug/tools/watershed <heightmap.png> <output_dir> [options]
```

**Outputs:**
- `flow_direction.png` - 8-bit D8 flow direction (values 0-7)
- `flow_accumulation.exr` - Float grid of accumulated water flow
- `watershed_labels.png` - RGBA-encoded uint32 watershed basin IDs
- `rivers.geojson` - River paths with flow/width metadata
- `lakes.geojson` - Lake polygons

#### biome_preprocess

Generates biome classification based on terrain features.

```bash
./build/debug/tools/biome_preprocess <heightmap.png> <watershed_cache> <output_dir> [options]
  --sea-level <value>         Height below which is sea (default: 0.0)
  --terrain-size <value>      World size in meters (default: 16384.0)
  --min-altitude <value>      Min altitude in heightmap (default: 0.0)
  --max-altitude <value>      Max altitude in heightmap (default: 200.0)
  --output-resolution <value> Biome map resolution (default: 1024)
```

**Biome zones:**
| ID | Zone | Description |
|----|------|-------------|
| 0 | Sea | Below sea level |
| 1 | Beach | Low coastal, gentle slope |
| 2 | Chalk Cliff | Steep coastal slopes |
| 3 | Salt Marsh | Low-lying coastal wetland |
| 4 | River | River channels |
| 5 | Wetland | Inland wet areas near rivers |
| 6 | Grassland | Chalk downs, higher elevation |
| 7 | Agricultural | Flat lowland fields |
| 8 | Woodland | Valleys and sheltered slopes |

**Outputs:**
- `biome_map.png` - RGBA8 data (R=zone, G=subzone, B=settlement_distance)
- `biome_debug.png` - Colored visualization

### World Generation Tools

#### settlement_generator

Places settlements based on terrain suitability (flat land, water access, road connections).

```bash
./build/debug/tools/settlement_generator <biome_map.png> <flow_accumulation.exr> <output_dir> [options]
  --num-settlements <value>   Target number of settlements (default: 20)
```

**Outputs:** `settlements.json` - Settlement locations with metadata

#### road_generator

A* pathfinding to connect settlements with terrain-aware roads.

```bash
./build/debug/tools/road_generator <heightmap.png> <settlements.json> <output_dir> [options]
```

**Outputs:** `roads.geojson` - Road network with type and settlement connections

#### vegetation_generator

Poisson disk sampling for tree, rock, and detritus placement based on biome rules.

```bash
./build/debug/tools/vegetation_generator <biome_map.png> <output_dir> [options]
```

**Outputs:** JSON files with vegetation instance positions and parameters

#### town_generator

Medieval Fantasy City Generator - procedural town layout generation (port of MFCG).

```bash
./build/debug/tools/town_generator [options]
```

**Outputs:** SVG town layouts with buildings, streets, walls, and landmarks

#### dwelling_generator

Procedural floor plan generation for individual buildings.

```bash
./build/debug/tools/dwelling_generator [options]
```

**Outputs:** SVG floor plans with room layouts

### Virtual Texturing Tools

#### tile_generator

Generates virtual texture tiles by compositing terrain materials with road/river overlays.

```bash
./build/debug/tools/tile_generator <biome_map.png> <roads.geojson> <rivers.geojson> <output_dir> [options]
```

### Texture Generation Tools

#### foam_noise_gen

Generates procedural foam noise textures for water rendering.

#### caustics_gen

Generates procedural caustics textures for underwater lighting effects.

#### material_texture_gen

Generates procedural placeholder textures for terrain materials.

#### sbsar_render

Renders Substance Archive (.sbsar) files to texture outputs.

### Build Tools

#### shader_reflect

Parses compiled SPIR-V shaders and generates `generated/UBOs.h` with std140-aligned C++ structs matching shader uniform buffer layouts.

#### skinned_mesh_lod

Mesh simplification tool for generating LOD levels of skinned character meshes.

```bash
./build/debug/tools/skinned_mesh_lod <input.gltf> <output_dir> [options]
```

#### terrain_patch_generator

Preview tool for terrain-aware town placement.

## Dependencies

- SDL3 - Windowing and input
- Vulkan - Graphics API
- GLM - Mathematics
- vk-bootstrap - Vulkan initialization
- VMA - Memory allocation
- stb_image - Texture loading

## License

Crate texture from OpenGameArt.org (CC0).
