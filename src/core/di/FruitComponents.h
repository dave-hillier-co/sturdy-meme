#pragma once

#include <fruit/fruit.h>
#include <functional>
#include <memory>
#include <string>
#include <vulkan/vulkan.h>

// Forward declarations only - no heavy includes
class VulkanContext;
class RendererSystems;
class DescriptorManager;
struct InitContext;

/**
 * Minimal Fruit DI Components for core rendering infrastructure.
 *
 * Provides injection for:
 * - InitContext factory (runtime parameters)
 * - RendererSystems (subsystem container)
 *
 * Usage:
 *   fruit::Injector<IRendererSystems> injector(getRendererSystemsComponent);
 *   auto& systems = injector.get<IRendererSystems&>().get();
 */

// ============================================================================
// InitContext Factory
// ============================================================================

/**
 * Factory for creating InitContext with runtime parameters.
 */
using InitContextFactory = std::function<InitContext(
    const VulkanContext& vulkanContext,
    VkCommandPool commandPool,
    void* descriptorPool,  // DescriptorManager::Pool*
    const std::string& resourcePath,
    uint32_t framesInFlight
)>;

fruit::Component<InitContextFactory> getInitContextFactoryComponent();

// ============================================================================
// RendererSystems Interface
// ============================================================================

/**
 * Interface for RendererSystems injection.
 */
class IRendererSystems {
public:
    virtual ~IRendererSystems() = default;
    virtual RendererSystems& get() = 0;
    virtual const RendererSystems& get() const = 0;
};

fruit::Component<IRendererSystems> getRendererSystemsComponent();

// ============================================================================
// Combined Component
// ============================================================================

fruit::Component<IRendererSystems, InitContextFactory> getRenderingComponent();
