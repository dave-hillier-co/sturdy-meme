#include "RagdollRenderer.h"
#include "../../animation/AnimatedCharacter.h"
#include "../../debug/DebugLineSystem.h"

#include <SDL3/SDL_log.h>
#include <algorithm>

namespace ml::unicon {

void RagdollRenderer::configure(const Skeleton& referenceSkeleton) {
    skeleton_ = referenceSkeleton;
    configured_ = true;
    SDL_Log("RagdollRenderer: configured with %zu joints", skeleton_.joints.size());
}

size_t RagdollRenderer::updateBoneMatrices(const std::vector<ArticulatedBody>& ragdolls,
                                            const PhysicsWorld& physics,
                                            SkinnedMeshRenderer& skinnedRenderer,
                                            uint32_t frameIndex,
                                            uint32_t firstSlot) {
    if (!configured_) return 0;

    uint32_t maxSlots = SkinnedMeshRenderer::getMaxSlots();
    renderedCount_ = 0;
    ragdollTransforms_.clear();

    for (size_t i = 0; i < ragdolls.size(); ++i) {
        const auto& ragdoll = ragdolls[i];
        if (!ragdoll.isValid()) continue;

        uint32_t slot = firstSlot + static_cast<uint32_t>(renderedCount_);
        if (slot >= maxSlots) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "RagdollRenderer: out of bone matrix slots (%u/%u)", slot, maxSlots);
            break;
        }

        // Reset skeleton to bind pose
        Skeleton workSkel = skeleton_;

        // Write ragdoll physics state into skeleton joint transforms
        ragdoll.writeToSkeleton(workSkel, physics);

        // Compute global transforms via FK
        std::vector<glm::mat4> globalTransforms;
        workSkel.computeGlobalTransforms(globalTransforms);

        // Compute bone matrices (global * inverseBindMatrix)
        std::vector<glm::mat4> boneMatrices(workSkel.joints.size());
        for (size_t j = 0; j < workSkel.joints.size(); ++j) {
            boneMatrices[j] = globalTransforms[j] * workSkel.joints[j].inverseBindMatrix;
        }

        // Upload bone matrices to the GPU using raw upload
        skinnedRenderer.updateBoneMatricesRaw(frameIndex, slot,
                                               boneMatrices.data(), boneMatrices.size());

        // Ragdoll bone matrices are already in world space (physics positions are world-space),
        // so the draw transform should be identity
        ragdollTransforms_.push_back(glm::mat4(1.0f));

        renderedCount_++;
    }

    return renderedCount_;
}

void RagdollRenderer::recordDrawCommands(VkCommandBuffer cmd,
                                          uint32_t frameIndex,
                                          AnimatedCharacter& templateCharacter,
                                          SkinnedMeshRenderer& skinnedRenderer,
                                          uint32_t firstSlot) {
    for (size_t i = 0; i < renderedCount_; ++i) {
        uint32_t slot = firstSlot + static_cast<uint32_t>(i);
        skinnedRenderer.record(cmd, frameIndex, slot,
                               ragdollTransforms_[i], templateCharacter);
    }
}

void RagdollRenderer::drawDebugOverlay(const std::vector<ArticulatedBody>& ragdolls,
                                        const PhysicsWorld& physics,
                                        DebugLineSystem& debugLines,
                                        const std::vector<TargetFrame>* targetFrames) {
    if (!debugEnabled_ || !configured_) return;

    // Colors
    const glm::vec4 bodyColor(0.2f, 0.8f, 0.2f, 1.0f);     // Green: ragdoll bodies
    const glm::vec4 jointColor(1.0f, 1.0f, 0.0f, 1.0f);     // Yellow: joint connections
    const glm::vec4 targetColor(0.3f, 0.5f, 1.0f, 0.7f);    // Blue: target pose ghost

    for (const auto& ragdoll : ragdolls) {
        if (!ragdoll.isValid()) continue;

        // Draw each body part as a sphere at its position
        std::vector<ArticulatedBody::PartState> states;
        ragdoll.getState(states, physics);

        for (size_t i = 0; i < states.size(); ++i) {
            const auto& s = states[i];
            debugLines.addSphere(s.position, 0.05f, bodyColor, 8);

            // Draw connections between parent and child parts
            int32_t jointIdx = ragdoll.getPartJointIndex(i);
            if (jointIdx >= 0 && static_cast<size_t>(jointIdx) < skeleton_.joints.size()) {
                int32_t parentJoint = skeleton_.joints[jointIdx].parentIndex;
                if (parentJoint >= 0) {
                    // Find the part that maps to the parent joint
                    for (size_t p = 0; p < states.size(); ++p) {
                        if (ragdoll.getPartJointIndex(p) == parentJoint) {
                            debugLines.addLine(states[p].position, s.position, jointColor);
                            break;
                        }
                    }
                }
            }
        }

        // Draw velocity vectors
        for (const auto& s : states) {
            if (glm::length(s.linearVelocity) > 0.1f) {
                glm::vec3 velEnd = s.position + s.linearVelocity * 0.1f;
                debugLines.addLine(s.position, velEnd, glm::vec4(1.0f, 0.3f, 0.3f, 0.8f));
            }
        }
    }

    // Draw target pose ghost if available
    if (targetFrames && !targetFrames->empty()) {
        const auto& target = (*targetFrames)[0];

        // Root position marker
        debugLines.addSphere(target.rootPosition, 0.08f, targetColor, 8);

        // Joint positions
        for (size_t i = 0; i < target.jointPositions.size(); ++i) {
            debugLines.addSphere(target.jointPositions[i], 0.03f, targetColor, 6);

            // Connect to root as a simple visualization
            if (i < skeleton_.joints.size()) {
                int32_t parentIdx = skeleton_.joints[i].parentIndex;
                if (parentIdx >= 0 && static_cast<size_t>(parentIdx) < target.jointPositions.size()) {
                    debugLines.addLine(target.jointPositions[parentIdx],
                                        target.jointPositions[i], targetColor);
                }
            }
        }
    }
}

} // namespace ml::unicon
