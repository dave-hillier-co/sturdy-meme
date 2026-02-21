#pragma once

#include "StateEncoder.h"
#include "MotionScheduler.h"
#include "../MLPNetwork.h"
#include "../Tensor.h"
#include "../../physics/ArticulatedBody.h"
#include "../../physics/PhysicsSystem.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>

namespace ml::unicon {

// Drives ArticulatedBody ragdolls with an MLP policy using the UniCon
// target-frame-tracking approach.
//
// Usage:
//   ml::unicon::Controller controller;
//   controller.init(20, 1);                      // 20 joints, tau=1
//   controller.loadPolicy("weights.bin");         // or initRandomPolicy() for testing
//   controller.setScheduler(std::move(scheduler)); // optional: use mocap/keyboard targets
//   ...
//   controller.update(ragdolls, physics, dt);     // call each frame BEFORE physics step
//
// The controller builds an observation from each ragdoll's state + a target frame,
// runs the MLP, and applies the resulting torques.
class Controller {
public:
    // Configure encoder dimensions and allocate the policy.
    // numJoints: body part count (20 for standard humanoid)
    // tau: number of future target frames in the observation (paper uses 1)
    void init(size_t numJoints, size_t tau = 1);

    // Load trained policy weights from binary file.
    bool loadPolicy(const std::string& path);

    // Build a random policy for testing / debugging.
    void initRandomPolicy();

    // Set a motion scheduler to produce target frames each frame.
    // If no scheduler is set, a default standing target is used.
    void setScheduler(std::unique_ptr<MotionScheduler> scheduler);

    // Run the observe -> infer -> apply loop for every ragdoll.
    // Call this BEFORE physics().update() so that the torques are
    // integrated in the next simulation step.
    void update(std::vector<ArticulatedBody>& ragdolls, PhysicsWorld& physics, float dt = 1.0f / 60.0f);

    // Set the target frame that the policy should track (manual override).
    void setTargetFrame(const TargetFrame& target);

    // Apply a random impulse to a random body part of each ragdoll.
    // Useful for testing perturbation recovery.
    // maxForce: maximum force magnitude in Newtons
    void applyRandomPerturbation(std::vector<ArticulatedBody>& ragdolls,
                                  PhysicsWorld& physics, float maxForce = 500.0f);

    // Enable periodic automatic perturbations for stress-testing.
    // intervalSeconds: time between perturbations (0 = disabled)
    void setAutoPerturbation(float intervalSeconds, float maxForce = 500.0f);

    bool isReady() const { return policyLoaded_; }

    MotionScheduler* getScheduler() { return scheduler_.get(); }
    const std::vector<TargetFrame>& getTargetFrames() const { return targetFrames_; }
    size_t getObservationDim() const { return encoder_.getObservationDim(); }
    size_t getActionDim() const { return actionDim_; }

private:
    TargetFrame makeStandingTarget(const ArticulatedBody& body,
                                   const PhysicsWorld& physics) const;

    StateEncoder encoder_;
    ml::MLPNetwork policy_;
    bool policyLoaded_ = false;
    std::unique_ptr<MotionScheduler> scheduler_;

    // Target frames for the policy (one per tau)
    std::vector<TargetFrame> targetFrames_;

    // Reusable buffers (avoid per-frame allocation)
    std::vector<float> observation_;
    ml::Tensor obsTensor_;
    ml::Tensor actionTensor_;
    std::vector<glm::vec3> torques_;

    size_t numJoints_ = 0;
    size_t actionDim_ = 0;
    bool useCustomTarget_ = false;

    // Auto-perturbation state
    float perturbInterval_ = 0.0f;
    float perturbMaxForce_ = 500.0f;
    float perturbTimer_ = 0.0f;
};

} // namespace ml::unicon
