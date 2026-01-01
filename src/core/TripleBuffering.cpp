#include "TripleBuffering.h"
#include <SDL3/SDL_log.h>

bool TripleBuffering::init(const vk::raii::Device& device, uint32_t frameCount) {
    if (frameCount == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: frameCount must be > 0");
        return false;
    }

    destroy();

    // Store device pointer for fence operations
    device_ = &device;

    // Use FrameBuffered's resize with a factory function
    try {
        frames_.resize(frameCount, [&device](uint32_t i) -> FrameSyncPrimitives {
            FrameSyncPrimitives primitives;

            try {
                primitives.imageAvailable.emplace(device, vk::SemaphoreCreateInfo{});
                primitives.renderFinished.emplace(device, vk::SemaphoreCreateInfo{});

                // Create fences in signaled state so first frame doesn't wait forever
                auto fenceInfo = vk::FenceCreateInfo{}
                    .setFlags(vk::FenceCreateFlagBits::eSignaled);
                primitives.inFlightFence.emplace(device, fenceInfo);
            } catch (const vk::SystemError& e) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "TripleBuffering::init: failed to create sync primitives for frame %u: %s", i, e.what());
            }

            return primitives;
        });
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "TripleBuffering::init: exception during resize: %s", e.what());
        destroy();
        return false;
    }

    // Verify all primitives were created successfully
    for (uint32_t i = 0; i < frameCount; ++i) {
        const auto& p = frames_[i];
        if (!p.imageAvailable || !p.renderFinished || !p.inFlightFence) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "TripleBuffering::init: incomplete primitives for frame %u", i);
            destroy();
            return false;
        }
    }

    SDL_Log("TripleBuffering: initialized with %u frames in flight", frameCount);
    return true;
}

void TripleBuffering::destroy() {
    // RAII handles cleanup - just clear the FrameBuffered container
    frames_.clear();
    device_ = nullptr;
}
