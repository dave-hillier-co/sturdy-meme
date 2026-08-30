#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <array>
#include <optional>

#include "TreeOptions.h"
#include "ImpostorTypes.h"
#include "DescriptorManager.h"

class TreeImpostorAtlas {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TreeImpostorAtlas(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice;  // vulkan-hpp RAII device
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::CommandPool commandPool;
        vk::Queue graphicsQueue;
        DescriptorManager::Pool* descriptorPool;
        std::string resourcePath;
        uint32_t maxArchetypes = 16;  // Maximum different tree types
    };

    static std::unique_ptr<TreeImpostorAtlas> create(const InitInfo& info);
    ~TreeImpostorAtlas();

    // Non-copyable, non-movable
    TreeImpostorAtlas(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas& operator=(const TreeImpostorAtlas&) = delete;
    TreeImpostorAtlas(TreeImpostorAtlas&&) = delete;
    TreeImpostorAtlas& operator=(TreeImpostorAtlas&&) = delete;

    // Generate impostor atlas for a tree archetype
    // Returns archetype index, or -1 on failure
    int32_t generateArchetype(
        const std::string& name,
        const TreeOptions& options,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        vk::ImageView barkAlbedo,
        vk::ImageView barkNormal,
        vk::ImageView leafAlbedo,
        vk::Sampler sampler);

    // Get archetype by name
    const TreeImpostorArchetype* getArchetype(const std::string& name) const;
    const TreeImpostorArchetype* getArchetype(uint32_t index) const;

    // Get number of archetypes
    size_t getArchetypeCount() const { return archetypes_.size(); }

    // Get atlas array textures for binding (single array covers all archetypes)
    vk::ImageView getAlbedoAtlasArrayView() const { return octaAlbedoArrayView_ ? **octaAlbedoArrayView_ : VK_NULL_HANDLE; }
    vk::ImageView getNormalAtlasArrayView() const { return octaNormalArrayView_ ? **octaNormalArrayView_ : VK_NULL_HANDLE; }
    vk::Sampler getAtlasSampler() const { return atlasSampler_ ? **atlasSampler_ : VK_NULL_HANDLE; }

    // LOD settings
    TreeLODSettings& getLODSettings() { return lodSettings_; }
    const TreeLODSettings& getLODSettings() const { return lodSettings_; }

    // Per-archetype atlas views for UI preview. Pair with getAtlasSampler().
    // The GUI layer (GuiTreeTab) owns any ImGui descriptor-set creation.
    vk::ImageView getAlbedoAtlasView(uint32_t archetypeIndex) const;
    vk::ImageView getNormalAtlasView(uint32_t archetypeIndex) const;


private:
    bool initInternal(const InitInfo& info);

    // Pipeline and resource creation (implemented in TreeImpostorAtlasCapture.cpp)
    bool createRenderPass();
    bool createCapturePipeline();
    bool createLeafCapturePipeline();
    bool createLeafQuadMesh();

    // Atlas resource management
    bool createAtlasArrayTextures();
    bool createAtlasResources(uint32_t archetypeIndex);
    bool createSampler();

    // Octahedral rendering (implemented in TreeImpostorAtlasCapture.cpp)
    void renderOctahedralCell(
        vk::CommandBuffer cmd,
        int cellX, int cellY,
        glm::vec3 viewDirection,
        const struct Mesh& branchMesh,
        const std::vector<struct LeafInstanceGPU>& leafInstances,
        float horizontalRadius,
        float boundingSphereRadius,
        float halfHeight,
        float centerHeight,
        float baseY,
        vk::DescriptorSet branchDescSet,
        vk::DescriptorSet leafDescSet);

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_ = VK_NULL_HANDLE;
    vk::PhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    vk::CommandPool commandPool_ = VK_NULL_HANDLE;
    vk::Queue graphicsQueue_ = VK_NULL_HANDLE;
    DescriptorManager::Pool* descriptorPool_ = nullptr;
    std::string resourcePath_;

    // Render pass for capturing impostors (renders to G-buffer)
    std::optional<vk::raii::RenderPass> captureRenderPass_;

    // Pipeline for capturing tree geometry to G-buffer
    std::optional<vk::raii::Pipeline> branchCapturePipeline_;
    std::optional<vk::raii::Pipeline> leafCapturePipeline_;
    std::optional<vk::raii::PipelineLayout> capturePipelineLayout_;
    std::optional<vk::raii::PipelineLayout> leafCapturePipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> captureDescriptorSetLayout_;
    std::optional<vk::raii::DescriptorSetLayout> leafCaptureDescriptorSetLayout_;

    // Leaf quad mesh for capture
    vk::Buffer leafQuadVertexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadVertexAllocation_ = VK_NULL_HANDLE;
    vk::Buffer leafQuadIndexBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafQuadIndexAllocation_ = VK_NULL_HANDLE;
    uint32_t leafQuadIndexCount_ = 0;

    // Archetype data
    std::vector<TreeImpostorArchetype> archetypes_;

    // Texture array for all archetypes (shared across all archetypes)
    vk::Image octaAlbedoArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaAlbedoArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaAlbedoArrayView_;

    vk::Image octaNormalArrayImage_ = VK_NULL_HANDLE;
    VmaAllocation octaNormalArrayAllocation_ = VK_NULL_HANDLE;
    std::optional<vk::raii::ImageView> octaNormalArrayView_;

    uint32_t maxArchetypes_ = 16;  // Maximum layers in the array

    // Shared sampler for atlas textures
    std::optional<vk::raii::Sampler> atlasSampler_;

    // Capture descriptor sets (reused)
    std::vector<vk::DescriptorSet> captureDescriptorSets_;

    // LOD settings
    TreeLODSettings lodSettings_;

    // Leaf instance buffer for capture (temporary)
    vk::Buffer leafCaptureBuffer_ = VK_NULL_HANDLE;
    VmaAllocation leafCaptureAllocation_ = VK_NULL_HANDLE;
    vk::DeviceSize leafCaptureBufferSize_ = 0;

    // Per-archetype atlas (depth buffers and framebuffers)
    struct AtlasTextures {
        vk::Image depthImage = VK_NULL_HANDLE;
        VmaAllocation depthAllocation = VK_NULL_HANDLE;
        std::optional<vk::raii::ImageView> albedoView;   // View into octaAlbedoArrayImage_
        std::optional<vk::raii::ImageView> normalView;   // View into octaNormalArrayImage_
        std::optional<vk::raii::ImageView> depthView;
        std::optional<vk::raii::Framebuffer> framebuffer;
    };
    std::vector<AtlasTextures> atlasTextures_;
};
