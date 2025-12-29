#pragma once

#include "InitContext.h"
#include "DescriptorManager.h"
#include "CoreResources.h"

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

// Forward declarations to avoid circular dependencies
class PostProcessSystem;
class BloomSystem;
class SnowMaskSystem;
class VolumetricSnowSystem;
class GrassSystem;
class WindSystem;
class WeatherSystem;
class LeafSystem;
class FroxelSystem;
class AtmosphereLUTSystem;
class CloudShadowSystem;
class WaterSystem;
class WaterDisplacement;
class FlowMapGenerator;
class FoamBuffer;
class SSRSystem;
class WaterTileCull;
class WaterGBuffer;
class TerrainSystem;
class ShadowSystem;
class MaterialRegistry;
class SkinnedMeshRenderer;
class RendererSystems;
struct TerrainConfig;
struct EnvironmentSettings;

/**
 * WaterSubsystems - Groups all water-related systems for easier initialization
 */
struct WaterSubsystems {
    WaterSystem& system;
    WaterDisplacement& displacement;
    FlowMapGenerator& flowMapGenerator;
    FoamBuffer& foamBuffer;
    RendererSystems& rendererSystems;  // For SSR factory creation
    WaterTileCull& tileCull;
    WaterGBuffer& gBuffer;
};

/**
 * RendererInit - Cross-cutting initialization helpers
 *
 * Contains initialization logic that spans multiple unrelated systems.
 * For single-system initialization, use the system's own ::create() factory.
 * For InitContext creation, use InitContext::build().
 *
 * Design principles:
 * - Only include methods that touch multiple unrelated systems
 * - Single-system init belongs in that system's factory method
 */
class RendererInit {
public:
    // ========================================================================
    // Grouped subsystem initialization (creates multiple related systems)
    // ========================================================================

    /**
     * Initialize post-processing systems (PostProcessSystem, BloomSystem)
     * Uses factory pattern to create PostProcessSystem.
     * Should be called early to get HDR render pass for other systems
     */
    static bool initPostProcessing(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass finalRenderPass,
        VkFormat swapchainImageFormat
    );

    /**
     * Initialize snow subsystems (SnowMaskSystem, VolumetricSnowSystem)
     * Creates both systems via factory and stores them in RendererSystems
     */
    static bool initSnowSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    // Overload using CoreResources
    static bool initSnowSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr
    ) {
        return initSnowSubsystems(systems, ctx, hdr.renderPass);
    }

    /**
     * Initialize grass and wind systems (GrassSystem, WindSystem)
     * Also connects environment settings to grass and leaf systems
     * Creates WindSystem via factory and stores it in RendererSystems
     */
    static bool initGrassSubsystem(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass,
        VkRenderPass shadowRenderPass,
        uint32_t shadowMapSize
    );

    // Overload using CoreResources
    static bool initGrassSubsystem(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr,
        const ShadowResources& shadow
    ) {
        return initGrassSubsystem(systems, ctx,
                                   hdr.renderPass, shadow.renderPass, shadow.mapSize);
    }

    /**
     * Initialize weather-related systems (WeatherSystem, LeafSystem)
     * Creates WeatherSystem via factory and stores it in RendererSystems
     */
    static bool initWeatherSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass
    );

    // Overload using CoreResources
    static bool initWeatherSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const HDRResources& hdr
    ) {
        return initWeatherSubsystems(systems, ctx, hdr.renderPass);
    }

    /**
     * Initialize atmosphere/fog systems (FroxelSystem, AtmosphereLUTSystem, CloudShadowSystem)
     * Computes initial atmosphere LUTs and connects froxel to post-process
     * Creates FroxelSystem via factory and stores it in RendererSystems
     */
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        VkImageView shadowMapView,
        VkSampler shadowMapSampler,
        const std::vector<VkBuffer>& lightBuffers
    );

    // Overload using CoreResources
    static bool initAtmosphereSubsystems(
        RendererSystems& systems,
        const InitContext& ctx,
        const ShadowResources& shadow,
        const std::vector<VkBuffer>& lightBuffers
    ) {
        return initAtmosphereSubsystems(systems, ctx, shadow.cascadeView, shadow.sampler, lightBuffers);
    }

    /**
     * Initialize all water-related systems
     */
    static bool initWaterSubsystems(
        WaterSubsystems& water,
        const InitContext& ctx,
        VkRenderPass hdrRenderPass,
        const ShadowSystem& shadowSystem,
        const TerrainSystem& terrainSystem,
        const TerrainConfig& terrainConfig,
        const PostProcessSystem& postProcessSystem,
        VkSampler depthSampler
    );

    /**
     * Create water descriptor sets after all water systems are initialized
     */
    static bool createWaterDescriptorSets(
        WaterSubsystems& water,
        const std::vector<VkBuffer>& uniformBuffers,
        size_t uniformBufferSize,
        ShadowSystem& shadowSystem,
        const TerrainSystem& terrainSystem,
        const PostProcessSystem& postProcessSystem,
        VkSampler depthSampler
    );

    // ========================================================================
    // Cross-cutting descriptor updates (touch multiple unrelated systems)
    // ========================================================================

    /**
     * Update cloud shadow bindings across all descriptor sets
     * Called after CloudShadowSystem is initialized
     */
    static void updateCloudShadowBindings(
        VkDevice device,
        MaterialRegistry& materialRegistry,
        const std::vector<VkDescriptorSet>& rockDescriptorSets,
        const std::vector<VkDescriptorSet>& detritusDescriptorSets,
        SkinnedMeshRenderer& skinnedMeshRenderer,
        VkImageView cloudShadowView,
        VkSampler cloudShadowSampler
    );
};
