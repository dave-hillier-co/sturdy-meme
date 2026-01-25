#include "GlobalBufferManager.h"
#include "VulkanContext.h"

std::unique_ptr<GlobalBufferManager> GlobalBufferManager::createWithDependencies(
    VulkanContext& vulkanContext,
    uint32_t frameCount,
    uint32_t maxBones) {
    return GlobalBufferManager::create(
        vulkanContext.getAllocator(),
        vulkanContext.getVkPhysicalDevice(),
        frameCount,
        maxBones);
}
