#include "MotionScheduler.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/quaternion.hpp>
#include <cmath>
#include <algorithm>

namespace ml::unicon {

// =============================================================================
// MocapScheduler
// =============================================================================

void MocapScheduler::configure(const Skeleton& skeleton, size_t tau, float futureStepDt) {
    skel_ = skeleton;
    tau_ = tau;
    futureStepDt_ = futureStepDt;
    configured_ = true;
}

void MocapScheduler::setClip(const AnimationClip* clip) {
    clip_ = clip;
    currentTime_ = 0.0f;
}

void MocapScheduler::reset() {
    currentTime_ = 0.0f;
}

void MocapScheduler::schedule(float dt,
                               const glm::vec3& /*currentRootPos*/,
                               const glm::quat& /*currentRootRot*/,
                               std::vector<TargetFrame>& outFrames) {
    outFrames.resize(tau_);

    if (!configured_ || !clip_ || clip_->duration <= 0.0f) {
        // Fill with default standing frames
        for (auto& tf : outFrames) {
            tf = TargetFrame{};
        }
        return;
    }

    // Advance playback time
    currentTime_ += dt * playbackSpeed_;
    if (looping_) {
        while (currentTime_ >= clip_->duration) {
            currentTime_ -= clip_->duration;
        }
        while (currentTime_ < 0.0f) {
            currentTime_ += clip_->duration;
        }
    } else {
        currentTime_ = std::clamp(currentTime_, 0.0f, clip_->duration);
    }

    // Sample current frame for velocity estimation baseline
    float prevTime = currentTime_ - futureStepDt_;
    if (looping_ && prevTime < 0.0f) prevTime += clip_->duration;
    prevTime = std::max(prevTime, 0.0f);

    TargetFrame prevFrame = sampleAtTime(prevTime);

    // Produce tau future target frames
    for (size_t i = 0; i < tau_; ++i) {
        float futureTime = currentTime_ + static_cast<float>(i + 1) * futureStepDt_;
        if (looping_) {
            while (futureTime >= clip_->duration) {
                futureTime -= clip_->duration;
            }
        } else {
            futureTime = std::min(futureTime, clip_->duration);
        }

        TargetFrame frame = sampleAtTime(futureTime);

        // Estimate velocities from the previous sample
        float samplePrevTime = futureTime - futureStepDt_;
        if (looping_ && samplePrevTime < 0.0f) samplePrevTime += clip_->duration;
        samplePrevTime = std::max(samplePrevTime, 0.0f);

        TargetFrame samplePrev = sampleAtTime(samplePrevTime);
        estimateVelocities(samplePrev, frame, futureStepDt_, frame);

        outFrames[i] = frame;
        prevFrame = frame;
    }
}

TargetFrame MocapScheduler::sampleAtTime(float time) const {
    TargetFrame tf;

    // Make a working copy of the skeleton and sample the clip into it
    Skeleton workSkel = skel_;
    clip_->sample(time, workSkel, /*stripRootMotion=*/false);

    // Compute global transforms via FK
    std::vector<glm::mat4> globalTransforms;
    workSkel.computeGlobalTransforms(globalTransforms);

    // Extract root transform
    int32_t rootIdx = clip_->rootBoneIndex;
    if (rootIdx >= 0 && rootIdx < static_cast<int32_t>(globalTransforms.size())) {
        const glm::mat4& rootGlobal = globalTransforms[rootIdx];
        tf.rootPosition = glm::vec3(rootGlobal[3]);

        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(rootGlobal[0]));
        scale.y = glm::length(glm::vec3(rootGlobal[1]));
        scale.z = glm::length(glm::vec3(rootGlobal[2]));

        glm::mat3 rotMat(
            glm::vec3(rootGlobal[0]) / scale.x,
            glm::vec3(rootGlobal[1]) / scale.y,
            glm::vec3(rootGlobal[2]) / scale.z
        );
        tf.rootRotation = glm::quat_cast(rotMat);
    } else if (!globalTransforms.empty()) {
        tf.rootPosition = glm::vec3(globalTransforms[0][3]);
        tf.rootRotation = glm::quat_cast(glm::mat3(globalTransforms[0]));
    }

    // Extract per-joint world-space positions and rotations
    size_t numJoints = workSkel.joints.size();
    tf.jointPositions.resize(numJoints);
    tf.jointRotations.resize(numJoints);
    tf.jointAngularVelocities.resize(numJoints, glm::vec3(0.0f));

