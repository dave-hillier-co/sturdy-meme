#include "MotionMatchingController.h"
#include "Animation.h"
#include "GLTFLoader.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace MotionMatching {

void MotionMatchingController::initialize(const ControllerConfig& config) {
    config_ = config;
    trajectoryPredictor_.setConfig(config.trajectoryConfig);
    inertialBlender_.setConfig({config.defaultBlendDuration});
    matcher_.setDatabase(&database_);

    initialized_ = true;

    SDL_Log("MotionMatchingController: Initialized");
}

void MotionMatchingController::setSkeleton(const Skeleton& skeleton) {
    database_.initialize(skeleton, config_.featureConfig);
    featureExtractor_.initialize(skeleton, config_.featureConfig);

    // Initialize pose storage
    currentPose_.resize(skeleton.joints.size());
    previousPose_.resize(skeleton.joints.size());

    SDL_Log("MotionMatchingController: Skeleton set with %zu joints",
            skeleton.joints.size());
}

void MotionMatchingController::addClip(const AnimationClip* clip,
                                         const std::string& name,
                                         bool looping,
                                         const std::vector<std::string>& tags) {
    database_.addClip(clip, name, looping, 30.0f, tags);
}

void MotionMatchingController::buildDatabase(const DatabaseBuildOptions& options) {
    database_.build(options);

    // Start with first pose if available
    if (database_.getPoseCount() > 0) {
        const DatabasePose& firstPose = database_.getPose(0);
        playback_.clipIndex = firstPose.clipIndex;
        playback_.time = firstPose.time;
        playback_.matchedPoseIndex = 0;

        updatePose();
    }

    SDL_Log("MotionMatchingController: Database built with %zu poses",
            database_.getPoseCount());
}

void MotionMatchingController::update(const glm::vec3& position,
                                        const glm::vec3& facing,
                                        const glm::vec3& inputDirection,
                                        float inputMagnitude,
                                        float deltaTime) {
    if (!initialized_ || !database_.isBuilt()) {
        return;
    }

    // Update trajectory predictor
    trajectoryPredictor_.update(position, facing, inputDirection, inputMagnitude, deltaTime);

    // Update inertial blender
    if (config_.useInertialBlending) {
        inertialBlender_.update(deltaTime);
    }

    // Advance current playback
    advancePlayback(deltaTime);

    // Update pose from current playback
    updatePose();

    // Extract query features from current state
    extractQueryFeatures();

    // Update search timing
    timeSinceLastSearch_ += deltaTime;
    playback_.timeSinceMatch += deltaTime;

    // Update stats timing
    matchCountTimer_ += deltaTime;
    if (matchCountTimer_ >= 1.0f) {
        stats_.matchesThisSecond = matchCountThisSecond_;
        matchCountThisSecond_ = 0;
        matchCountTimer_ = 0.0f;
    }

    // Check if we need to search for a new pose
    bool shouldSearch = forceSearchNextUpdate_ ||
                        (timeSinceLastSearch_ >= config_.searchInterval);

    // Also search if trajectory has changed significantly
    if (!shouldSearch && database_.getPoseCount() > 0) {
        const DatabasePose& currentMatchedPose = database_.getPose(playback_.matchedPoseIndex);
        float currentCost = queryTrajectory_.computeCost(
            currentMatchedPose.trajectory,
            config_.featureConfig.trajectoryPositionWeight,
            config_.featureConfig.trajectoryVelocityWeight,
            config_.featureConfig.trajectoryFacingWeight
        );
        if (currentCost > config_.forceSearchThreshold) {
            shouldSearch = true;
        }
    }

    if (shouldSearch) {
        performSearch();
        forceSearchNextUpdate_ = false;
        timeSinceLastSearch_ = 0.0f;
    }
}

void MotionMatchingController::performSearch() {
    // Generate query trajectory
    queryTrajectory_ = trajectoryPredictor_.generateTrajectory();

    // Set current pose in search options for continuity
    SearchOptions options = config_.searchOptions;
    options.currentPoseIndex = playback_.matchedPoseIndex;

    // Perform search
    MatchResult match = matcher_.findBestMatch(queryTrajectory_, queryPose_, options);

    if (match.isValid()) {
        // Check if this is a different pose than current
        bool isDifferentPose = (match.poseIndex != playback_.matchedPoseIndex);

        // Only transition if cost is significantly better or we've been on same pose too long
        bool shouldTransition = isDifferentPose &&
                               (match.cost < stats_.lastMatchCost * 0.8f ||
                                playback_.timeSinceMatch > 0.5f);

        if (shouldTransition || playback_.timeSinceMatch > 1.0f) {
            transitionToPose(match);
            ++matchCountThisSecond_;

            if (config_.onPoseMatched) {
                config_.onPoseMatched(match);
            }
        }

        // Update stats
        stats_.lastMatchCost = match.cost;
        stats_.lastTrajectoryCost = match.trajectoryCost;
        stats_.lastPoseCost = match.poseCost;
        stats_.posesSearched = database_.getPoseCount();
    }
}

