#pragma once

#include "GuiPanelRegistry.h"

class TerrainSystem;
class PhysicsTerrainTileManager;
class Camera;

/**
 * Terrain tile loader debug panel. Owns its view-mode state; dependencies
 * are bound at construction. The physics tile manager may be null when
 * physics terrain streaming is disabled.
 */
class GuiTileLoaderTab {
public:
    // Tile loader visualization mode
    enum class TileViewMode {
        GPU,      // Active GPU tiles (loaded with GPU resources)
        CPU,      // All tiles with CPU data (includes GPU + CPU-only + base LOD)
        Physics   // Physics collision tiles
    };

    struct State {
        TileViewMode viewMode = TileViewMode::GPU;
    };

    GuiTileLoaderTab(TerrainSystem& terrain, PhysicsTerrainTileManager* physicsTerrainTiles)
        : terrain_(terrain), physicsTerrainTiles_(physicsTerrainTiles) {}

    void draw(const GuiFrameContext& ctx) {
        render(terrain_, physicsTerrainTiles_, ctx.camera, state_);
    }

private:
    static void render(TerrainSystem& terrain, PhysicsTerrainTileManager* physicsTerrainTiles,
                       const Camera& camera, State& state);

    TerrainSystem& terrain_;
    PhysicsTerrainTileManager* physicsTerrainTiles_ = nullptr;  // Nullable: streaming may be off
    State state_;
};