    for (size_t i = 0; i < numJoints; ++i) {
        if (i < globalTransforms.size()) {
            const glm::mat4& g = globalTransforms[i];
            tf.jointPositions[i] = glm::vec3(g[3]);

            glm::vec3 s;
            s.x = glm::length(glm::vec3(g[0]));
            s.y = glm::length(glm::vec3(g[1]));
            s.z = glm::length(glm::vec3(g[2]));

            glm::mat3 rotMat(
                glm::vec3(g[0]) / s.x,
                glm::vec3(g[1]) / s.y,
                glm::vec3(g[2]) / s.z
            );
            tf.jointRotations[i] = glm::quat_cast(rotMat);
        }
    }

    return tf;
}

void MocapScheduler::estimateVelocities(const TargetFrame& prev, const TargetFrame& curr,
                                          float dt, TargetFrame& out) {
    if (dt <= 0.0f) {
        out.rootLinearVelocity = glm::vec3(0.0f);
        out.rootAngularVelocity = glm::vec3(0.0f);
        return;
    }

    float invDt = 1.0f / dt;

    // Root linear velocity: finite difference of position
    out.rootLinearVelocity = (curr.rootPosition - prev.rootPosition) * invDt;

    // Root angular velocity: from quaternion difference
    // dq = curr * inverse(prev), then angular_vel = 2 * log(dq) / dt
    glm::quat dq = curr.rootRotation * glm::inverse(prev.rootRotation);
    // Ensure shortest path
    if (dq.w < 0.0f) dq = -dq;

    // Convert quaternion delta to angular velocity
    // For small rotations: angular_vel ~= 2 * vec(dq) / dt
    float sinHalfAngle = glm::length(glm::vec3(dq.x, dq.y, dq.z));
    if (sinHalfAngle > 1e-6f) {
        float halfAngle = std::atan2(sinHalfAngle, dq.w);
        glm::vec3 axis = glm::vec3(dq.x, dq.y, dq.z) / sinHalfAngle;
        out.rootAngularVelocity = axis * (2.0f * halfAngle * invDt);
    } else {
        out.rootAngularVelocity = glm::vec3(0.0f);
    }

    // Per-joint angular velocities
    size_t n = std::min(prev.jointRotations.size(), curr.jointRotations.size());
    out.jointAngularVelocities.resize(curr.jointRotations.size(), glm::vec3(0.0f));

    for (size_t i = 0; i < n; ++i) {
        glm::quat jdq = curr.jointRotations[i] * glm::inverse(prev.jointRotations[i]);
        if (jdq.w < 0.0f) jdq = -jdq;

        float jSin = glm::length(glm::vec3(jdq.x, jdq.y, jdq.z));
        if (jSin > 1e-6f) {
            float jHalf = std::atan2(jSin, jdq.w);
            glm::vec3 jAxis = glm::vec3(jdq.x, jdq.y, jdq.z) / jSin;
            out.jointAngularVelocities[i] = jAxis * (2.0f * jHalf * invDt);
        }
    }
}

// =============================================================================
// KeyboardScheduler
// =============================================================================

void KeyboardScheduler::configure(const Skeleton& skeleton, const ClipBinding& clips,
                                   size_t tau, float futureStepDt) {
    clips_ = clips;
    primary_.configure(skeleton, tau, futureStepDt);
    secondary_.configure(skeleton, tau, futureStepDt);

    // Start with idle
    if (clips_.idle) {
        primary_.setClip(clips_.idle);
        activeClip_ = clips_.idle;
    }
    configured_ = true;
}

void KeyboardScheduler::setInput(const glm::vec3& direction, float magnitude,
                                  const glm::vec3& facingDir) {
    inputDir_ = direction;
    inputMag_ = magnitude;
    facingDir_ = facingDir;
}

void KeyboardScheduler::reset() {
    primary_.reset();
    secondary_.reset();
    blending_ = false;
    blendTimer_ = 0.0f;
    blendWeight_ = 0.0f;
    inputMag_ = 0.0f;
    activeClip_ = clips_.idle;
    if (clips_.idle) primary_.setClip(clips_.idle);
}

