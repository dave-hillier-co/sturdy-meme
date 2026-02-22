#pragma once

#include "MotionFrame.h"
#include "CharacterConfig.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>

struct Skeleton;

namespace training {

// Computes AMP observations from reference motion data (MotionFrames).
//
// This mirrors the math in ml::ObservationExtractor but operates on
// pre-sampled animation data instead of live physics state. Used by
// MotionLibrary to precompute observations for all clip frames, enabling
// direct FBX-to-discriminator training without an intermediate .npy step.
//
// Observation layout matches ObservationExtractor exactly:
//   [0]        root height (1)
//   [1..6]     root rotation, heading-invariant 6D (6)
//   [7..9]     local root velocity in heading frame (3)
//   [10..12]   local root angular velocity in heading frame (3)
//   [13..13+N] DOF positions (N)
//   [13+N..13+2N] DOF velocities (N)
//   [13+2N..]  key body positions in root-relative heading frame (K*3)
class MotionObservationComputer {
public:
    MotionObservationComputer() = default;
    explicit MotionObservationComputer(const ml::CharacterConfig& config);

    // Compute a single-frame AMP observation from a MotionFrame.
    // prevFrame may be nullptr for the first frame (velocities will be zero).
    // dt is the time step between frames (1/fps).
    // Result is written to outObs which must have observationDim() floats.
    void computeFrame(const MotionFrame& frame,
                      const MotionFrame* prevFrame,
                      float dt,
                      float* outObs) const;

    // Compute observations for an entire clip (sequence of MotionFrames).
    // Returns a flat vector of size (numFrames * observationDim).
    std::vector<float> computeClip(const std::vector<MotionFrame>& frames,
                                   float fps) const;

    int observationDim() const { return config_.observationDim; }

private:
    ml::CharacterConfig config_;

    // Observation sub-extractors (same math as ObservationExtractor)
    void extractRootFeatures(const MotionFrame& frame,
                             const MotionFrame* prevFrame,
                             float dt,
                             std::vector<float>& obs) const;

    void extractDOFFeatures(const MotionFrame& frame,
                            const MotionFrame* prevFrame,
                            float dt,
                            std::vector<float>& obs) const;

    void extractKeyBodyFeatures(const MotionFrame& frame,
                                std::vector<float>& obs) const;

    // Static helpers (identical to ObservationExtractor)
    static void quatToTanNorm6D(const glm::quat& q, float out[6]);
    static float getHeadingAngle(const glm::quat& q);
    static glm::quat removeHeading(const glm::quat& q);
    static glm::vec3 quatToEulerXYZ(const glm::quat& q);
};

} // namespace training
