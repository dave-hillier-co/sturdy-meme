#pragma once

#include "StateEncoder.h"
#include "../../animation/Animation.h"
#include "../../loaders/GLTFLoader.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <memory>
#include <random>

namespace ml::unicon {

// MotionScheduler produces TargetFrame sequences for the UniCon policy.
// Different implementations select/blend animation clips in various ways
// (mocap playback, keyboard-driven, planner-based, etc.)
//
// The Controller calls schedule() each frame, receiving tau future target
// frames that are fed to the StateEncoder as part of the observation.
class MotionScheduler {
public:
    virtual ~MotionScheduler() = default;

    // Produce tau target frames starting from the current state.
    // dt: time step in seconds
    // currentRootPos/Rot: current ragdoll root state (for relative offsets)
    // outFrames: resized and filled with tau target frames
    virtual void schedule(float dt,
                          const glm::vec3& currentRootPos,
                          const glm::quat& currentRootRot,
                          std::vector<TargetFrame>& outFrames) = 0;

    // Reset scheduler state (e.g. when switching clips or on episode reset)
    virtual void reset() = 0;
};

// Plays back a single AnimationClip, converting sampled skeleton poses into
// TargetFrame sequences. Velocities are estimated via finite differences
// between consecutive samples.
class MocapScheduler : public MotionScheduler {
public:
    // Configure with the skeleton used for FK and the number of future frames.
    // tau: how many future target frames to produce (typically 1)
    // futureStepDt: time offset between consecutive future frames
    void configure(const Skeleton& skeleton, size_t tau = 1, float futureStepDt = 1.0f / 30.0f);

    // Set the clip to play. Resets playback time to 0.
    void setClip(const AnimationClip* clip);

    // Set looping mode (default: true)
    void setLooping(bool loop) { looping_ = loop; }

    // Set playback speed (default: 1.0)
    void setPlaybackSpeed(float speed) { playbackSpeed_ = speed; }

    void schedule(float dt,
                  const glm::vec3& currentRootPos,
                  const glm::quat& currentRootRot,
                  std::vector<TargetFrame>& outFrames) override;

    void reset() override;

    float getCurrentTime() const { return currentTime_; }
    const AnimationClip* getCurrentClip() const { return clip_; }

private:
    // Sample the clip at a given time and convert to TargetFrame.
    TargetFrame sampleAtTime(float time) const;

    // Estimate velocities via finite differences between two frames.
    static void estimateVelocities(const TargetFrame& prev, const TargetFrame& curr,
                                   float dt, TargetFrame& out);

    const AnimationClip* clip_ = nullptr;
    Skeleton skel_;              // Working copy for sampling
    size_t tau_ = 1;
    float futureStepDt_ = 1.0f / 30.0f;
    float currentTime_ = 0.0f;
    float playbackSpeed_ = 1.0f;
    bool looping_ = true;
    bool configured_ = false;
};

// Selects and blends animation clips based on keyboard/gamepad input.
// Maps WASD directions to clips (idle, walk forward, strafe left/right, walk back)
// and blends between them based on input magnitude and direction.
class KeyboardScheduler : public MotionScheduler {
public:
    struct ClipBinding {
        const AnimationClip* idle = nullptr;
        const AnimationClip* walkForward = nullptr;
        const AnimationClip* walkBack = nullptr;
        const AnimationClip* strafeLeft = nullptr;
        const AnimationClip* strafeRight = nullptr;
        const AnimationClip* run = nullptr;          // Optional: used at high speed
    };

    void configure(const Skeleton& skeleton, const ClipBinding& clips,
                   size_t tau = 1, float futureStepDt = 1.0f / 30.0f);

    // Set input state each frame.
    // direction: normalized XZ movement direction in world space
    // magnitude: 0 = idle, 0-0.5 = walk, 0.5-1.0 = run
    // facingDir: character's current facing direction
    void setInput(const glm::vec3& direction, float magnitude, const glm::vec3& facingDir);

    void schedule(float dt,
                  const glm::vec3& currentRootPos,
                  const glm::quat& currentRootRot,
                  std::vector<TargetFrame>& outFrames) override;

    void reset() override;

private:
    // Blend two target frames by lerp/slerp with weight t.
    static TargetFrame blend(const TargetFrame& a, const TargetFrame& b, float t);

    ClipBinding clips_;
    MocapScheduler primary_;     // Currently dominant clip
    MocapScheduler secondary_;   // Clip being blended from
    float blendWeight_ = 0.0f;
    float blendDuration_ = 0.3f;
    float blendTimer_ = 0.0f;
    bool blending_ = false;

    glm::vec3 inputDir_{0.0f};
    float inputMag_ = 0.0f;
    glm::vec3 facingDir_{0.0f, 0.0f, 1.0f};

    const AnimationClip* activeClip_ = nullptr;
    bool configured_ = false;
};

} // namespace ml::unicon
