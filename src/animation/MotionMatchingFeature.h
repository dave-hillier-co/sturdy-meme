#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <array>

// Forward declarations
struct Skeleton;
struct AnimationClip;
struct SkeletonPose;

namespace MotionMatching {

// Maximum number of trajectory samples for prediction
constexpr size_t MAX_TRAJECTORY_SAMPLES = 8;

// Maximum number of bones to track for pose features
constexpr size_t MAX_FEATURE_BONES = 8;

// Forward declaration
struct FeatureNormalization;

// Default feature bones commonly used in locomotion
namespace FeatureBones {
    constexpr const char* LEFT_FOOT = "LeftFoot";
    constexpr const char* RIGHT_FOOT = "RightFoot";
    constexpr const char* LEFT_HAND = "LeftHand";
    constexpr const char* RIGHT_HAND = "RightHand";
    constexpr const char* HIPS = "Hips";
    constexpr const char* SPINE = "Spine";
}

// A single trajectory sample point
struct TrajectorySample {
    glm::vec3 position{0.0f};    // Position relative to character (local space)
    glm::vec3 velocity{0.0f};    // Velocity at this point
    glm::vec3 facing{0.0f, 0.0f, 1.0f}; // Facing direction
    float timeOffset = 0.0f;      // Time offset from current (negative = past, positive = future)
};

// Trajectory containing past and future movement prediction
struct Trajectory {
    std::array<TrajectorySample, MAX_TRAJECTORY_SAMPLES> samples;
    size_t sampleCount = 0;

    void clear() { sampleCount = 0; }

    void addSample(const TrajectorySample& sample) {
        if (sampleCount < MAX_TRAJECTORY_SAMPLES) {
            samples[sampleCount++] = sample;
        }
    }

    // Compute cost between two trajectories
    float computeCost(const Trajectory& other,
                      float positionWeight = 1.0f,
                      float velocityWeight = 0.5f,
                      float facingWeight = 0.3f) const;

    // Compute normalized cost between two trajectories
    float computeNormalizedCost(const Trajectory& other,
                                const FeatureNormalization& norm,
                                float positionWeight = 1.0f,
                                float velocityWeight = 0.5f,
                                float facingWeight = 0.3f) const;
};

// Feature for a single bone (position + velocity in character space)
struct BoneFeature {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};

    float computeCost(const BoneFeature& other,
                      float positionWeight = 1.0f,
                      float velocityWeight = 0.5f) const {
        float posCost = glm::length(position - other.position) * positionWeight;
        float velCost = glm::length(velocity - other.velocity) * velocityWeight;
        return posCost + velCost;
    }
};

// Complete pose features for matching
struct PoseFeatures {
    // Bone features (position + velocity for key bones)
    std::array<BoneFeature, MAX_FEATURE_BONES> boneFeatures;
    size_t boneCount = 0;

    // Root velocity (horizontal movement)
    glm::vec3 rootVelocity{0.0f};

    // Root angular velocity (turning rate)
    float rootAngularVelocity = 0.0f;

    // Foot phase information (0-1 cycle)
    float leftFootPhase = 0.0f;
    float rightFootPhase = 0.0f;

    // Compute cost between two pose features
    float computeCost(const PoseFeatures& other,
                      float boneWeight = 1.0f,
                      float rootVelWeight = 0.5f,
                      float angularVelWeight = 0.3f,
                      float phaseWeight = 0.2f) const;

    // Compute normalized cost between two pose features
    float computeNormalizedCost(const PoseFeatures& other,
                                const FeatureNormalization& norm,
                                float boneWeight = 1.0f,
                                float rootVelWeight = 0.5f,
                                float angularVelWeight = 0.3f,
                                float phaseWeight = 0.2f) const;
};

// Normalization statistics for a single feature dimension
struct FeatureStats {
    float mean = 0.0f;
    float stdDev = 1.0f;  // Default to 1 to avoid division by zero

    // Normalize a value using these statistics
    float normalize(float value) const {
        return (value - mean) / stdDev;
    }
};