void KeyboardScheduler::schedule(float dt,
                                  const glm::vec3& currentRootPos,
                                  const glm::quat& currentRootRot,
                                  std::vector<TargetFrame>& outFrames) {
    if (!configured_) {
        outFrames.resize(1);
        outFrames[0] = TargetFrame{};
        return;
    }

    // Determine which clip to use based on input
    const AnimationClip* desiredClip = clips_.idle;

    if (inputMag_ > 0.05f) {
        // Determine direction relative to facing
        float facingLen = glm::length(glm::vec2(facingDir_.x, facingDir_.z));
        if (facingLen > 1e-6f) {
            glm::vec2 fwd = glm::normalize(glm::vec2(facingDir_.x, facingDir_.z));
            glm::vec2 right = glm::vec2(fwd.y, -fwd.x);
            glm::vec2 moveDir = glm::vec2(inputDir_.x, inputDir_.z);
            float moveLen = glm::length(moveDir);
            if (moveLen > 1e-6f) {
                moveDir /= moveLen;
                float dotFwd = glm::dot(moveDir, fwd);
                float dotRight = glm::dot(moveDir, right);

                // Use run clip if available and magnitude is high enough
                bool useRun = inputMag_ > 0.6f && clips_.run;

                if (dotFwd > 0.5f) {
                    desiredClip = useRun ? clips_.run : clips_.walkForward;
                } else if (dotFwd < -0.5f) {
                    desiredClip = clips_.walkBack ? clips_.walkBack : clips_.walkForward;
                } else if (dotRight > 0.3f) {
                    desiredClip = clips_.strafeRight ? clips_.strafeRight : clips_.walkForward;
                } else if (dotRight < -0.3f) {
                    desiredClip = clips_.strafeLeft ? clips_.strafeLeft : clips_.walkForward;
                } else {
                    desiredClip = useRun ? clips_.run : clips_.walkForward;
                }
            }
        }

        // Fallback: if desired clip is null, use idle
        if (!desiredClip) desiredClip = clips_.idle;
    }

    // Trigger blend if clip changed
    if (desiredClip != activeClip_ && desiredClip) {
        secondary_ = primary_;
        primary_.setClip(desiredClip);
        blending_ = true;
        blendTimer_ = 0.0f;
        activeClip_ = desiredClip;
    }

    // Update blend
    if (blending_) {
        blendTimer_ += dt;
        blendWeight_ = std::clamp(blendTimer_ / blendDuration_, 0.0f, 1.0f);
        if (blendWeight_ >= 1.0f) {
            blending_ = false;
        }
    }

    // Schedule from primary
    std::vector<TargetFrame> primaryFrames;
    primary_.schedule(dt, currentRootPos, currentRootRot, primaryFrames);

    if (blending_) {
        // Also schedule from secondary and blend
        std::vector<TargetFrame> secondaryFrames;
        secondary_.schedule(dt, currentRootPos, currentRootRot, secondaryFrames);

        outFrames.resize(primaryFrames.size());
        for (size_t i = 0; i < primaryFrames.size(); ++i) {
            if (i < secondaryFrames.size()) {
                outFrames[i] = blend(secondaryFrames[i], primaryFrames[i], blendWeight_);
            } else {
                outFrames[i] = primaryFrames[i];
            }
        }
    } else {
        outFrames = std::move(primaryFrames);
    }
}

TargetFrame KeyboardScheduler::blend(const TargetFrame& a, const TargetFrame& b, float t) {
    TargetFrame result;
    result.rootPosition = glm::mix(a.rootPosition, b.rootPosition, t);
    result.rootRotation = glm::slerp(a.rootRotation, b.rootRotation, t);
    result.rootLinearVelocity = glm::mix(a.rootLinearVelocity, b.rootLinearVelocity, t);
    result.rootAngularVelocity = glm::mix(a.rootAngularVelocity, b.rootAngularVelocity, t);

    size_t n = std::min(a.jointPositions.size(), b.jointPositions.size());
    result.jointPositions.resize(n);
    result.jointRotations.resize(n);
    result.jointAngularVelocities.resize(n);

    for (size_t i = 0; i < n; ++i) {
        result.jointPositions[i] = glm::mix(a.jointPositions[i], b.jointPositions[i], t);
        result.jointRotations[i] = glm::slerp(a.jointRotations[i], b.jointRotations[i], t);
        if (i < a.jointAngularVelocities.size() && i < b.jointAngularVelocities.size()) {
            result.jointAngularVelocities[i] = glm::mix(a.jointAngularVelocities[i],
                                                         b.jointAngularVelocities[i], t);
        }
    }

    return result;
}

} // namespace ml::unicon
