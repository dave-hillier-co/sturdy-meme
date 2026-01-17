#include "DebugControlSubsystem.h"
#include "DebugLineSystem.h"
#include "HiZSystem.h"
#include "RendererSystems.h"
#include "RoadRiverVisualization.h"

#ifdef JPH_DEBUG_RENDERER
#include "PhysicsDebugRenderer.h"
#endif

#ifdef JPH_DEBUG_RENDERER
PhysicsDebugRenderer* DebugControlSubsystem::getPhysicsDebugRenderer() {
    return systems_.physicsDebugRenderer();
}

const PhysicsDebugRenderer* DebugControlSubsystem::getPhysicsDebugRenderer() const {
    return systems_.physicsDebugRenderer();
}
#endif

DebugLineSystem& DebugControlSubsystem::getDebugLineSystem() {
    return debugLine_;
}

const DebugLineSystem& DebugControlSubsystem::getDebugLineSystem() const {
    return debugLine_;
}

void DebugControlSubsystem::setHiZCullingEnabled(bool enabled) {
    hiZ_.setHiZEnabled(enabled);
}

bool DebugControlSubsystem::isHiZCullingEnabled() const {
    return hiZ_.isHiZEnabled();
}

IDebugControl::CullingStats DebugControlSubsystem::getHiZCullingStats() const {
    auto stats = hiZ_.getStats();
    return CullingStats{stats.totalObjects, stats.visibleObjects, stats.frustumCulled, stats.occlusionCulled};
}

void DebugControlSubsystem::updateRoadRiverVisualization() {
    if (!roadRiverVisEnabled_) {
        // Clear persistent lines when disabled
        if (debugLine_.getPersistentLineCount() > 0) {
            debugLine_.clearPersistentLines();
        }
        return;
    }

    // Sync individual road/river toggles to visualization config
    auto& config = systems_.roadRiverVis().getConfig();
    bool needsRebuild = false;

    if (config.showRoads != showRoads_) {
        config.showRoads = showRoads_;
        needsRebuild = true;
    }
    if (config.showRivers != showRivers_) {
        config.showRivers = showRivers_;
        needsRebuild = true;
    }

    if (needsRebuild) {
        systems_.roadRiverVis().invalidateCache();
        debugLine_.clearPersistentLines();
    }

    // Add road/river visualization to debug lines
    systems_.roadRiverVis().addToDebugLines(debugLine_);
}
