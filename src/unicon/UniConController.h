#pragma once

#include "StateEncoder.h"
#include "../core/ml/MLPPolicy.h"
#include "../physics/ArticulatedBody.h"
#include "../physics/PhysicsSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>

// Drives ArticulatedBody ragdolls with the MLP policy.
//
// Usage:
//   UniConController controller;
//   controller.init(20, 1);                      // 20 joints, tau=1
//   controller.loadPolicy("weights.bin");         // or initRandomPolicy() for testing
//   ...
//   controller.update(ragdolls, physics);         // call each frame BEFORE physics step
//
// The controller builds an observation from each ragdoll's state + a target frame,
// runs the MLP, and applies the resulting torques.
class UniConController {
public:
    // Configure encoder dimensions and allocate the policy.
    // numJoints: body part count (20 for standard humanoid)
    // tau: number of future target frames in the observation (paper uses 1)
    void init(size_t numJoints, size_t tau = 1);

    // Load trained policy weights from disk.
    bool loadPolicy(const std::string& path);

    // Build a random policy for testing / debugging.
    void initRandomPolicy();

    // Run the observe → infer → apply loop for every ragdoll.
    // Call this BEFORE physics().update() so that the torques are
    // integrated in the next simulation step.
    void update(std::vector<ArticulatedBody>& ragdolls, PhysicsWorld& physics);

    // Set the target frame that the policy should track.
    // For now this is a single global target; later it will come from
    // a motion planner or high-level policy.
    void setTargetFrame(const TargetFrame& target);

    bool isReady() const { return policy_.isLoaded(); }

    // Expose for GUI / debug
    size_t getObservationDim() const { return encoder_.getObservationDim(); }
    size_t getActionDim() const { return policy_.getOutputDim(); }

private:
    // Build a default standing target frame from the ragdoll's current root.
    TargetFrame makeStandingTarget(const ArticulatedBody& body,
                                   const PhysicsWorld& physics) const;

    StateEncoder encoder_;
    MLPPolicy policy_;

    // Target frames for the policy (one per tau)
    std::vector<TargetFrame> targetFrames_;

    // Reusable buffers (avoid per-frame allocation)
    std::vector<float> observation_;
    std::vector<float> action_;
    std::vector<glm::vec3> torques_;

    size_t numJoints_ = 0;
    bool useCustomTarget_ = false;
};
