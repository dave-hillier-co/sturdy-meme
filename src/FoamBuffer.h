#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>

/**
 * FoamBuffer - Phase 14: Temporal Foam Persistence (Sea of Thieves)
 *
 * Implements persistent foam that fades over time:
 * - Foam render target that persists between frames
 * - Progressive blur to simulate foam dissipation
 * - Advection using flow map
 * - Sharp foam at wave crests, gradual fade
 *
 * Based on Sea of Thieves GDC 2018 talk.
 */
class FoamBuffer {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        uint32_t resolution = 512;      // Foam buffer resolution
        float worldSize = 16384.0f;     // World size covered by foam buffer
    };

    // Push constants for compute shader
    struct FoamPushConstants {
        glm::vec4 worldExtent;    // xy = center, zw = size
        float deltaTime;
        float blurStrength;       // How much to blur each frame
        float decayRate;          // How fast foam fades
        float injectionStrength;  // Strength of new foam injection
    };

    FoamBuffer() = default;
    ~FoamBuffer() = default;

    bool init(const InitInfo& info);
    void destroy();

    // Record compute shader dispatch for blur/decay
    // Call this each frame before water rendering
    void recordCompute(VkCommandBuffer cmd, uint32_t frameIndex, float deltaTime,
                       VkImageView flowMapView, VkSampler flowMapSampler);

    // Get foam buffer for sampling in water shader
    VkImageView getFoamBufferView() const { return foamBufferView[currentBuffer]; }
    VkSampler getSampler() const { return sampler; }

    // Configuration
    void setWorldExtent(const glm::vec2& center, const glm::vec2& size);
    void setBlurStrength(float strength) { blurStrength = strength; }
    void setDecayRate(float rate) { decayRate = rate; }
    void setInjectionStrength(float strength) { injectionStrength = strength; }

    float getBlurStrength() const { return blurStrength; }
    float getDecayRate() const { return decayRate; }
    float getInjectionStrength() const { return injectionStrength; }

    // Clear foam buffer
    void clear(VkCommandBuffer cmd);

private:
    bool createFoamBuffers();
    bool createComputePipeline();
    bool createDescriptorSets();

    // Device handles
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkQueue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;

    // Configuration
    uint32_t framesInFlight = 0;
    uint32_t resolution = 512;
    float worldSize = 16384.0f;
    glm::vec2 worldCenter = glm::vec2(0.0f);

    // Foam parameters
    float blurStrength = 0.02f;      // Subtle blur per frame
    float decayRate = 0.5f;          // Foam decay per second
    float injectionStrength = 1.0f;  // New foam injection strength

    // Double-buffered foam maps (ping-pong for blur)
    // R16F format - single channel foam intensity
    VkImage foamBuffer[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkImageView foamBufferView[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VmaAllocation foamAllocation[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    int currentBuffer = 0;  // Which buffer to read from

    // Sampler
    VkSampler sampler = VK_NULL_HANDLE;

    // Compute pipeline
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout computePipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;
};
