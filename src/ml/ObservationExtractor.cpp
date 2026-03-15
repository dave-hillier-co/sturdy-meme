#include "ObservationExtractor.h"
#include "GLTFLoader.h"
#include "CharacterController.h"
#include "RagdollInstance.h"
#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <cassert>

namespace ml {

ObservationExtractor::ObservationExtractor(const CharacterConfig& config)
    : config_(config) {
    // Pre-allocate history frames
    for (auto& frame : history_) {
        frame.resize(config_.observationDim, 0.0f);
    }
    prevDOFPositions_.resize(config_.actionDim, 0.0f);
}

void ObservationExtractor::reset() {
    historyIndex_ = 0;
    historyCount_ = 0;
    hasPreviousFrame_ = false;
    std::fill(prevDOFPositions_.begin(), prevDOFPositions_.end(), 0.0f);
    prevRootRotation_ = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void ObservationExtractor::extractFrame(const Skeleton& skeleton,
                                         const CharacterController& controller,
                                         float deltaTime) {
    auto& frame = history_[historyIndex_];
    frame.clear();
    frame.reserve(config_.observationDim);

    extractRootFeatures(skeleton, controller, deltaTime, frame);
    extractDOFFeatures(skeleton, deltaTime, frame);
    extractKeyBodyFeatures(skeleton, frame);

    assert(static_cast<int>(frame.size()) == config_.observationDim);

    historyIndex_ = (historyIndex_ + 1) % MAX_OBS_HISTORY;
    if (historyCount_ < MAX_OBS_HISTORY) {
        ++historyCount_;
    }

    hasPreviousFrame_ = true;
}

Tensor ObservationExtractor::getCurrentObs() const {
    if (historyCount_ == 0) {
        return Tensor(config_.observationDim);
    }
    int latest = (historyIndex_ - 1 + MAX_OBS_HISTORY) % MAX_OBS_HISTORY;
    return Tensor(1, config_.observationDim,
                  std::vector<float>(history_[latest].begin(), history_[latest].end()));
}

Tensor ObservationExtractor::getStackedObs(int numSteps) const {
    int totalDim = numSteps * config_.observationDim;
    std::vector<float> stacked(totalDim, 0.0f);

    int available = std::min(numSteps, historyCount_);
    for (int s = 0; s < available; ++s) {
        // Stack from oldest to newest
        int frameIdx = (historyIndex_ - available + s + MAX_OBS_HISTORY) % MAX_OBS_HISTORY;
        int offset = s * config_.observationDim;
        const auto& frame = history_[frameIdx];
        std::copy(frame.begin(), frame.end(), stacked.begin() + offset);
    }

    return Tensor(1, totalDim, std::move(stacked));
}

Tensor ObservationExtractor::getEncoderObs() const {
    return getStackedObs(config_.numEncoderObsSteps);
}

Tensor ObservationExtractor::getPolicyObs() const {
    return getStackedObs(config_.numPolicyObsSteps);
}

// ---- Root features ----

void ObservationExtractor::extractRootFeatures(
    const Skeleton& skeleton,
    const CharacterController& controller,
    float deltaTime,
    std::vector<float>& obs) {

    // Root position and rotation
    glm::vec3 rootPos = controller.getPosition();
    const auto& rootJoint = skeleton.joints[config_.rootJointIndex];
    glm::quat rootRot = glm::quat_cast(rootJoint.localTransform);

    // 1) Root height (1D)
    obs.push_back(rootPos.y);

    // 2) Root rotation — heading-invariant 6D (6D)
    glm::quat headingFree = removeHeading(rootRot);
    float rot6d[6];
    quatToTanNorm6D(headingFree, rot6d);
    for (int i = 0; i < 6; ++i) {
        obs.push_back(rot6d[i]);
    }

    // 3) Local root velocity in heading frame (3D)
    // Use quaternion rotation to transform world velocity into heading frame
    glm::quat invHeading = glm::angleAxis(-getHeadingAngle(rootRot), glm::vec3(0.0f, 1.0f, 0.0f));

    glm::vec3 worldVel = controller.getVelocity();
    glm::vec3 localVel = invHeading * worldVel;
    obs.push_back(localVel.x);
    obs.push_back(localVel.y);
    obs.push_back(localVel.z);

    // 4) Local root angular velocity (3D)
    if (hasPreviousFrame_ && deltaTime > 0.0f) {
        glm::quat deltaRot = rootRot * glm::inverse(prevRootRotation_);
        // Ensure shortest path
        if (deltaRot.w < 0.0f) deltaRot = -deltaRot;
        float angle = glm::angle(deltaRot);
        glm::vec3 axis = (angle > 1e-6f) ? glm::axis(deltaRot) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 angVel = axis * (angle / deltaTime);
        glm::vec3 localAngVel = invHeading * angVel;
        obs.push_back(localAngVel.x);
        obs.push_back(localAngVel.y);
        obs.push_back(localAngVel.z);
    } else {
        obs.push_back(0.0f);
        obs.push_back(0.0f);
        obs.push_back(0.0f);
    }

    prevRootRotation_ = rootRot;
}

// ---- DOF features ----

void ObservationExtractor::extractDOFFeatures(
    const Skeleton& skeleton,
    float deltaTime,
    std::vector<float>& obs) {

    // Extract current DOF positions (joint angles)
    std::vector<float> currentDOFs(config_.actionDim, 0.0f);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        const auto& joint = skeleton.joints[mapping.jointIndex];

        // Decompose local transform to Euler angles
        glm::vec3 euler = matrixToEulerXYZ(joint.localTransform);
        currentDOFs[d] = euler[mapping.axis];
    }

    // DOF positions
    for (int d = 0; d < config_.actionDim; ++d) {
        obs.push_back(currentDOFs[d]);
    }

    // DOF velocities (finite difference)
    for (int d = 0; d < config_.actionDim; ++d) {
        if (hasPreviousFrame_ && deltaTime > 0.0f) {
            obs.push_back((currentDOFs[d] - prevDOFPositions_[d]) / deltaTime);
        } else {
            obs.push_back(0.0f);
        }
    }

    prevDOFPositions_ = currentDOFs;
}

// ---- Key body features ----

void ObservationExtractor::extractKeyBodyFeatures(
    const Skeleton& skeleton,
    std::vector<float>& obs) {

    // Compute global transforms
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    // Root position and heading for local frame conversion
    glm::vec3 rootPos(0.0f);
    float headingAngle = 0.0f;

    if (config_.rootJointIndex >= 0 &&
        static_cast<size_t>(config_.rootJointIndex) < globalTransforms.size()) {
        rootPos = glm::vec3(globalTransforms[config_.rootJointIndex][3]);
        glm::quat rootRot = glm::quat_cast(globalTransforms[config_.rootJointIndex]);
        headingAngle = getHeadingAngle(rootRot);
    }

    glm::quat invHeading = glm::angleAxis(-headingAngle, glm::vec3(0.0f, 1.0f, 0.0f));

    for (const auto& kb : config_.keyBodies) {
        if (kb.jointIndex >= 0 &&
            static_cast<size_t>(kb.jointIndex) < globalTransforms.size()) {
            glm::vec3 worldPos(globalTransforms[kb.jointIndex][3]);
            glm::vec3 relPos = worldPos - rootPos;
            glm::vec3 localPos = invHeading * relPos;

            obs.push_back(localPos.x);
            obs.push_back(localPos.y);
            obs.push_back(localPos.z);
        } else {
            obs.push_back(0.0f);
            obs.push_back(0.0f);
            obs.push_back(0.0f);
        }
    }
}

// ---- Static helpers ----

void ObservationExtractor::quatToTanNorm6D(const glm::quat& q, float out[6]) {
    // Convert quaternion to rotation matrix, take first two columns
    glm::mat3 m = glm::mat3_cast(q);
    // Column 0
    out[0] = m[0][0];
    out[1] = m[0][1];
    out[2] = m[0][2];
    // Column 1
    out[3] = m[1][0];
    out[4] = m[1][1];
    out[5] = m[1][2];
}

float ObservationExtractor::getHeadingAngle(const glm::quat& q) {
    // Project forward direction onto XZ plane, compute yaw
    glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, 1.0f);
    return std::atan2(forward.x, forward.z);
}

