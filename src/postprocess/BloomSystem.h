#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "DescriptorManager.h"
#include "InitContext.h"
#include "core/vulkan/VmaImage.h"

class BloomSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit BloomSystem(ConstructToken) {}

    // Legacy InitInfo - kept for backward compatibility during migration
    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        VkExtent2D extent;
        std::string shaderPath;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize bloom system.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<BloomSystem> create(const InitInfo& info);
    static std::unique_ptr<BloomSystem> create(const InitContext& ctx);

    ~BloomSystem();

    // Non-copyable, non-movable (stored via unique_ptr only)
    BloomSystem(BloomSystem&&) = delete;
    BloomSystem& operator=(BloomSystem&&) = delete;
    BloomSystem(const BloomSystem&) = delete;
    BloomSystem& operator=(const BloomSystem&) = delete;

    void resize(VkExtent2D newExtent);

    void recordBloomPass(vk::CommandBuffer cmd, vk::ImageView hdrInput);

    vk::ImageView getBloomOutput() const { return mipChain.empty() ? VK_NULL_HANDLE : **mipChain[0].imageView; }
    vk::Sampler getBloomSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    void setThreshold(float t) { threshold = t; }
    float getThreshold() const { return threshold; }
    void setIntensity(float i) { intensity = i; }
    float getIntensity() const { return intensity; }


private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    struct MipLevel {
        ManagedImage image;
        std::optional<vk::raii::ImageView> imageView;
        std::optional<vk::raii::Framebuffer> framebuffer;
        VkExtent2D extent = {0, 0};
    };

    bool createMipChain();
    bool createRenderPass();
    bool createSampler();
    bool createDescriptorSetLayouts();
    bool createPipelines();
    bool createDescriptorSets();

    void destroyMipChain();

    vk::Device device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool = nullptr;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    const vk::raii::Device* raiiDevice_ = nullptr;

    static constexpr VkFormat BLOOM_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr uint32_t MAX_MIP_LEVELS = 6;

    std::vector<MipLevel> mipChain;

    std::optional<vk::raii::RenderPass> downsampleRenderPass_;
    std::optional<vk::raii::RenderPass> upsampleRenderPass_;
    std::optional<vk::raii::Sampler> sampler_;

    // Downsample pipeline
    std::optional<vk::raii::DescriptorSetLayout> downsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> downsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> downsamplePipeline_;
    std::vector<vk::DescriptorSet> downsampleDescSets;

    // Upsample pipeline
    std::optional<vk::raii::DescriptorSetLayout> upsampleDescSetLayout_;
    std::optional<vk::raii::PipelineLayout> upsamplePipelineLayout_;
    std::optional<vk::raii::Pipeline> upsamplePipeline_;
    std::vector<vk::DescriptorSet> upsampleDescSets;

    // Source view the descriptor sets were last written with. Descriptor sets
    // must not be rewritten every frame - in-flight frames still execute with
    // them bound (see recordBloomPass).
    vk::ImageView writtenHdrInput_ = VK_NULL_HANDLE;

    // Parameters
    float threshold = 1.0f;
    float intensity = 1.0f;

    // Push constants for downsample
    struct DownsamplePushConstants {
        float resolutionX;
        float resolutionY;
        float threshold;
        int isFirstPass;
    };

    // Push constants for upsample
    struct UpsamplePushConstants {
        float resolutionX;
        float resolutionY;
        float filterRadius;
        float padding;
    };
};
