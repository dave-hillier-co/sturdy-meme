#include "RenderingInfrastructure.h"
#include "VulkanContext.h"
#include <SDL3/SDL.h>

RenderingInfrastructure::~RenderingInfrastructure() {
    if (initialized_) {
        shutdown();
    }
}

bool RenderingInfrastructure::init(VulkanContext& context, uint32_t threadCount) {
    // Initialize async transfer manager for non-blocking GPU uploads
    if (!asyncTransferManager_.initialize(context)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager initialization failed - using synchronous transfers");
        // Continue - not a fatal error
    }

    // Initialize threaded command pool for parallel command recording
    if (threadCount > 0) {
        if (!threadedCommandPool_.initialize(context, threadCount + 1)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "ThreadedCommandPool initialization failed - using single-threaded recording");
            // Continue - not a fatal error
        }
    }

    // Initialize async texture uploader for non-blocking texture uploads
    if (!asyncTextureUploader_.initialize(
            context.getVkDevice(),
            context.getAllocator(),
            &asyncTransferManager_)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTextureUploader initialization failed - using synchronous uploads");
        // Continue - not a fatal error
    }

    // PassScheduler is initialized empty and populated later by PassSchedulerBuilder
    // AssetRegistry is initialized separately via initAssetRegistry()

    initialized_ = true;
    return true;
}

void RenderingInfrastructure::initAssetRegistry(vk::Device device, vk::PhysicalDevice physicalDevice,
                                                 VmaAllocator allocator, vk::CommandPool commandPool,
                                                 vk::Queue graphicsQueue) {
    assetRegistry_.init(device, physicalDevice, allocator, commandPool, graphicsQueue);
}

void RenderingInfrastructure::shutdown() {
    if (!initialized_) return;

    // Shutdown in reverse initialization order
    asyncTextureUploader_.shutdown();  // Must shutdown before transfer manager
    asyncTransferManager_.shutdown();
    threadedCommandPool_.shutdown();
    // PassScheduler has no explicit shutdown
    // AssetRegistry cleanup is automatic (RAII)

    initialized_ = false;
}
