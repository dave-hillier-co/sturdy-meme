#include "UniConController.h"

#include <SDL3/SDL_log.h>
#include <cmath>

void UniConController::init(size_t numJoints, size_t tau) {
    numJoints_ = numJoints;
    encoder_.configure(numJoints, tau);
    targetFrames_.resize(tau);

    // Pre-fill target frames with identity (standing upright at origin)
    for (auto& tf : targetFrames_) {
        tf.rootPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        tf.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        tf.rootLinearVelocity = glm::vec3(0.0f);
        tf.rootAngularVelocity = glm::vec3(0.0f);
        tf.jointPositions.assign(numJoints, glm::vec3(0.0f));
        tf.jointRotations.assign(numJoints, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
        tf.jointAngularVelocities.assign(numJoints, glm::vec3(0.0f));
    }

    SDL_Log("UniConController initialized: %zu joints, tau=%zu, obs_dim=%zu",
            numJoints, tau, encoder_.getObservationDim());
}

bool UniConController::loadPolicy(const std::string& path) {
    return policy_.loadWeights(path);
}

void UniConController::initRandomPolicy() {
    size_t obsDim = encoder_.getObservationDim();
    size_t actDim = numJoints_ * 3; // 3 torque components per joint
    policy_.initRandom(obsDim, actDim);
}

void UniConController::update(std::vector<ArticulatedBody>& ragdolls,
                               PhysicsWorld& physics) {
    if (!policy_.isLoaded()) return;

    for (auto& ragdoll : ragdolls) {
        if (!ragdoll.isValid()) continue;

        // 1. Build target frames — use custom if set, otherwise generate from current state
        if (!useCustomTarget_) {
            for (auto& tf : targetFrames_) {
                tf = makeStandingTarget(ragdoll, physics);
            }
        }

        // 2. Encode observation
        encoder_.encode(ragdoll, physics, targetFrames_, observation_);

        // 3. Run policy
        policy_.evaluate(observation_, action_);

        // 4. Convert flat action vector to per-joint torques
        size_t partCount = ragdoll.getPartCount();
        torques_.resize(partCount);
        for (size_t i = 0; i < partCount; ++i) {
            size_t base = i * 3;
            if (base + 2 < action_.size()) {
                torques_[i] = glm::vec3(action_[base], action_[base + 1], action_[base + 2]);
            } else {
                torques_[i] = glm::vec3(0.0f);
            }
        }

        // 5. Apply torques (effort factors are applied inside ArticulatedBody)
        ragdoll.applyTorques(physics, torques_);
    }
}

void UniConController::setTargetFrame(const TargetFrame& target) {
    useCustomTarget_ = true;
    for (auto& tf : targetFrames_) {
        tf = target;
    }
}

TargetFrame UniConController::makeStandingTarget(const ArticulatedBody& body,
                                                  const PhysicsWorld& physics) const {
    // The target is "stay upright where you are, zero velocity."
    // We take the current root position but enforce an upright orientation.
    TargetFrame tf;
    tf.rootPosition = body.getRootPosition(physics);
    tf.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f); // upright
    tf.rootLinearVelocity = glm::vec3(0.0f);
    tf.rootAngularVelocity = glm::vec3(0.0f);

    // Joint targets: all at root position with identity rotation (T-pose).
    // This is a rough approximation — a real system would use a reference pose.
    size_t n = body.getPartCount();
    tf.jointPositions.assign(n, tf.rootPosition);
    tf.jointRotations.assign(n, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    tf.jointAngularVelocities.assign(n, glm::vec3(0.0f));

    return tf;
}
