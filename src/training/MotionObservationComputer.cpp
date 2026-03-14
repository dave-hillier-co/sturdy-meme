#include "MotionObservationComputer.h"

#include <glm/gtc/matrix_transform.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>
#include <cmath>
#include <cassert>
#include <algorithm>

namespace training {

MotionObservationComputer::MotionObservationComputer(const ml::CharacterConfig& config)
    : config_(config) {
}

void MotionObservationComputer::computeFrame(const MotionFrame& frame,
                                              const MotionFrame* prevFrame,
                                              float dt,
                                              float* outObs) const {
    std::vector<float> obs;
    obs.reserve(config_.observationDim);

    extractRootFeatures(frame, prevFrame, dt, obs);
    extractDOFFeatures(frame, prevFrame, dt, obs);
    extractKeyBodyFeatures(frame, obs);

    assert(static_cast<int>(obs.size()) == config_.observationDim);
    std::copy(obs.begin(), obs.end(), outObs);
}

std::vector<float> MotionObservationComputer::computeClip(
    const std::vector<MotionFrame>& frames,
    float fps) const {

    if (frames.empty()) return {};

    float dt = (fps > 0.0f) ? (1.0f / fps) : (1.0f / 30.0f);
    int obsDim = config_.observationDim;
    std::vector<float> allObs(frames.size() * obsDim, 0.0f);

    for (size_t i = 0; i < frames.size(); ++i) {
        const MotionFrame* prev = (i > 0) ? &frames[i - 1] : nullptr;
        computeFrame(frames[i], prev, dt, allObs.data() + i * obsDim);
    }

    return allObs;
}

// ---- Root features ----

void MotionObservationComputer::extractRootFeatures(
    const MotionFrame& frame,
    const MotionFrame* prevFrame,
    float dt,
    std::vector<float>& obs) const {

    glm::vec3 rootPos = frame.rootPosition;
    glm::quat rootRot = frame.rootRotation;

    // 1) Root height (1D)
    obs.push_back(rootPos.y);

    // 2) Root rotation - heading-invariant 6D (6D)
    glm::quat headingFree = removeHeading(rootRot);
    float rot6d[6];
    quatToTanNorm6D(headingFree, rot6d);
    for (int i = 0; i < 6; ++i) {
        obs.push_back(rot6d[i]);
    }

    // 3) Local root velocity in heading frame (3D)
    // Use quaternion to rotate world velocity into heading frame instead of manual sin/cos
    glm::quat headingQuat = removeHeading(rootRot);
    glm::quat invHeading = glm::angleAxis(-getHeadingAngle(rootRot), glm::vec3(0.0f, 1.0f, 0.0f));

    if (prevFrame && dt > 0.0f) {
        glm::vec3 worldVel = (rootPos - prevFrame->rootPosition) / dt;
        glm::vec3 localVel = invHeading * worldVel;
        obs.push_back(localVel.x);
        obs.push_back(localVel.y);
        obs.push_back(localVel.z);
    } else {
        obs.push_back(0.0f);
        obs.push_back(0.0f);
        obs.push_back(0.0f);
    }

    // 4) Local root angular velocity in heading frame (3D)
    if (prevFrame && dt > 0.0f) {
        glm::quat deltaRot = rootRot * glm::inverse(prevFrame->rootRotation);
        // Ensure shortest path
        if (deltaRot.w < 0.0f) deltaRot = -deltaRot;
        float angle = glm::angle(deltaRot);
        glm::vec3 axis = (angle > 1e-6f) ? glm::axis(deltaRot) : glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 angVel = axis * (angle / dt);
        glm::vec3 localAngVel = invHeading * angVel;
        obs.push_back(localAngVel.x);
        obs.push_back(localAngVel.y);
        obs.push_back(localAngVel.z);
    } else {
        obs.push_back(0.0f);
        obs.push_back(0.0f);
        obs.push_back(0.0f);
    }
}

// ---- DOF features ----

void MotionObservationComputer::extractDOFFeatures(
    const MotionFrame& frame,
    const MotionFrame* prevFrame,
    float dt,
    std::vector<float>& obs) const {

    int numDOFs = config_.actionDim;

    // Extract current DOF positions from local joint rotations
    std::vector<float> currentDOFs(numDOFs, 0.0f);
    for (int d = 0; d < numDOFs; ++d) {
        const auto& mapping = config_.dofMappings[d];
        if (mapping.jointIndex >= 0 &&
            static_cast<size_t>(mapping.jointIndex) < frame.jointRotations.size()) {
            glm::vec3 euler = quatToEulerXYZ(frame.jointRotations[mapping.jointIndex]);
            currentDOFs[d] = euler[mapping.axis];
        }
    }

    // DOF positions
    for (int d = 0; d < numDOFs; ++d) {
        obs.push_back(currentDOFs[d]);
    }

    // DOF velocities (finite difference from previous frame)
    if (prevFrame && dt > 0.0f) {
        std::vector<float> prevDOFs(numDOFs, 0.0f);
        for (int d = 0; d < numDOFs; ++d) {
            const auto& mapping = config_.dofMappings[d];
            if (mapping.jointIndex >= 0 &&
                static_cast<size_t>(mapping.jointIndex) < prevFrame->jointRotations.size()) {
                glm::vec3 euler = quatToEulerXYZ(prevFrame->jointRotations[mapping.jointIndex]);
                prevDOFs[d] = euler[mapping.axis];
            }
        }
        for (int d = 0; d < numDOFs; ++d) {
            obs.push_back((currentDOFs[d] - prevDOFs[d]) / dt);
        }
    } else {
        for (int d = 0; d < numDOFs; ++d) {
            obs.push_back(0.0f);
        }
    }
}

// ---- Key body features ----

void MotionObservationComputer::extractKeyBodyFeatures(
    const MotionFrame& frame,
    std::vector<float>& obs) const {

    glm::vec3 rootPos = frame.rootPosition;
    glm::quat invHeading = glm::angleAxis(-getHeadingAngle(frame.rootRotation), glm::vec3(0.0f, 1.0f, 0.0f));

    for (const auto& kb : config_.keyBodies) {
        if (kb.jointIndex >= 0 &&
            static_cast<size_t>(kb.jointIndex) < frame.jointPositions.size()) {
            glm::vec3 worldPos = frame.jointPositions[kb.jointIndex];
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

// ---- Static helpers (identical to ObservationExtractor) ----

void MotionObservationComputer::quatToTanNorm6D(const glm::quat& q, float out[6]) {
    glm::mat3 m = glm::mat3_cast(q);
    out[0] = m[0][0];
    out[1] = m[0][1];
    out[2] = m[0][2];
    out[3] = m[1][0];
    out[4] = m[1][1];
    out[5] = m[1][2];
}

float MotionObservationComputer::getHeadingAngle(const glm::quat& q) {
    glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, 1.0f);
    return std::atan2(forward.x, forward.z);
}

glm::quat MotionObservationComputer::removeHeading(const glm::quat& q) {
    float heading = getHeadingAngle(q);
    glm::quat headingQuat = glm::angleAxis(-heading, glm::vec3(0.0f, 1.0f, 0.0f));
    return headingQuat * q;
}

glm::vec3 MotionObservationComputer::quatToEulerXYZ(const glm::quat& q) {
    // Use GLM's built-in euler angle extraction
    return glm::eulerAngles(q);
}

} // namespace training