glm::quat ObservationExtractor::removeHeading(const glm::quat& q) {
    float heading = getHeadingAngle(q);
    glm::quat headingQuat = glm::angleAxis(-heading, glm::vec3(0.0f, 1.0f, 0.0f));
    return headingQuat * q;
}

glm::vec3 ObservationExtractor::matrixToEulerXYZ(const glm::mat4& m) {
    // Convert to quaternion and use GLM's built-in euler angle extraction
    glm::quat q = glm::quat_cast(m);
    return glm::eulerAngles(q);
}

// ---- Ragdoll-based observation extraction ----

void ObservationExtractor::extractFrameFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime) {

    auto& frame = history_[historyIndex_];
    frame.clear();
    frame.reserve(config_.observationDim);

    extractRootFeaturesFromRagdoll(skeleton, ragdoll, deltaTime, frame);
    extractDOFFeaturesFromRagdoll(skeleton, ragdoll, deltaTime, frame);
    extractKeyBodyFeatures(skeleton, frame);  // Reuse — works from skeleton global transforms

    assert(static_cast<int>(frame.size()) == config_.observationDim);

    historyIndex_ = (historyIndex_ + 1) % MAX_OBS_HISTORY;
    if (historyCount_ < MAX_OBS_HISTORY) {
        ++historyCount_;
    }

    hasPreviousFrame_ = true;
}

