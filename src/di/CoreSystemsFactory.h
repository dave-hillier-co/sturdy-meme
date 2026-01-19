#pragma once

#include <fruit/fruit.h>
#include <memory>
#include <vulkan/vulkan.h>

#include "VulkanServices.h"

// Forward declarations
class PostProcessSystem;
class BloomSystem;
class BilateralGridSystem;
class ShadowSystem;
class TerrainSystem;
class GlobalBufferManager;

namespace di {

/**
 * Annotations for distinguishing system types in Fruit
 */
struct PostProcess {};
struct Shadow {};
struct Terrain {};

/**
 * Core systems that other systems depend on.
 * These are Tier 1 systems created first.
 */
struct CoreBundle {
    std::unique_ptr<PostProcessSystem> postProcess;
    std::unique_ptr<BloomSystem> bloom;
    std::unique_ptr<BilateralGridSystem> bilateralGrid;
    std::unique_ptr<ShadowSystem> shadow;
    std::unique_ptr<TerrainSystem> terrain;
    std::unique_ptr<GlobalBufferManager> globalBuffers;
};

/**
 * Factory for creating core systems.
 * Takes VulkanServices via DI and creates systems on demand.
 */
class CoreSystemsFactory {
public:
    INJECT(CoreSystemsFactory(VulkanServices& services))
        : services_(services) {}

    /**
     * Create the post-processing bundle (PostProcess, Bloom, BilateralGrid).
     * @param swapchainRenderPass The final output render pass
     * @param swapchainFormat The swapchain image format
     */
    struct PostProcessBundle {
        std::unique_ptr<PostProcessSystem> postProcess;
        std::unique_ptr<BloomSystem> bloom;
        std::unique_ptr<BilateralGridSystem> bilateralGrid;
    };
    std::optional<PostProcessBundle> createPostProcess(
        VkRenderPass swapchainRenderPass,
        VkFormat swapchainFormat
    );

    /**
     * Create shadow system.
     * @param mainDescriptorLayout The main material descriptor set layout
     * @param skinnedMeshLayout The skinned mesh descriptor set layout
     */
    std::unique_ptr<ShadowSystem> createShadow(
        VkDescriptorSetLayout mainDescriptorLayout,
        VkDescriptorSetLayout skinnedMeshLayout
    );

    /**
     * Create terrain system.
     * @param hdrRenderPass The HDR render pass
     * @param shadowRenderPass The shadow render pass
     * @param shadowMapSize Shadow map resolution
     * @param resourcePath Path to terrain resources
     */
    std::unique_ptr<TerrainSystem> createTerrain(
        VkRenderPass hdrRenderPass,
        VkRenderPass shadowRenderPass,
        uint32_t shadowMapSize,
        const std::string& resourcePath
    );

    /**
     * Create global buffer manager.
     */
    std::unique_ptr<GlobalBufferManager> createGlobalBuffers();

private:
    VulkanServices& services_;
};

/**
 * Get the Fruit component for CoreSystemsFactory.
 */
fruit::Component<CoreSystemsFactory> getCoreSystemsFactoryComponent();

} // namespace di
