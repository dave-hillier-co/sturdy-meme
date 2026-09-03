#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstring>
#include <memory>

#include "PerFrameBuffer.h"
#include "DynamicUniformBuffer.h"
#include "vulkan/VmaBuffer.h"
#include "Light.h"
#include "UBOs.h"

/**
 * GlobalBufferManager - Manages per-frame shared GPU buffers
 *
 * Consolidates uniform buffer, light buffer (SSBO), and bone matrices
 * buffer management that was scattered throughout Renderer.
 *
 * Uses the existing BufferUtils patterns for consistency.
 *
 * Usage:
 *   auto buffers = GlobalBufferManager::create(allocator, physicalDevice, frameCount);
 *   if (!buffers) { handle error; }
 */
class GlobalBufferManager {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GlobalBufferManager(ConstructToken) {}

    /**
     * Factory: Create and initialize buffer manager.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GlobalBufferManager> create(VmaAllocator allocator, vk::PhysicalDevice physicalDevice,
                                                        uint32_t frameCount, uint32_t maxBones = 128) {
        auto manager = std::make_unique<GlobalBufferManager>(ConstructToken{});
        if (!manager->initInternal(allocator, physicalDevice, frameCount, maxBones)) {
            return nullptr;
        }
        return manager;
    }

    // The owning VmaBuffers (declared below, before the public views) free
    // every buffer; the views are plain handles.
    ~GlobalBufferManager() = default;

    // Non-copyable, non-movable (stored via unique_ptr)
    GlobalBufferManager(GlobalBufferManager&&) = delete;
    GlobalBufferManager& operator=(GlobalBufferManager&&) = delete;
    GlobalBufferManager(const GlobalBufferManager&) = delete;
    GlobalBufferManager& operator=(const GlobalBufferManager&) = delete;

private:
    // Owners. The public sets below are non-owning views over these so the
    // readers of .buffers/.mappedPointers stay unchanged.
    std::vector<VmaBuffer> uniformBufferOwners_;
    VmaBuffer dynamicRendererUBOOwner_;
    std::vector<VmaBuffer> lightBufferOwners_;
    std::vector<VmaBuffer> boneMatricesBufferOwners_;
    std::vector<VmaBuffer> snowBufferOwners_;
    std::vector<VmaBuffer> cloudShadowBufferOwners_;

public:
    // Per-frame buffer sets (public for descriptor binding; non-owning views)
    BufferUtils::PerFrameBufferSet uniformBuffers;
    BufferUtils::PerFrameBufferSet lightBuffers;
    BufferUtils::PerFrameBufferSet boneMatricesBuffers;
    BufferUtils::PerFrameBufferSet snowBuffers;         // Snow UBO (binding 14)
    BufferUtils::PerFrameBufferSet cloudShadowBuffers;  // Cloud shadow UBO (binding 15)

    // Dynamic uniform buffer for renderer UBO - use with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
    // to avoid per-frame descriptor set updates in vegetation/weather systems
    BufferUtils::DynamicUniformBuffer dynamicRendererUBO;

    // Configuration accessors
    uint32_t getFramesInFlight() const { return framesInFlight_; }
    uint32_t getMaxBoneMatrices() const { return maxBoneMatrices_; }

    // Buffer accessors - Return raw vk::Buffer vectors (wrapped by BufferUtils::PerFrameBufferSet)
    const std::vector<vk::Buffer>& getUniformBuffers() const { return uniformBuffers.buffers; }
    size_t getUniformBufferSize() const { return sizeof(UniformBufferObject); }

    const std::vector<vk::Buffer>& getLightBuffers() const { return lightBuffers.buffers; }
    size_t getLightBufferSize() const { return sizeof(LightBuffer); }

    // Extended buffer accessors
    const std::vector<vk::Buffer>& getSnowBuffers() const { return snowBuffers.buffers; }
    size_t getSnowBufferSize() const { return sizeof(SnowUBO); }

    const std::vector<vk::Buffer>& getCloudShadowBuffers() const { return cloudShadowBuffers.buffers; }
    size_t getCloudShadowBufferSize() const { return sizeof(CloudShadowUBO); }

private:
    // Take ownership of a freshly built per-frame set. The set keeps its
    // handles and mapped pointers as a view; the owners free the buffers
    // (the persistently mapped allocations need no explicit unmap).
    static std::vector<VmaBuffer> adopt(VmaAllocator allocator, const BufferUtils::PerFrameBufferSet& set) {
        std::vector<VmaBuffer> owners;
        owners.reserve(set.buffers.size());
        for (size_t i = 0; i < set.buffers.size(); ++i) {
            owners.push_back(VmaBuffer::fromRaw(allocator, set.buffers[i], set.allocations[i]));
        }
        return owners;
    }

    static bool buildPerFrame(VmaAllocator allocator, uint32_t frameCount, vk::DeviceSize size,
                              vk::BufferUsageFlags usage, BufferUtils::PerFrameBufferSet& outSet,
                              std::vector<VmaBuffer>& outOwners) {
        if (!BufferUtils::PerFrameBufferBuilder()
                .setAllocator(allocator)
                .setFrameCount(frameCount)
                .setSize(size)
                .setUsage(static_cast<VkBufferUsageFlags>(usage))
                .build(outSet)) {
            return false;
        }
        outOwners = adopt(allocator, outSet);
        return true;
    }

    bool initInternal(VmaAllocator allocator, vk::PhysicalDevice physicalDevice,
                      uint32_t frameCount, uint32_t maxBones) {
        framesInFlight_ = frameCount;
        maxBoneMatrices_ = maxBones;

        // On any early return the owners built so far free their buffers.

        // Create uniform buffers
        if (!buildPerFrame(allocator, frameCount, sizeof(UniformBufferObject),
                           vk::BufferUsageFlagBits::eUniformBuffer, uniformBuffers, uniformBufferOwners_)) {
            return false;
        }

        // Create dynamic renderer UBO for vegetation/weather systems
        // This avoids per-frame descriptor set updates by using dynamic offsets
        if (!BufferUtils::DynamicUniformBufferBuilder()
                .setAllocator(allocator)
                .setPhysicalDevice(physicalDevice)
                .setFrameCount(frameCount)
                .setElementSize(sizeof(UniformBufferObject))
                .build(dynamicRendererUBO)) {
            return false;
        }
        dynamicRendererUBOOwner_ = VmaBuffer::fromRaw(allocator, dynamicRendererUBO.buffer,
                                                      dynamicRendererUBO.allocation);

        // Create light buffers (SSBO)
        if (!buildPerFrame(allocator, frameCount, sizeof(LightBuffer),
                           vk::BufferUsageFlagBits::eStorageBuffer, lightBuffers, lightBufferOwners_)) {
            return false;
        }

        // Create bone matrices buffers
        if (!buildPerFrame(allocator, frameCount, sizeof(glm::mat4) * maxBones,
                           vk::BufferUsageFlagBits::eStorageBuffer, boneMatricesBuffers, boneMatricesBufferOwners_)) {
            return false;
        }

        // Create snow UBO buffers (binding 14)
        if (!buildPerFrame(allocator, frameCount, sizeof(SnowUBO),
                           vk::BufferUsageFlagBits::eUniformBuffer, snowBuffers, snowBufferOwners_)) {
            return false;
        }

        // Create cloud shadow UBO buffers (binding 15)
        if (!buildPerFrame(allocator, frameCount, sizeof(CloudShadowUBO),
                           vk::BufferUsageFlagBits::eUniformBuffer, cloudShadowBuffers, cloudShadowBufferOwners_)) {
            return false;
        }

        return true;
    }

public:
    // Update the main UBO for a frame (updates both regular and dynamic buffers)
    void updateUniformBuffer(uint32_t frameIndex, const UniformBufferObject& ubo) {
        if (frameIndex < uniformBuffers.mappedPointers.size()) {
            std::memcpy(uniformBuffers.mappedPointers[frameIndex], &ubo, sizeof(ubo));
        }
        // Also update the dynamic UBO used by vegetation/weather systems
        if (dynamicRendererUBO.isValid()) {
            void* ptr = dynamicRendererUBO.getMappedPtr(frameIndex);
            if (ptr) {
                std::memcpy(ptr, &ubo, sizeof(ubo));
            }
        }
    }

    // Update light buffer for a frame
    void updateLightBuffer(uint32_t frameIndex, const LightBuffer& buffer) {
        if (frameIndex < lightBuffers.mappedPointers.size()) {
            std::memcpy(lightBuffers.mappedPointers[frameIndex], &buffer, sizeof(buffer));
        }
    }

    // Update bone matrices for a frame
    void updateBoneMatrices(uint32_t frameIndex, const std::vector<glm::mat4>& matrices) {
        if (frameIndex < boneMatricesBuffers.mappedPointers.size() && !matrices.empty()) {
            size_t copySize = std::min(matrices.size(), static_cast<size_t>(maxBoneMatrices_)) * sizeof(glm::mat4);
            std::memcpy(boneMatricesBuffers.mappedPointers[frameIndex], matrices.data(), copySize);
        }
    }

    // Update snow UBO for a frame
    void updateSnowBuffer(uint32_t frameIndex, const SnowUBO& snowUbo) {
        if (frameIndex < snowBuffers.mappedPointers.size()) {
            std::memcpy(snowBuffers.mappedPointers[frameIndex], &snowUbo, sizeof(snowUbo));
        }
    }

    // Update cloud shadow UBO for a frame
    void updateCloudShadowBuffer(uint32_t frameIndex, const CloudShadowUBO& cloudShadowUbo) {
        if (frameIndex < cloudShadowBuffers.mappedPointers.size()) {
            std::memcpy(cloudShadowBuffers.mappedPointers[frameIndex], &cloudShadowUbo, sizeof(cloudShadowUbo));
        }
    }

    // Descriptor buffer info accessors
    VkDescriptorBufferInfo getUniformBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < uniformBuffers.buffers.size()) {
            info.buffer = uniformBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(UniformBufferObject);
        }
        return info;
    }

    // Dynamic renderer UBO accessors (for vegetation/weather systems using dynamic uniform buffers)
    const BufferUtils::DynamicUniformBuffer& getDynamicRendererUBO() const {
        return dynamicRendererUBO;
    }

    // Get descriptor buffer info for dynamic UBO (use with VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
    // Write this to descriptor set once, then use getDynamicOffset() at bind time
    VkDescriptorBufferInfo getDynamicUniformBufferInfo() const {
        VkDescriptorBufferInfo info{};
        if (dynamicRendererUBO.isValid()) {
            info.buffer = dynamicRendererUBO.buffer;
            info.offset = 0;
            info.range = dynamicRendererUBO.alignedSize;  // One element's aligned size
        }
        return info;
    }

    // Get dynamic offset for a specific frame (use at vkCmdBindDescriptorSets time)
    uint32_t getDynamicOffset(uint32_t frameIndex) const {
        return dynamicRendererUBO.getDynamicOffset(frameIndex);
    }

    VkDescriptorBufferInfo getLightBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < lightBuffers.buffers.size()) {
            info.buffer = lightBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(LightBuffer);
        }
        return info;
    }

    VkDescriptorBufferInfo getBoneMatricesBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < boneMatricesBuffers.buffers.size()) {
            info.buffer = boneMatricesBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(glm::mat4) * maxBoneMatrices_;
        }
        return info;
    }

    VkDescriptorBufferInfo getSnowBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < snowBuffers.buffers.size()) {
            info.buffer = snowBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(SnowUBO);
        }
        return info;
    }

    VkDescriptorBufferInfo getCloudShadowBufferInfo(uint32_t frameIndex) const {
        VkDescriptorBufferInfo info{};
        if (frameIndex < cloudShadowBuffers.buffers.size()) {
            info.buffer = cloudShadowBuffers.buffers[frameIndex];
            info.offset = 0;
            info.range = sizeof(CloudShadowUBO);
        }
        return info;
    }

private:
    uint32_t framesInFlight_ = 0;
    uint32_t maxBoneMatrices_ = 128;
};
