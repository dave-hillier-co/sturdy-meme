#include "MotionLibrary.h"
#include "../loaders/FBXLoader.h"
#include "../loaders/FBXPostProcess.h"

#include <SDL3/SDL_log.h>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace training {

int MotionLibrary::loadFromDirectory(const std::string& directory, const Skeleton& skeleton) {
    int totalLoaded = 0;

    if (!fs::exists(directory) || !fs::is_directory(directory)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "MotionLibrary: Directory not found: %s", directory.c_str());
        return 0;
    }

    // Recursively find all .fbx files
    std::vector<fs::path> fbxFiles;
    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            auto ext = entry.path().extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == ".fbx") {
                // Skip mesh-only files (Y Bot.fbx is the T-pose mesh)
                auto filename = entry.path().filename().string();
                if (filename == "Y Bot.fbx") continue;
                fbxFiles.push_back(entry.path());
            }
        }
    }

    std::sort(fbxFiles.begin(), fbxFiles.end());
    SDL_Log("MotionLibrary: Found %zu FBX files in %s", fbxFiles.size(), directory.c_str());

    for (const auto& path : fbxFiles) {
        int loaded = loadFile(path.string(), skeleton);
        totalLoaded += loaded;
    }

    SDL_Log("MotionLibrary: Loaded %d animation clips (total duration: %.1fs)",
            totalLoaded, totalDuration_);
    return totalLoaded;
}

int MotionLibrary::loadFile(const std::string& path, const Skeleton& skeleton) {
    // Use Mixamo preset (0.01 scale for cm→m, Y-up)
    auto clips = FBXLoader::loadAnimations(path, skeleton, FBXPresets::Mixamo());

    if (clips.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MotionLibrary: No animations in %s", path.c_str());
        return 0;
    }

    int count = 0;
    for (auto& clip : clips) {
        if (clip.duration > 0.0f && !clip.channels.empty()) {
            totalDuration_ += clip.duration;
            SDL_Log("MotionLibrary:   '%s' - %.2fs, %zu channels",
                    clip.name.c_str(), clip.duration, clip.channels.size());
            clips_.push_back(std::move(clip));
            count++;
        }
    }
    return count;
}

MotionFrame MotionLibrary::sampleRandomFrame(std::mt19937& rng, const Skeleton& skeleton) const {
    if (clips_.empty()) {
        // Return default standing pose
        MotionFrame frame;
        frame.rootPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        frame.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return frame;
    }

    // Duration-weighted clip selection (longer clips more likely)
    std::uniform_real_distribution<float> dist(0.0f, totalDuration_);
    float r = dist(rng);
    float cumulative = 0.0f;
    int clipIdx = 0;
    for (int i = 0; i < static_cast<int>(clips_.size()); ++i) {
        cumulative += clips_[i].duration;
        if (r <= cumulative) {
            clipIdx = i;
            break;
        }
    }

    // Random time within clip
    std::uniform_real_distribution<float> timeDist(0.0f, clips_[clipIdx].duration);
    float time = timeDist(rng);

    return sampleFrame(clipIdx, time, skeleton);
}

MotionFrame MotionLibrary::sampleFrame(int clipIndex, float time, const Skeleton& skeleton) const {
    if (clipIndex < 0 || clipIndex >= static_cast<int>(clips_.size())) {
        MotionFrame frame;
        frame.rootPosition = glm::vec3(0.0f, 1.0f, 0.0f);
        frame.rootRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        return frame;
    }

    const auto& clip = clips_[clipIndex];

    // Make a copy of the skeleton to sample into (AnimationClip::sample modifies skeleton)
    Skeleton skel = skeleton;

    // Sample the animation into the skeleton's local transforms
    // Don't strip root motion - we want the full pose for training resets
    clip.sample(time, skel, /*stripRootMotion=*/false);

    // Compute global transforms via FK
    std::vector<glm::mat4> globalTransforms;
    skel.computeGlobalTransforms(globalTransforms);

    return poseToMotionFrame(skel, globalTransforms, clip.rootBoneIndex);
}

// --- Observation precomputation ---

