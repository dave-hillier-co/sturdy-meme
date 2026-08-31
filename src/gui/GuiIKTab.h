#pragma once

#include <glm/glm.hpp>

class ISceneControl;
class Camera;

// IK debug settings for GUI control
struct IKDebugSettings {
    bool showSkeleton = false;
    bool showIKTargets = false;
    bool showFootPlacement = false;

    // IK feature enables
    bool lookAtEnabled = false;
    bool footPlacementEnabled = true;
    bool straddleEnabled = false;

    // Look-at target mode
    enum class LookAtMode { Fixed, Camera, Mouse };
    LookAtMode lookAtMode = LookAtMode::Camera;
    glm::vec3 fixedLookAtTarget = glm::vec3(0, 1.5f, 5.0f);

    // Foot placement
    float groundOffset = 0.0f;
};

/**
 * IK / animation panel. Owns the IK debug settings; ISceneControl is bound
 * at construction.
 */
class GuiIKTab {
public:
    explicit GuiIKTab(ISceneControl& sceneControl) : sceneControl_(sceneControl) {}

    void draw(const Camera& camera) { render(sceneControl_, camera, settings_); }

    void drawSkeletonOverlay(const Camera& camera, bool showCapeColliders) {
        renderSkeletonOverlay(sceneControl_, camera, settings_, showCapeColliders);
    }

    const IKDebugSettings& settings() const { return settings_; }

private:
    static void render(ISceneControl& sceneControl, const Camera& camera, IKDebugSettings& settings);
    static void renderSkeletonOverlay(ISceneControl& sceneControl, const Camera& camera,
                                      const IKDebugSettings& settings, bool showCapeColliders);

    ISceneControl& sceneControl_;
    IKDebugSettings settings_;
};
