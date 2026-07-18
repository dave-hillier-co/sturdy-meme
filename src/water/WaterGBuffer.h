#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>

#include "DescriptorManager.h"
#include "VmaImage.h"

/**
 * WaterGBuffer - Phase 3: Screen-Space Mini G-Buffer
 *
 * Stores per-pixel water data for deferred water compositing:
 * - Data texture: shader ID, material index, LOD level, foam amount
 * - Mesh normal texture: low-res mesh normals
 * - Water-only depth buffer (separate from scene depth)
 *
 * Based on Far Cry 5's water rendering approach (GDC 2018).
 */
class WaterGBuffer {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit WaterGBuffer(ConstructToken) {}

    struct InitInfo {
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkExtent2D fullResExtent;       // Full screen resolution
        float resolutionScale = 0.5f;   // G-buffer resolution relative to full res
        uint32_t framesInFlight;
        std::string shaderPath;         // Path to shader SPV files
        DescriptorManager::Pool* descriptorPool; // For allocating descriptor sets
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // G-buffer data packed into textures
    // Data texture (RGBA8):
    //   R: Shader/material ID (0-255)
    //   G: LOD level (0-255, maps to 0.0-1.0)
    //   B: Foam amount (0-255, maps to 0.0-1.0)
    //   A: Reserved (blend material ID, etc.)
    //
    // Normal texture (RGBA16F):
    //   RGB: Mesh normal (world space)
    //   A: Water depth (for refraction)
    //
    // Depth texture (D32F):
    //   Water-only depth for proper compositing

    /**
     * Factory: Create and initialize WaterGBuffer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<WaterGBuffer> create(const InitInfo& info);


    ~WaterGBuffer();

    // Non-copyable, non-movable
    WaterGBuffer(const WaterGBuffer&) = delete;
    WaterGBuffer& operator=(const WaterGBuffer&) = delete;
    WaterGBuffer(WaterGBuffer&&) = delete;
    WaterGBuffer& operator=(WaterGBuffer&&) = delete;

    // Resize G-buffer when window changes
    void resize(VkExtent2D newFullResExtent);

    // Get render pass for position pass
    vk::RenderPass getRenderPass() const { return renderPass_ ? **renderPass_ : VK_NULL_HANDLE; }
    vk::Framebuffer getFramebuffer() const { return framebuffer_ ? **framebuffer_ : VK_NULL_HANDLE; }
    VkExtent2D getExtent() const { return gbufferExtent; }

    // Get G-buffer textures for sampling in composite pass
    vk::ImageView getDataImageView() const { return dataImageView_ ? **dataImageView_ : VK_NULL_HANDLE; }
    vk::ImageView getNormalImageView() const { return normalImageView_ ? **normalImageView_ : VK_NULL_HANDLE; }
    vk::ImageView getDepthImageView() const { return depthImageView_ ? **depthImageView_ : VK_NULL_HANDLE; }
    vk::Sampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Get pipeline resources
    vk::Pipeline getPipeline() const { return pipeline_ ? **pipeline_ : VK_NULL_HANDLE; }
    vk::PipelineLayout getPipelineLayout() const { return pipelineLayout_ ? **pipelineLayout_ : VK_NULL_HANDLE; }
    bool hasDescriptorSets() const { return !descriptorSets.empty(); }
    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

    // Create descriptor sets after resources are available
    bool createDescriptorSets(
        const std::vector<vk::Buffer>& mainUBOs,
        vk::DeviceSize mainUBOSize,
        const std::vector<vk::Buffer>& waterUBOs,
        vk::DeviceSize waterUBOSize,
        vk::ImageView terrainHeightView, vk::Sampler terrainSampler,
        vk::ImageView flowMapView, vk::Sampler flowMapSampler);

    // Begin/end G-buffer rendering
    void beginRenderPass(vk::CommandBuffer cmd);
    void endRenderPass(vk::CommandBuffer cmd);

    // Clear G-buffer (call at start of frame)
    void clear(vk::CommandBuffer cmd);

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createImages();
    bool createRenderPass();
    bool createFramebuffer();
    bool createSampler();
    bool createDescriptorSetLayout();
    bool createPipelineLayout();
    bool createPipeline();
    void destroyImages();

    // Device handles
    vk::Device device = VK_NULL_HANDLE;
    vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;

    // Resolution
    VkExtent2D fullResExtent = {0, 0};
    VkExtent2D gbufferExtent = {0, 0};
    float resolutionScale = 0.5f;

    // G-buffer images
    ManagedImage dataImage;
    std::optional<vk::raii::ImageView> dataImageView_;

    ManagedImage normalImage;
    std::optional<vk::raii::ImageView> normalImageView_;

    ManagedImage depthImage;
    std::optional<vk::raii::ImageView> depthImageView_;

    // Render pass and framebuffer (RAII-managed)
    std::optional<vk::raii::RenderPass> renderPass_;
    std::optional<vk::raii::Framebuffer> framebuffer_;

    // Sampler for reading G-buffer in composite pass (RAII-managed)
    std::optional<vk::raii::Sampler> sampler_;

    // Graphics pipeline for position pass (RAII-managed)
    std::optional<vk::raii::Pipeline> pipeline_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::vector<vk::DescriptorSet> descriptorSets;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;
};