// Normalization data for all features
struct FeatureNormalization {
    // Trajectory normalization (per sample point)
    std::array<FeatureStats, MAX_TRAJECTORY_SAMPLES> trajectoryPosition;  // magnitude
    std::array<FeatureStats, MAX_TRAJECTORY_SAMPLES> trajectoryVelocity;  // magnitude

    // Bone feature normalization (per bone)
    std::array<FeatureStats, MAX_FEATURE_BONES> bonePosition;    // magnitude
    std::array<FeatureStats, MAX_FEATURE_BONES> boneVelocity;    // magnitude

    // Root motion normalization
    FeatureStats rootVelocity;       // magnitude
    FeatureStats rootAngularVelocity;

    bool isComputed = false;
};

// Configuration for feature extraction
struct FeatureConfig {
    // Bones to extract features from (by name)
    std::vector<std::string> featureBoneNames;

    // Weights for cost computation
    // Trajectory is weighted higher for locomotion type selection (idle/walk/run)
    // Pose is more important for continuity within the same locomotion type
    float trajectoryWeight = 2.0f;
    float poseWeight = 1.0f;
    float bonePositionWeight = 1.0f;
    float boneVelocityWeight = 0.5f;
    float trajectoryPositionWeight = 1.0f;
    float trajectoryVelocityWeight = 0.5f;
    float trajectoryFacingWeight = 0.3f;
    float rootVelocityWeight = 0.5f;
    float angularVelocityWeight = 0.3f;
    float phaseWeight = 0.2f;

    // Trajectory sample times (relative to current time)
    std::vector<float> trajectorySampleTimes = {-0.2f, -0.1f, 0.1f, 0.2f, 0.4f, 0.6f};

    // Default locomotion configuration
    static FeatureConfig locomotion() {
        FeatureConfig config;
        config.featureBoneNames = {
            FeatureBones::LEFT_FOOT,
            FeatureBones::RIGHT_FOOT,
            FeatureBones::HIPS
        };
        return config;
    }

    // Full body configuration
    static FeatureConfig fullBody() {
        FeatureConfig config;
        config.featureBoneNames = {
            FeatureBones::LEFT_FOOT,
            FeatureBones::RIGHT_FOOT,
            FeatureBones::LEFT_HAND,
            FeatureBones::RIGHT_HAND,
            FeatureBones::HIPS,
            FeatureBones::SPINE
        };
        return config;
    }
};

// Feature extractor - extracts features from animation poses
class FeatureExtractor {
public:
    FeatureExtractor() = default;

    // Initialize with skeleton and configuration
    void initialize(const Skeleton& skeleton, const FeatureConfig& config);

    // Extract features from a pose at a specific time
    PoseFeatures extractFromPose(const Skeleton& skeleton,
                                  const SkeletonPose& pose,
                                  const SkeletonPose& prevPose,
                                  float deltaTime) const;

    // Extract features from an animation clip at a specific time
    PoseFeatures extractFromClip(const AnimationClip& clip,
                                  const Skeleton& skeleton,
                                  float time,
                                  float deltaTime = 1.0f / 60.0f) const;

    // Extract trajectory from an animation clip
    Trajectory extractTrajectoryFromClip(const AnimationClip& clip,
                                          const Skeleton& skeleton,
                                          float currentTime) const;

    bool isInitialized() const { return initialized_; }
    const FeatureConfig& getConfig() const { return config_; }

private:
    FeatureConfig config_;
    std::vector<int32_t> featureBoneIndices_;
    int32_t rootBoneIndex_ = -1;
    bool initialized_ = false;

    // Compute bone position in character space
    glm::vec3 computeBonePosition(const Skeleton& skeleton,
                                   const SkeletonPose& pose,
                                   int32_t boneIndex) const;

    // Compute root transform from pose
    glm::mat4 computeRootTransform(const Skeleton& skeleton,
                                    const SkeletonPose& pose) const;
};

} // namespace MotionMatching
