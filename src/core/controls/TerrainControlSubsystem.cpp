#include "TerrainControlSubsystem.h"
#include "TerrainSystem.h"

void TerrainControlSubsystem::toggleTerrainWireframe() {
    terrain_.setWireframeMode(!terrain_.isWireframeMode());
}

bool TerrainControlSubsystem::isTerrainWireframeMode() const {
    return terrain_.isWireframeMode();
}

uint32_t TerrainControlSubsystem::getTerrainNodeCount() const {
    return terrain_.getNodeCount();
}

float TerrainControlSubsystem::getTerrainHeightAt(float x, float z) const {
    return terrain_.getHeightAt(x, z);
}

TerrainSystem& TerrainControlSubsystem::getTerrainSystem() {
    return terrain_;
}

const TerrainSystem& TerrainControlSubsystem::getTerrainSystem() const {
    return terrain_;
}
