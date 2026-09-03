#include "TripleBuffering.h"
#include <SDL3/SDL_log.h>

std::optional<TripleBuffering> TripleBuffering::create(const vk::raii::Device& device,
                                                       uint32_t frameCount) {
    TripleBuffering frames(device, frameCount);
    if (!frames.isInitialized()) {
        return std::nullopt;
    }
    return std::optional<TripleBuffering>(std::move(frames));
}

TripleBuffering::TripleBuffering(const vk::raii::Device& device, uint32_t frameCount) {
    if (frameCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: frameCount must be > 0");
        return;
    }

    if (frameCount > frameSignalValues_.size()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: frameCount %u exceeds maximum %zu",
            frameCount, frameSignalValues_.size());
        return;
    }

    // Store device pointer for operations
    device_ = &device;

    // Create timeline semaphore for frame completion tracking (Vulkan 1.2)
    try {
        auto typeInfo = vk::SemaphoreTypeCreateInfo{}
            .setSemaphoreType(vk::SemaphoreType::eTimeline)
            .setInitialValue(0);

        auto createInfo = vk::SemaphoreCreateInfo{}
            .setPNext(&typeInfo);

        frameTimeline_.emplace(device, createInfo);
        SDL_Log("TripleBuffering: Created timeline semaphore for frame sync");
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: failed to create timeline semaphore: %s", e.what());
        clearSync();
        return;
    }

    // Initialize frame signal values to 0 (all frames considered complete initially)
    frameSignalValues_.fill(0);
    globalFrameCounter_ = 0;

    // Use FrameBuffered's resize with a factory function for binary semaphores
    try {
        frames_.resize(frameCount, [&device](uint32_t i) -> FrameSyncPrimitives {
            FrameSyncPrimitives primitives;

            try {
                // Binary semaphore for swapchain acquire (per frame-in-flight slot).
                // The present-wait (render-finished) semaphore is per swapchain
                // image instead — see initPresentSemaphores().
                primitives.imageAvailable.emplace(device, vk::SemaphoreCreateInfo{});
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "TripleBuffering::init: failed to create semaphores for frame %u: %s", i, e.what());
            }

            return primitives;
        });
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: exception during resize: %s", e.what());
        clearSync();
        return;
    }

    // Verify all primitives were created successfully
    for (uint32_t i = 0; i < frameCount; ++i) {
        const auto& p = frames_[i];
        if (!p.imageAvailable) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: incomplete primitives for frame %u", i);
            clearSync();
            return;
        }
    }

    SDL_Log("TripleBuffering: initialized with %u frames in flight (timeline semaphore sync)", frameCount);
}

bool TripleBuffering::initPresentSemaphores(uint32_t imageCount) {
    if (!device_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::initPresentSemaphores: called before init()");
        return false;
    }
    if (imageCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::initPresentSemaphores: imageCount must be > 0");
        return false;
    }

    // Recreate from scratch; RAII destroys any previous semaphores. Callers must
    // ensure the device is idle (e.g. after a swapchain recreate) before resizing
    // this set, since outstanding presents may still reference the old semaphores.
    presentSemaphores_.clear();
    presentSemaphores_.reserve(imageCount);
    try {
        for (uint32_t i = 0; i < imageCount; ++i) {
            presentSemaphores_.emplace_back(*device_, vk::SemaphoreCreateInfo{});
        }
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::initPresentSemaphores: failed to create semaphores: %s", e.what());
        presentSemaphores_.clear();
        return false;
    }

    SDL_Log("TripleBuffering: created %u per-image present-wait semaphores", imageCount);
    return true;
}

void TripleBuffering::clearSync() {
    // RAII handles cleanup - just clear containers and reset pointers
    frames_.clear();
    presentSemaphores_.clear();
    frameTimeline_.reset();
    frameSignalValues_.fill(0);
    globalFrameCounter_ = 0;
    device_ = nullptr;
}

uint64_t TripleBuffering::getTimelineCounterValue() const {
    if (!frameTimeline_ || !device_) {
        return 0;
    }

    // vkGetSemaphoreCounterValue is non-blocking - call on semaphore object
    return frameTimeline_->getCounterValue();
}

void TripleBuffering::waitForCurrentFrame() const {
    if (!frameTimeline_ || !device_) {
        return;
    }

    uint64_t waitValue = currentFrameWaitValue();
    if (waitValue == 0) {
        // Frame was never submitted, no need to wait
        return;
    }

    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(**frameTimeline_)
        .setValues(waitValue);

    (void)device_->waitSemaphores(waitInfo, UINT64_MAX);
}

void TripleBuffering::waitForPreviousFrame() const {
    if (!frameTimeline_ || !device_) {
        return;
    }

    uint64_t waitValue = frameSignalValues_[frames_.previousIndex()];
    if (waitValue == 0) {
        // Previous frame was never submitted, no need to wait
        return;
    }

    // Check if already complete (non-blocking optimization)
    if (getTimelineCounterValue() >= waitValue) {
        return;
    }

    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(**frameTimeline_)
        .setValues(waitValue);

    (void)device_->waitSemaphores(waitInfo, UINT64_MAX);
}

void TripleBuffering::waitForAllFrames() const {
    if (!frameTimeline_ || !device_ || globalFrameCounter_ == 0) {
        return;
    }

    auto waitInfo = vk::SemaphoreWaitInfo{}
        .setSemaphores(**frameTimeline_)
        .setValues(globalFrameCounter_);

    (void)device_->waitSemaphores(waitInfo, UINT64_MAX);
}

vk::TimelineSemaphoreSubmitInfo TripleBuffering::createTimelineSubmitInfo(
    uint64_t signalValue,
    const uint64_t* waitValues,
    uint32_t waitCount) const
{
    // Note: The caller must ensure signalValues array stays valid during submit
    return vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(waitCount)
        .setPWaitSemaphoreValues(waitValues)
        .setSignalSemaphoreValueCount(1)
        .setPSignalSemaphoreValues(&signalValue);
}