void ObservationExtractor::extractRootFeaturesFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime,
    std::vector<float>& obs) {

    // Root position and rotation from ragdoll physics body
    glm::vec3 rootPos = ragdoll.getRootPosition();
    glm::quat rootRot = ragdoll.getRootRotation();

    // 1) Root height (1D)
    obs.push_back(rootPos.y);

    // 2) Root rotation — heading-invariant 6D (6D)
    glm::quat headingFree = removeHeading(rootRot);
    float rot6d[6];
    quatToTanNorm6D(headingFree, rot6d);
    for (int i = 0; i < 6; ++i) {
        obs.push_back(rot6d[i]);
    }

    // 3) Local root velocity in heading frame (3D)
    glm::quat invHeading = glm::angleAxis(-getHeadingAngle(rootRot), glm::vec3(0.0f, 1.0f, 0.0f));

    // Exact velocity from physics instead of finite differences
    glm::vec3 worldVel = ragdoll.getRootLinearVelocity();
    glm::vec3 localVel = invHeading * worldVel;
    obs.push_back(localVel.x);
    obs.push_back(localVel.y);
    obs.push_back(localVel.z);

    // 4) Local root angular velocity (3D)
    // Exact angular velocity from physics — much more accurate than finite differences
    glm::vec3 angVel = ragdoll.getRootAngularVelocity();
    glm::vec3 localAngVel = invHeading * angVel;
    obs.push_back(localAngVel.x);
    obs.push_back(localAngVel.y);
    obs.push_back(localAngVel.z);

    prevRootRotation_ = rootRot;
}

void ObservationExtractor::extractDOFFeaturesFromRagdoll(
    const Skeleton& skeleton,
    const physics::RagdollInstance& ragdoll,
    float deltaTime,
    std::vector<float>& obs) {

    // Read current pose from ragdoll
    SkeletonPose ragdollPose;
    ragdoll.readPose(ragdollPose, skeleton);

    // Extract DOF positions from ragdoll pose
    std::vector<float> currentDOFs(config_.actionDim, 0.0f);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        if (mapping.jointIndex >= 0 &&
            static_cast<size_t>(mapping.jointIndex) < ragdollPose.size()) {
            const auto& bp = ragdollPose[mapping.jointIndex];
            // Convert rotation to Euler angles
            glm::mat4 rotMat = glm::mat4_cast(bp.rotation);
            glm::vec3 euler = matrixToEulerXYZ(rotMat);
            currentDOFs[d] = euler[mapping.axis];
        }
    }

    // DOF positions
    for (int d = 0; d < config_.actionDim; ++d) {
        obs.push_back(currentDOFs[d]);
    }

    // DOF velocities — use per-body angular velocities from physics
    // This is more accurate than finite differences
    std::vector<glm::vec3> angVels;
    ragdoll.readBodyAngularVelocities(angVels);

    for (int d = 0; d < config_.actionDim; ++d) {
        const auto& mapping = config_.dofMappings[d];
        if (mapping.jointIndex >= 0 &&
            static_cast<size_t>(mapping.jointIndex) < angVels.size()) {
            // Project angular velocity onto the DOF axis
            obs.push_back(angVels[mapping.jointIndex][mapping.axis]);
        } else {
            obs.push_back(0.0f);
        }
    }

    prevDOFPositions_ = currentDOFs;
}

} // namespace ml
