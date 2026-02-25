#pragma once

#include "MotionFrame.h"
#include "MotionObservationComputer.h"
#include "../animation/Animation.h"
#include "../loaders/GLTFLoader.h"

#include <vector>
#include <string>
#include <random>

namespace ml { struct CharacterConfig; }

namespace training {

// Loads FBX animation files via FBXLoader and provides random MotionFrame
// sampling for training episode resets and reference motion data.
//
// After loading, call precomputeObservations() to compute and cache AMP
// observations for all clip frames. This enables direct FBX-to-discriminator
// training without an intermediate .npy conversion step.
//
// Usage:
//   MotionLibrary lib;
//   lib.loadFromDirectory("assets/characters/fbx/", skeleton);
//   lib.precomputeObservations(charConfig, skeleton, 30.0f);
//   // For episode resets:
//   MotionFrame frame = lib.sampleRandomFrame(rng, skeleton);
//   // For discriminator training:
//   lib.sampleObservations(batchSize, rng, outBuffer);
class MotionLibrary {
public:
    MotionLibrary() = default;

    // Load all FBX files from a directory (recursively).
    // Returns number of clips loaded.
    int loadFromDirectory(const std::string& directory, const Skeleton& skeleton);

    // Load a single FBX file. Returns number of clips loaded from it.
    int loadFile(const std::string& path, const Skeleton& skeleton);

    // Sample a random MotionFrame from a random clip at a random time.
    // The skeleton is used to compute FK (global joint positions).
    MotionFrame sampleRandomFrame(std::mt19937& rng, const Skeleton& skeleton) const;

    // Sample a MotionFrame from a specific clip at a specific time.
    MotionFrame sampleFrame(int clipIndex, float time, const Skeleton& skeleton) const;

    // --- Observation precomputation ---

    // Pre-sample all clips at the given FPS and compute AMP observations.
    // Must be called after loading clips and before sampleObservations().
    void precomputeObservations(const ml::CharacterConfig& config,
                                const Skeleton& skeleton,
                                float fps = 30.0f);

    // Sample a batch of random AMP observations from the precomputed cache.
    // outBuffer must have space for (batchSize * observationDim) floats.
    // Duration-weighted sampling: longer clips are sampled more often.
    void sampleObservations(int batchSize, std::mt19937& rng,
                            float* outBuffer) const;

    // Whether observations have been precomputed.
    bool hasObservations() const { return !cachedObs_.empty(); }

    // Per-frame observation dimension (0 if not precomputed).
    int observationDim() const { return obsDim_; }

    // Total number of precomputed observation frames.
    int totalObsFrames() const { return totalObsFrames_; }

    // Number of loaded clips.
    int numClips() const { return static_cast<int>(clips_.size()); }

    // Total duration of all clips (for weighted sampling).
    float totalDuration() const { return totalDuration_; }

    // Get clip name.
    const std::string& clipName(int index) const { return clips_[index].name; }

    // Get clip duration.
    float clipDuration(int index) const { return clips_[index].duration; }

    // Check if any clips are loaded.
    bool empty() const { return clips_.empty(); }

private:
    std::vector<AnimationClip> clips_;
    float totalDuration_ = 0.0f;

    // Precomputed observation cache.
    // Per-clip: flat array of (numFrames * obsDim) floats.
    struct ClipObservations {
        int numFrames = 0;
        std::vector<float> data;  // numFrames * obsDim
    };
    std::vector<ClipObservations> cachedObs_;
    int obsDim_ = 0;
    int totalObsFrames_ = 0;

    // Convert a sampled skeleton pose to a MotionFrame.
    static MotionFrame poseToMotionFrame(
        const Skeleton& skeleton,
        const std::vector<glm::mat4>& globalTransforms,
        int rootBoneIndex);
};

} // namespace training
