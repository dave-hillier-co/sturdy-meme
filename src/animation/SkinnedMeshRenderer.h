#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <optional>

#include "SkinnedMesh.h"
#include "CharacterLOD.h"
#include "DescriptorManager.h"
#include "MaterialDescriptorFactory.h"
#include "GlobalBufferManager.h"
#include "DynamicUniformBuffer.h"
#include "VmaBuffer.h"

class AnimatedCharacter;

// Maximum number of skinned characters that can be rendered per frame
// Each character needs a separate bone matrix slot in the dynamic uniform buffer
constexpr uint32_t MAX_SKINNED_CHARACTERS = 64;

// Skinned mesh renderer - handles GPU skinning pipeline and bone matrices
class SkinnedMeshRenderer {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SkinnedMeshRenderer(ConstructToken) {}

    // Callback type for adding common descriptor bindings
    using AddCommonBindingsCallback = std::function<void(DescriptorManager::LayoutBuilder&)>;

    struct InitInfo {
        vk::Device device;
        vk::PhysicalDevice physicalDevice;  // For minUniformBufferOffsetAlignment
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;
        vk::RenderPass renderPass;  // HDR render pass
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        AddCommonBindingsCallback addCommonBindings;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // Resources needed for descriptor set writing
    struct DescriptorResources {
        const GlobalBufferManager* globalBufferManager;

        // Shadow system resources
        vk::ImageView shadowMapView;
        vk::Sampler shadowMapSampler;
        vk::ImageView emissiveMapView;
        vk::Sampler emissiveMapSampler;
        std::vector<vk::ImageView>* pointShadowViews;  // Per-frame
        vk::Sampler pointShadowSampler;
        std::vector<vk::ImageView>* spotShadowViews;   // Per-frame
        vk::Sampler spotShadowSampler;
        vk::ImageView snowMaskView;
        vk::Sampler snowMaskSampler;

        // Placeholder textures
        vk::ImageView whiteTextureView;
        vk::Sampler whiteTextureSampler;

        // Player material textures (from MaterialRegistry based on player's materialId)
        vk::ImageView playerDiffuseView;
        vk::Sampler playerDiffuseSampler;
        vk::ImageView playerNormalView;
        vk::Sampler playerNormalSampler;
    };

    /**
     * Factory: Create and initialize SkinnedMeshRenderer.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<SkinnedMeshRenderer> create(const InitInfo& info);


    ~SkinnedMeshRenderer();

    // Non-copyable, non-movable
    SkinnedMeshRenderer(const SkinnedMeshRenderer&) = delete;
    SkinnedMeshRenderer& operator=(const SkinnedMeshRenderer&) = delete;
    SkinnedMeshRenderer(SkinnedMeshRenderer&&) = delete;
    SkinnedMeshRenderer& operator=(SkinnedMeshRenderer&&) = delete;

    // Create descriptor sets after all resources are ready
    bool createDescriptorSets(const DescriptorResources& resources);

    // Update cloud shadow binding after cloud shadow system is initialized
    void updateCloudShadowBinding(vk::ImageView cloudShadowView, vk::Sampler cloudShadowSampler);

    /**
     * Update bone matrices for a character at a specific slot.
     * @param frameIndex Current frame index for triple-buffered resources
     * @param slotIndex Character slot (0 to MAX_SKINNED_CHARACTERS-1)
     * @param character Character to get bone matrices from (can be null for identity)
     */
    void updateBoneMatrices(uint32_t frameIndex, uint32_t slotIndex, AnimatedCharacter* character);

    /**
     * Update bone matrices from raw matrix data (for physics-based animation).
     * @param frameIndex Current frame index for triple-buffered resources
     * @param slotIndex Character slot (0 to MAX_SKINNED_CHARACTERS-1)
     * @param matrices Bone matrices to upload (global * inverseBindMatrix)
     * @param count Number of matrices
     */
    void updateBoneMatricesRaw(uint32_t frameIndex, uint32_t slotIndex,
                               const glm::mat4* matrices, size_t count);

    /**
     * Record draw commands with explicit transform (ECS-compatible).
     * Uses VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC to select the correct
     * bone matrix slot for this character at draw time.
     *
     * @param cmd Command buffer to record to
     * @param frameIndex Current frame index
     * @param slotIndex Character slot containing this character's bone matrices
     * @param transform World transform matrix for the character
     * @param character Character for mesh data
     * @param hueShift Optional hue shift for NPC tinting (default: 0.0f)
     */
    void record(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                const glm::mat4& transform, AnimatedCharacter& character,
                float hueShift = 0.0f);

    /**
     * Record draw commands with explicit LOD mesh (ECS-compatible).
     */
    void recordWithLOD(vk::CommandBuffer cmd, uint32_t frameIndex, uint32_t slotIndex,
                       const glm::mat4& transform, AnimatedCharacter& character,
                       const CharacterLODMesh& lodMesh);

    // Get the maximum number of character slots
    static constexpr uint32_t getMaxSlots() { return MAX_SKINNED_CHARACTERS; }

    // Update extent for viewport (on window resize)
    void setExtent(VkExtent2D newExtent) { extent = newExtent; }

    // Accessors for ShadowSystem integration
    vk::DescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout_ ? **descriptorSetLayout_ : vk::DescriptorSetLayout{}; }
    vk::PipelineLayout getPipelineLayout() const { return pipelineLayout_ ? **pipelineLayout_ : vk::PipelineLayout{}; }
    vk::Pipeline getPipeline() const { return pipeline_ ? **pipeline_ : vk::Pipeline{}; }
    vk::DescriptorSet getDescriptorSet(uint32_t frameIndex) const { return descriptorSets[frameIndex]; }

private:
    bool initInternal(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool createPipeline();
    bool createBoneMatricesBuffers();

    // Vulkan handles (stored, not owned)
    vk::Device device{};
    vk::PhysicalDevice physicalDevice{};
    VmaAllocator allocator = nullptr;
    DescriptorManager::Pool* descriptorPool = nullptr;
    vk::RenderPass renderPass{};
    VkExtent2D extent{};
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    AddCommonBindingsCallback addCommonBindings;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Created resources (RAII-managed), declared in dependency order so that
    // reverse destruction runs: bone buffer, pipeline, pipeline layout, set layout.
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    // Pool-owned: released with the DescriptorManager::Pool
    std::vector<vk::DescriptorSet> descriptorSets;

    // Multi-slot dynamic buffer for bone matrices
    // Supports MAX_SKINNED_CHARACTERS slots per frame, selected via dynamic offset.
    // The plain struct keeps offsets/mapped pointer; boneMatricesStorage_ owns the
    // buffer + allocation (persistently mapped, VMA unmaps on destroy).
    BufferUtils::MultiSlotDynamicBuffer boneMatricesBuffer_;
    VmaBuffer boneMatricesStorage_;

    // Reused across updateBoneMatrices() calls to avoid a per-call heap allocation.
    // Safe because updateBoneMatrices is only called sequentially on the main thread.
    std::vector<glm::mat4> boneMatricesScratch_;
};
