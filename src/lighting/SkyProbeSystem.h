#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include <array>
#include "SkyProbeConfig.h"
#include "InitContext.h"
#include "DescriptorManager.h"

/**
 * SkyProbeSystem - Cascaded Sky Visibility Probes
 *
 * Provides global ambient lighting from sky, modulated by local visibility.
 * Implements Ghost of Tsushima-style sky probes with:
 * - 4 camera-relative cascades (4m to 256m spacing)
 * - SH1 or bent normal storage format
 * - Optional runtime baking via SDF cone tracing
 * - Integration with AtmosphereLUTSystem irradiance
 *
 * Usage:
 * 1. Initialize with config (quality preset or custom)
 * 2. Each frame: updateCascades(cameraPos) to reposition
 * 3. Optional: recordBaking() for runtime probe updates
 * 4. In shaders: #include "sky_probe_sample.glsl" and call sampleSkyProbe()
 */
class SkyProbeSystem {
public:
    struct InitInfo {
        VkDevice device;
        VkPhysicalDevice physicalDevice;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue computeQueue;
        std::string shaderPath;
        std::string resourcePath;
        uint32_t framesInFlight;
        DescriptorManager::Pool* descriptorPool;
        SkyProbeConfig config;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Push constants for baking shader
    struct BakePushConstants {
        glm::vec4 cascadeOrigin;    // xyz = origin, w = spacing
        glm::vec4 cascadeParams;    // x = gridSize, y = layer offset, z = numSamples, w = unused
        glm::vec4 skyParams;        // x = sunZenith, y = sunAzimuth, z = turbidity, w = unused
        uint32_t probeStartIndex;
        uint32_t probeCount;
        float padding[2];
    };

    static std::unique_ptr<SkyProbeSystem> create(const InitInfo& info);
    static std::unique_ptr<SkyProbeSystem> create(const InitContext& ctx, const SkyProbeConfig& config);

    ~SkyProbeSystem();

    // Non-copyable
    SkyProbeSystem(const SkyProbeSystem&) = delete;
    SkyProbeSystem& operator=(const SkyProbeSystem&) = delete;

    /**
     * Update cascade positions based on camera.
     * Cascades are centered on camera, snapped to grid.
     */
    void updateCascades(const glm::vec3& cameraPos);

    /**
     * Record probe baking compute pass.
     * Only needed if runtimeBaking is enabled.
     * sdfAtlasView/sdfEntriesBuffer: SDF data for occlusion
     */
    void recordBaking(VkCommandBuffer cmd, uint32_t frameIndex,
                      VkImageView sdfAtlasView,
                      VkBuffer sdfEntriesBuffer,
                      VkBuffer sdfInstancesBuffer,
                      uint32_t sdfInstanceCount,
                      float sunZenith, float sunAzimuth);

    /**
     * Load pre-baked probes from file.
     * File format: binary dump of probe data per cascade.
     */
    bool loadBakedProbes(const std::string& path);

    /**
     * Save current probes to file (for offline baking).
     */
    bool saveBakedProbes(const std::string& path);

    // GPU resource accessors for shader binding
    VkImageView getProbeTextureView() const { return probeTextureView_; }
    VkSampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }
    VkBuffer getCascadeInfoBuffer() const { return cascadeInfoBuffer_; }

    // Configuration
    void setEnabled(bool enable) { enabled_ = enable; }
    bool isEnabled() const { return enabled_; }

    void setIntensity(float i) { intensity_ = i; }
    float getIntensity() const { return intensity_; }

    const SkyProbeConfig& getConfig() const { return config_; }

private:
    SkyProbeSystem() = default;
    bool initInternal(const InitInfo& info);
    void cleanup();

    bool createProbeTexture();
    bool createBuffers();
    bool createBakePipeline();
    bool createDescriptorSets();

    void updateCascadeInfoBuffer();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    std::string shaderPath_;
    std::string resourcePath_;
    uint32_t framesInFlight_ = 0;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    SkyProbeConfig config_;
    const vk::raii::Device* raiiDevice_ = nullptr;

    bool enabled_ = true;
    float intensity_ = 1.0f;

    // Per-cascade runtime state
    struct CascadeState {
        glm::vec3 origin;           // Current world origin
        uint32_t layerOffset;       // Offset in 3D texture (Z layers)
        uint32_t nextProbeToUpdate; // For incremental baking
    };
    std::array<CascadeState, SkyProbeConfig::NUM_CASCADES> cascadeStates_;

    // 3D texture containing all cascade probes
    // Layout: X × Y × (Z * numCascades), where each cascade occupies gridSize layers
    VkImage probeTexture_ = VK_NULL_HANDLE;
    VkImageView probeTextureView_ = VK_NULL_HANDLE;
    VmaAllocation probeAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::Sampler> sampler_;

    // Cascade info buffer (for shader lookup)
    VkBuffer cascadeInfoBuffer_ = VK_NULL_HANDLE;
    VmaAllocation cascadeInfoAllocation_ = VK_NULL_HANDLE;

    // Baking pipeline (optional, for runtime baking)
    std::optional<vk::raii::Pipeline> bakePipeline_;
    std::optional<vk::raii::PipelineLayout> bakePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> bakeDescriptorSetLayout_;
    std::vector<VkDescriptorSet> bakeDescriptorSets_;
};