void MotionMatchingController::transitionToPose(const MatchResult& match) {
    // Store previous pose for blending
    previousPose_ = currentPose_;

    // Update playback state
    playback_.clipIndex = match.pose->clipIndex;
    playback_.time = match.pose->time;
    playback_.normalizedTime = match.pose->normalizedTime;
    playback_.matchedPoseIndex = match.poseIndex;
    playback_.timeSinceMatch = 0.0f;

    // Update stats
    stats_.currentClipName = match.clip->name;
    stats_.currentClipTime = match.pose->time;

    // Start inertial blend if enabled
    if (config_.useInertialBlending && previousPose_.size() > 0) {
        // Get root position from previous and new pose
        if (currentPose_.size() > 0 && previousPose_.size() > 0) {
            glm::vec3 prevRootPos = previousPose_[0].translation;
            glm::vec3 newRootPos = currentPose_[0].translation;
            glm::vec3 prevRootVel = queryPose_.rootVelocity;
            glm::vec3 newRootVel = match.pose->poseFeatures.rootVelocity;

            inertialBlender_.startBlend(prevRootPos, prevRootVel, newRootPos, newRootVel);
        }
    }

    // Update the current pose immediately
    updatePose();
}

void MotionMatchingController::advancePlayback(float deltaTime) {
    if (!playback_.isPlaying || database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Advance time
    playback_.time += deltaTime;

    // Handle looping
    if (clip.looping) {
        while (playback_.time >= clip.duration) {
            playback_.time -= clip.duration;
        }
    } else {
        playback_.time = std::min(playback_.time, clip.duration);
    }

    // Update normalized time
    if (clip.duration > 0.0f) {
        playback_.normalizedTime = playback_.time / clip.duration;
    }

    // Update stats
    stats_.currentClipTime = playback_.time;
}

void MotionMatchingController::updatePose() {
    if (database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Sample animation at current time
    Skeleton tempSkeleton = database_.getSkeleton();
    clip.clip->sample(playback_.time, tempSkeleton, true);

    // Convert to SkeletonPose
    for (size_t i = 0; i < tempSkeleton.joints.size() && i < currentPose_.size(); ++i) {
        currentPose_[i] = BonePose::fromMatrix(
            tempSkeleton.joints[i].localTransform,
            tempSkeleton.joints[i].preRotation
        );
    }
}

void MotionMatchingController::extractQueryFeatures() {
    if (database_.getClipCount() == 0) {
        return;
    }

    const DatabaseClip& clip = database_.getClip(playback_.clipIndex);
    if (!clip.clip) {
        return;
    }

    // Extract features from current pose
    queryPose_ = featureExtractor_.extractFromClip(
        *clip.clip,
        database_.getSkeleton(),
        playback_.time
    );

    // Update root velocity from trajectory predictor
    queryPose_.rootVelocity = trajectoryPredictor_.getCurrentVelocity();
}

void MotionMatchingController::applyToSkeleton(Skeleton& skeleton) const {
    if (currentPose_.size() == 0) {
        return;
    }

    // Apply current pose to skeleton
    for (size_t i = 0; i < skeleton.joints.size() && i < currentPose_.size(); ++i) {
        skeleton.joints[i].localTransform = currentPose_[i].toMatrix(
            skeleton.joints[i].preRotation
        );
    }

    // Apply inertial blending offset to root
    if (config_.useInertialBlending && inertialBlender_.isBlending() && skeleton.joints.size() > 0) {
        glm::vec3 offset = inertialBlender_.getPositionOffset();
        skeleton.joints[0].localTransform[3] += glm::vec4(offset, 0.0f);
    }
}

void MotionMatchingController::getCurrentPose(SkeletonPose& outPose) const {
    outPose = currentPose_;

    // Apply inertial blending offset
    if (config_.useInertialBlending && inertialBlender_.isBlending() && outPose.size() > 0) {
        outPose[0].translation += inertialBlender_.getPositionOffset();
    }
}

void MotionMatchingController::setRequiredTags(const std::vector<std::string>& tags) {
    config_.searchOptions.requiredTags = tags;
}

void MotionMatchingController::setExcludedTags(const std::vector<std::string>& tags) {
    config_.searchOptions.excludedTags = tags;
}

const Trajectory& MotionMatchingController::getLastMatchedTrajectory() const {
    static Trajectory empty;

    if (database_.getPoseCount() == 0) {
        return empty;
    }

    return database_.getPose(playback_.matchedPoseIndex).trajectory;
}

} // namespace MotionMatching
