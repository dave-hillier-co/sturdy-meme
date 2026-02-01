#pragma once

#include <cstdint>
#include <glm/glm.hpp>

class IPlayerControl;
class Camera;

// Player settings for GUI control
struct PlayerSettings {
    // Cape
    bool capeEnabled = false;
    bool showCapeColliders = false;

    // Weapons
    bool showSword = true;        // Show sword in right hand
    bool showShield = true;       // Show shield on left arm

    // Weapons debug
    bool showWeaponAxes = false;  // Show RGB axis indicators on hand bones

    // LOD debug
    bool showLODOverlay = false;  // Show LOD level as screen text
    bool forceLODLevel = false;   // Override automatic LOD selection
    uint32_t forcedLOD = 0;       // Forced LOD level when override is enabled

    // Motion Matching debug
    bool motionMatchingEnabled = false;  // Use motion matching instead of state machine
    bool showMotionMatchingTrajectory = false;  // Show predicted and matched trajectory
    bool showMotionMatchingFeatures = false;   // Show feature bone positions
    bool showMotionMatchingStats = false;      // Show match cost statistics

    // Strafe mode (Unreal-style)
    bool strafeModeEnabled = false;   // Lock orientation to camera direction
    bool thirdPersonCamera = false;   // Third-person camera mode (for strafe testing)
};

namespace GuiPlayerTab {
    void render(IPlayerControl& playerControl, PlayerSettings& settings);

    // Render motion matching debug overlay (trajectory visualization)
    void renderMotionMatchingOverlay(IPlayerControl& playerControl, const Camera& camera,
                                      const PlayerSettings& settings);
}
