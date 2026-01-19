#include "CoreSystemsFactory.h"
#include "PostProcessSystem.h"
#include "BloomSystem.h"
#include "BilateralGridSystem.h"
#include "ShadowSystem.h"
#include "TerrainSystem.h"
#include "TerrainFactory.h"
#include "GlobalBufferManager.h"

namespace di {

std::optional<CoreSystemsFactory::PostProcessBundle> CoreSystemsFactory::createPostProcess(
    VkRenderPass swapchainRenderPass,
    VkFormat swapchainFormat
) {
    auto bundle = PostProcessSystem::createWithDependencies(
        services_,
        swapchainRenderPass,
        swapchainFormat
    );

    if (!bundle) {
        return std::nullopt;
    }

    PostProcessBundle result;
    result.postProcess = std::move(bundle->postProcess);
    result.bloom = std::move(bundle->bloom);
    result.bilateralGrid = std::move(bundle->bilateralGrid);
    return result;
}

std::unique_ptr<ShadowSystem> CoreSystemsFactory::createShadow(
    VkDescriptorSetLayout mainDescriptorLayout,
    VkDescriptorSetLayout skinnedMeshLayout
) {
    return ShadowSystem::create(
        services_.toInitContext(),
        mainDescriptorLayout,
        skinnedMeshLayout
    );
}

std::unique_ptr<TerrainSystem> CoreSystemsFactory::createTerrain(
    VkRenderPass hdrRenderPass,
    VkRenderPass shadowRenderPass,
    uint32_t shadowMapSize,
    const std::string& resourcePath
) {
    TerrainFactory::Config config;
    config.hdrRenderPass = hdrRenderPass;
    config.shadowRenderPass = shadowRenderPass;
    config.shadowMapSize = shadowMapSize;
    config.resourcePath = resourcePath;

    return TerrainFactory::create(services_.toInitContext(), config);
}

std::unique_ptr<GlobalBufferManager> CoreSystemsFactory::createGlobalBuffers() {
    return GlobalBufferManager::create(
        services_.allocator(),
        services_.physicalDevice(),
        services_.framesInFlight()
    );
}

fruit::Component<CoreSystemsFactory> getCoreSystemsFactoryComponent() {
    return fruit::createComponent();
}

} // namespace di
