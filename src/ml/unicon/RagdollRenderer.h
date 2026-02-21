#pragma once

#include "StateEncoder.h"
#include "../../physics/ArticulatedBody.h"
#include "../../animation/SkinnedMeshRenderer.h"
#include "../../loaders/GLTFLoader.h"

#include <glm/glm.hpp>
#include <vector>

class DebugLineSystem;

namespace ml::unicon {

// Renders ArticulatedBody ragdolls as skinned characters by computing bone
// matrices from physics state and uploading them to the SkinnedMeshRenderer.
//
// Shares the player character's mesh and skeleton data - does NOT create
// duplicate GPU mesh resources. Each ragdoll just needs a bone matrix slot.
//
// Also provides debug visualization of ragdoll body parts and target poses.
class RagdollRenderer {
public:
    // Configure with the reference skeleton used by the ragdoll bodies.
    void configure(const Skeleton& referenceSkeleton);

    // Update bone matrices for all ragdolls.
    size_t updateBoneMatrices(const std::vector<ArticulatedBody>& ragdolls,
                              const PhysicsWorld& physics,
                              SkinnedMeshRenderer& skinnedRenderer,
                              uint32_t frameIndex,
                              uint32_t firstSlot = 32);

    // Record draw commands for all rendered ragdolls.
    void recordDrawCommands(VkCommandBuffer cmd,
                            uint32_t frameIndex,
                            AnimatedCharacter& templateCharacter,
                            SkinnedMeshRenderer& skinnedRenderer,
                            uint32_t firstSlot = 32);

    // Draw wireframe debug overlay for ragdolls (body capsules + target ghost).
    // Call during the debug line collection phase.
    void drawDebugOverlay(const std::vector<ArticulatedBody>& ragdolls,
                          const PhysicsWorld& physics,
                          DebugLineSystem& debugLines,
                          const std::vector<TargetFrame>* targetFrames = nullptr);

    void setDebugEnabled(bool enabled) { debugEnabled_ = true; }
    bool isDebugEnabled() const { return debugEnabled_; }

    bool isConfigured() const { return configured_; }
    size_t getRenderedCount() const { return renderedCount_; }

private:
    Skeleton skeleton_;
    bool configured_ = false;
    bool debugEnabled_ = false;
    size_t renderedCount_ = 0;

    std::vector<glm::mat4> ragdollTransforms_;
};

} // namespace ml::unicon
