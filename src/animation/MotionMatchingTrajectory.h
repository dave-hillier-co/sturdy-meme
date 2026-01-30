#pragma once

#include "MotionMatchingFeature.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <deque>
#include <vector>

namespace MotionMatching {

// Stores historical trajectory data
struct TrajectoryHistory {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    glm::vec3 facing{0.0f, 0.0f, 1.0f};
    float timestamp = 0.0f;
};

// Predicts future trajectory based on player input and current state
class TrajectoryPredictor {
public:
    TrajectoryPredictor() = default;

    // Configuration
    struct Config {
        // Sample times for trajectory (negative = past, positive = future)
        std::vector<float> sampleTimes = {-0.3f, -0.2f, -0.1f, 0.1f, 0.2f, 0.4f, 0.6f, 1.0f};

        // Movement parameters
        float maxSpeed = 4.0f;           // Maximum movement speed (m/s)
        float acceleration = 10.0f;      // How fast character accelerates
        float deceleration = 15.0f;      // How fast character decelerates
        float turnSpeed = 360.0f;        // Degrees per second for turning

        // History parameters
        float historyDuration = 1.0f;    // How long to keep history (seconds)

        // Responsiveness
        float inputSmoothing = 0.1f;     // Time constant for input smoothing
    };

    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    // Update with current state (call every frame)
    // position: current world position
    // facing: current facing direction (normalized, Y=0)
    // inputDirection: desired movement direction from input (normalized, Y=0)
    // inputMagnitude: 0-1 how much movement is desired
    // deltaTime: frame time
    void update(const glm::vec3& position,
                const glm::vec3& facing,
                const glm::vec3& inputDirection,
                float inputMagnitude,
                float deltaTime);

    // Generate trajectory for matching
    // Returns trajectory with samples at configured time offsets
    Trajectory generateTrajectory() const;

    // Get current velocity
    glm::vec3 getCurrentVelocity() const { return currentVelocity_; }

    // Get current facing direction
    glm::vec3 getCurrentFacing() const { return currentFacing_; }

    // Get smoothed input direction
    glm::vec3 getSmoothedInput() const { return smoothedInput_; }

    // Reset state (call when teleporting character)
    void reset();

private:
    Config config_;

    // Current state
    glm::vec3 currentPosition_{0.0f};
    glm::vec3 currentVelocity_{0.0f};
    glm::vec3 currentFacing_{0.0f, 0.0f, 1.0f};
    glm::vec3 smoothedInput_{0.0f};
    float currentTime_ = 0.0f;

    // History for past trajectory
    std::deque<TrajectoryHistory> history_;

    // Predict future position/velocity/facing at a given time offset
    TrajectorySample predictFuture(float timeOffset) const;

    // Get historical sample at a given time offset (negative)
    TrajectorySample getHistorySample(float timeOffset) const;

    // Prune old history entries
    void pruneHistory();
};

// Inertial blending for smooth transitions between poses
// Based on "Inertialization" technique for animation
class InertialBlender {
public:
    InertialBlender() = default;

    // Configuration
    struct Config {
        float blendDuration = 0.3f;       // How long to blend over
        float dampingRatio = 1.0f;        // Critically damped by default
        float naturalFrequency = 10.0f;   // Higher = faster convergence
    };

    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    // Start a new blend from current state to target
    // Call when switching to a new animation pose
    void startBlend(const glm::vec3& currentPosition,
                    const glm::vec3& currentVelocity,
                    const glm::vec3& targetPosition,
                    const glm::vec3& targetVelocity);

    // Update blend state
    void update(float deltaTime);

    // Get blended position/velocity offsets
    // Add these to the target animation position to get final position
    glm::vec3 getPositionOffset() const { return positionOffset_; }
    glm::vec3 getVelocityOffset() const { return velocityOffset_; }

    // Check if blend is active
    bool isBlending() const { return blendTime_ < config_.blendDuration; }

    // Get blend progress (0-1)
    float getProgress() const {
        return config_.blendDuration > 0.0f ?
               std::min(1.0f, blendTime_ / config_.blendDuration) : 1.0f;
    }

    // Reset blend state
    void reset();

private:
    Config config_;

    // Blend state
    glm::vec3 positionOffset_{0.0f};
    glm::vec3 velocityOffset_{0.0f};
    float blendTime_ = 0.0f;

    // Spring-damper state for smooth decay
    glm::vec3 springPosition_{0.0f};
    glm::vec3 springVelocity_{0.0f};
};

// Root motion handler for extracting and applying root movement
class RootMotionExtractor {
public:
    RootMotionExtractor() = default;

    // Configuration
    struct Config {
        bool extractTranslation = true;   // Extract horizontal translation
        bool extractRotation = true;      // Extract rotation around Y axis
        bool applyTranslation = true;     // Apply translation to character
        bool applyRotation = true;        // Apply rotation to character
        float translationScale = 1.0f;    // Scale applied to translation
        float rotationScale = 1.0f;       // Scale applied to rotation
    };

    void setConfig(const Config& config) { config_ = config; }
    const Config& getConfig() const { return config_; }

    // Update with new root pose from animation
    // Returns the delta translation and rotation to apply to character
    void update(const glm::vec3& rootPosition,
                const glm::quat& rootRotation,
                float deltaTime);

    // Get delta motion since last frame
    glm::vec3 getDeltaTranslation() const { return deltaTranslation_; }
    float getDeltaRotation() const { return deltaRotation_; }

    // Reset state (call when changing animations)
    void reset();

    // Set current pose as reference (no delta on first frame)
    void setReference(const glm::vec3& rootPosition, const glm::quat& rootRotation);

private:
    Config config_;

    // Previous frame state
    glm::vec3 prevRootPosition_{0.0f};
    glm::quat prevRootRotation_{1.0f, 0.0f, 0.0f, 0.0f};
    bool hasReference_ = false;

    // Current frame delta
    glm::vec3 deltaTranslation_{0.0f};
    float deltaRotation_ = 0.0f;
};

} // namespace MotionMatching