void MotionLibrary::precomputeObservations(const ml::CharacterConfig& config,
                                            const Skeleton& skeleton,
                                            float fps) {
    if (clips_.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "MotionLibrary: No clips loaded, skipping observation precomputation");
        return;
    }

    MotionObservationComputer obsComputer(config);
    obsDim_ = config.observationDim;
    totalObsFrames_ = 0;
    cachedObs_.clear();
    cachedObs_.resize(clips_.size());

    float frameTime = 1.0f / fps;

    SDL_Log("MotionLibrary: Precomputing observations for %zu clips at %.0f fps (obs_dim=%d)",
            clips_.size(), fps, obsDim_);

    for (size_t ci = 0; ci < clips_.size(); ++ci) {
        const auto& clip = clips_[ci];
        int numFrames = std::max(2, static_cast<int>(clip.duration * fps) + 1);

        // Sample all frames of this clip into MotionFrames
        std::vector<MotionFrame> frames(numFrames);
        for (int fi = 0; fi < numFrames; ++fi) {
            float time = static_cast<float>(fi) * frameTime;
            time = std::min(time, clip.duration);
            frames[fi] = sampleFrame(static_cast<int>(ci), time, skeleton);
        }

        // Compute observations for the entire clip
        cachedObs_[ci].numFrames = numFrames;
        cachedObs_[ci].data = obsComputer.computeClip(frames, fps);
        totalObsFrames_ += numFrames;
    }

    SDL_Log("MotionLibrary: Precomputed %d observation frames across %zu clips",
            totalObsFrames_, clips_.size());
}

void MotionLibrary::sampleObservations(int batchSize, std::mt19937& rng,
                                        float* outBuffer) const {
    if (cachedObs_.empty() || totalObsFrames_ == 0) {
        std::fill(outBuffer, outBuffer + batchSize * obsDim_, 0.0f);
        return;
    }

    for (int b = 0; b < batchSize; ++b) {
        // Pick a random clip weighted by frame count
        std::uniform_int_distribution<int> frameDist(0, totalObsFrames_ - 1);
        int globalFrame = frameDist(rng);

        int cumFrames = 0;
        size_t clipIdx = 0;
        for (size_t ci = 0; ci < cachedObs_.size(); ++ci) {
            cumFrames += cachedObs_[ci].numFrames;
            if (globalFrame < cumFrames) {
                clipIdx = ci;
                break;
            }
        }

        // Random frame within the selected clip
        std::uniform_int_distribution<int> localDist(0, cachedObs_[clipIdx].numFrames - 1);
        int frameIdx = localDist(rng);

        const float* src = cachedObs_[clipIdx].data.data() + frameIdx * obsDim_;
        std::copy(src, src + obsDim_, outBuffer + b * obsDim_);
    }
}

MotionFrame MotionLibrary::poseToMotionFrame(
    const Skeleton& skeleton,
    const std::vector<glm::mat4>& globalTransforms,
    int rootBoneIndex)
{
    MotionFrame frame;

    // Extract root transform
    if (rootBoneIndex >= 0 && rootBoneIndex < static_cast<int>(globalTransforms.size())) {
        const glm::mat4& rootGlobal = globalTransforms[rootBoneIndex];
        frame.rootPosition = glm::vec3(rootGlobal[3]);

        // Extract rotation from the global transform matrix
        glm::vec3 scale;
        scale.x = glm::length(glm::vec3(rootGlobal[0]));
        scale.y = glm::length(glm::vec3(rootGlobal[1]));
        scale.z = glm::length(glm::vec3(rootGlobal[2]));

        glm::mat3 rotMat(
            glm::vec3(rootGlobal[0]) / scale.x,
            glm::vec3(rootGlobal[1]) / scale.y,
            glm::vec3(rootGlobal[2]) / scale.z
        );
        frame.rootRotation = glm::quat_cast(rotMat);
    } else if (!globalTransforms.empty()) {
        // Fallback to first joint
        frame.rootPosition = glm::vec3(globalTransforms[0][3]);
        frame.rootRotation = glm::quat_cast(glm::mat3(globalTransforms[0]));
    }

    // Extract per-joint data
    size_t numJoints = skeleton.joints.size();
    frame.jointRotations.resize(numJoints);
    frame.jointPositions.resize(numJoints);

    for (size_t i = 0; i < numJoints; ++i) {
        // Local rotation: decompose from skeleton's local transform
        const glm::mat4& local = skeleton.joints[i].localTransform;
        glm::vec3 s;
        s.x = glm::length(glm::vec3(local[0]));
        s.y = glm::length(glm::vec3(local[1]));
        s.z = glm::length(glm::vec3(local[2]));

        glm::mat3 rotMat(
            glm::vec3(local[0]) / s.x,
            glm::vec3(local[1]) / s.y,
            glm::vec3(local[2]) / s.z
        );
        frame.jointRotations[i] = glm::quat_cast(rotMat);

        // Global position from FK
        if (i < globalTransforms.size()) {
            frame.jointPositions[i] = glm::vec3(globalTransforms[i][3]);
        }
    }

    return frame;
}

} // namespace training
