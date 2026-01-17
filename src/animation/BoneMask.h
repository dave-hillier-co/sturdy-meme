#pragma once

#include "../core/NodeMask.h"
#include "GLTFLoader.h"
#include <vector>
#include <string>
#include <unordered_set>

// Bone mask defines per-bone weights for animation layer blending.
// Weight of 1.0 = fully affected by layer, 0.0 = not affected.
// Composes a generic NodeMask with skeleton-specific factory methods.
class BoneMask {
public:
    BoneMask() = default;

    // Create a mask for a given skeleton size (all weights = 1.0 by default)
    explicit BoneMask(size_t boneCount, float defaultWeight = 1.0f);

    // Create from an existing NodeMask
    explicit BoneMask(NodeMask mask);

    // Create from skeleton with specific bones enabled
    static BoneMask fromBoneNames(const Skeleton& skeleton,
                                  const std::vector<std::string>& boneNames,
                                  bool includeChildren = true);

    // Preset masks for common body parts
    static BoneMask upperBody(const Skeleton& skeleton);  // Spine, arms, head
    static BoneMask lowerBody(const Skeleton& skeleton);  // Hips, legs
    static BoneMask leftArm(const Skeleton& skeleton);
    static BoneMask rightArm(const Skeleton& skeleton);
    static BoneMask leftLeg(const Skeleton& skeleton);
    static BoneMask rightLeg(const Skeleton& skeleton);
    static BoneMask spine(const Skeleton& skeleton);
    static BoneMask head(const Skeleton& skeleton);

    // Access weights (delegates to internal NodeMask)
    float getWeight(size_t boneIndex) const { return mask_.getWeight(boneIndex); }
    void setWeight(size_t boneIndex, float weight) { mask_.setWeight(boneIndex, weight); }

    // Set weight for a bone and optionally its children
    void setWeightByName(const Skeleton& skeleton, const std::string& boneName,
                         float weight, bool includeChildren = true);

    // Get all weights (for use with blendMasked)
    const std::vector<float>& getWeights() const { return mask_.getWeights(); }
    std::vector<float>& getWeights() { return mask_.getWeights(); }

    // Resize the mask
    void resize(size_t count, float defaultWeight = 1.0f) { mask_.resize(count, defaultWeight); }

    // Size
    size_t size() const { return mask_.size(); }

    // Invert the mask (1 - weight for each bone)
    BoneMask inverted() const { return BoneMask(mask_.inverted()); }

    // Multiply all weights by a factor
    void scale(float factor) { mask_.scale(factor); }

    // Combine masks (multiply weights)
    BoneMask operator*(const BoneMask& other) const { return BoneMask(mask_ * other.mask_); }

    // Combine masks (add weights, clamped to [0,1])
    BoneMask operator+(const BoneMask& other) const { return BoneMask(mask_ + other.mask_); }

    // Access the underlying NodeMask
    const NodeMask& getNodeMask() const { return mask_; }
    NodeMask& getNodeMask() { return mask_; }

private:
    NodeMask mask_;

    // Helper to find bones by name pattern
    static void collectBonesByPattern(const Skeleton& skeleton,
                                      const std::vector<std::string>& patterns,
                                      std::unordered_set<int32_t>& outBoneIndices);

    // Helper to add children of specified bones
    static void addChildBones(const Skeleton& skeleton,
                              std::unordered_set<int32_t>& boneIndices);
};

// Predefined body part patterns for common skeleton naming conventions
namespace BoneMaskPatterns {
    // Upper body bone name patterns (case-insensitive matching)
    extern const std::vector<std::string> UPPER_BODY_ROOTS;
    extern const std::vector<std::string> LEFT_ARM_ROOTS;
    extern const std::vector<std::string> RIGHT_ARM_ROOTS;
    extern const std::vector<std::string> HEAD_ROOTS;
    extern const std::vector<std::string> SPINE_ROOTS;

    // Lower body bone name patterns
    extern const std::vector<std::string> LOWER_BODY_ROOTS;
    extern const std::vector<std::string> LEFT_LEG_ROOTS;
    extern const std::vector<std::string> RIGHT_LEG_ROOTS;
}
