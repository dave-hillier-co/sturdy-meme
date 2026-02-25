#include "MotionObservationComputer.h"

#include <glm/gtc/matrix_transform.hpp>
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
    float headingAngle = getHeadingAngle(rootRot);
    float cosH = std::cos(-headingAngle);
    float sinH = std::sin(-headingAngle);

    if (prevFrame && dt > 0.0f) {
        glm::vec3 worldVel = (rootPos - prevFrame->rootPosition) / dt;
        obs.push_back(cosH * worldVel.x + sinH * worldVel.z);
        obs.push_back(worldVel.y);
        obs.push_back(-sinH * worldVel.x + cosH * worldVel.z);
    } else {
        obs.push_back(0.0f);
        obs.push_back(0.0f);
        obs.push_back(0.0f);
    }

    // 4) Local root angular velocity in heading frame (3D)
    if (prevFrame && dt > 0.0f) {
        glm::quat deltaRot = rootRot * glm::inverse(prevFrame->rootRotation);
        float angle = 2.0f * std::acos(std::clamp(deltaRot.w, -1.0f, 1.0f));
        glm::vec3 axis(0.0f, 1.0f, 0.0f);
        float sinHalfAngle = std::sqrt(1.0f - deltaRot.w * deltaRot.w);
        if (sinHalfAngle > 1e-6f) {
            axis = glm::vec3(deltaRot.x, deltaRot.y, deltaRot.z) / sinHalfAngle;
        }
        glm::vec3 angVel = axis * (angle / dt);
        obs.push_back(cosH * angVel.x + sinH * angVel.z);
        obs.push_back(angVel.y);
        obs.push_back(-sinH * angVel.x + cosH * angVel.z);
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
    float headingAngle = getHeadingAngle(frame.rootRotation);
    float cosH = std::cos(-headingAngle);
    float sinH = std::sin(-headingAngle);

    for (const auto& kb : config_.keyBodies) {
        if (kb.jointIndex >= 0 &&
            static_cast<size_t>(kb.jointIndex) < frame.jointPositions.size()) {
            glm::vec3 worldPos = frame.jointPositions[kb.jointIndex];
            glm::vec3 relPos = worldPos - rootPos;

            obs.push_back(cosH * relPos.x + sinH * relPos.z);
            obs.push_back(relPos.y);
            obs.push_back(-sinH * relPos.x + cosH * relPos.z);
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
    // Convert quaternion to matrix, then extract Euler XYZ angles.
    // Matches ObservationExtractor::matrixToEulerXYZ exactly.
    glm::mat4 m = glm::mat4_cast(q);
    glm::vec3 euler;
    float sy = m[0][2];
    if (std::abs(sy) < 0.99999f) {
        euler.x = std::atan2(-m[1][2], m[2][2]);
        euler.y = std::asin(sy);
        euler.z = std::atan2(-m[0][1], m[0][0]);
    } else {
        euler.x = std::atan2(m[2][1], m[1][1]);
        euler.y = (sy > 0.0f) ? 1.5707963f : -1.5707963f;
        euler.z = 0.0f;
    }
    return euler;
}

} // namespace training
