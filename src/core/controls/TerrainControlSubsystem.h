#pragma once

#include "interfaces/ITerrainControl.h"

class TerrainSystem;

/**
 * TerrainControlSubsystem - Implements ITerrainControl
 * Wraps TerrainSystem for terrain rendering control.
 */
class TerrainControlSubsystem : public ITerrainControl {
public:
    explicit TerrainControlSubsystem(TerrainSystem& terrain)
        : terrain_(terrain) {}

    void setTerrainEnabled(bool enabled) override { terrainEnabled_ = enabled; }
    bool isTerrainEnabled() const override { return terrainEnabled_; }
    void toggleTerrainWireframe() override;
    bool isTerrainWireframeMode() const override;
    uint32_t getTerrainNodeCount() const override;
    float getTerrainHeightAt(float x, float z) const override;

    TerrainSystem& getTerrainSystem() override;
    const TerrainSystem& getTerrainSystem() const override;

    // Access to local state for Renderer
    bool& terrainEnabled() { return terrainEnabled_; }

private:
    TerrainSystem& terrain_;
    bool terrainEnabled_ = true;
};
