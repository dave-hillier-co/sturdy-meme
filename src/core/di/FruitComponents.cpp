// FruitComponents.cpp - Minimal Fruit DI components
#include "FruitComponents.h"
#include "InitContext.h"
#include "RendererSystems.h"
#include "DescriptorManager.h"

#include <SDL3/SDL_log.h>

// ============================================================================
// RendererSystems Implementation (internal)
// ============================================================================

class RendererSystemsImpl : public IRendererSystems {
public:
    INJECT(RendererSystemsImpl())
        : systems_(std::make_unique<RendererSystems>())
    {
        SDL_Log("RendererSystemsImpl: Created via Fruit DI");
    }

    ~RendererSystemsImpl() override = default;

    RendererSystems& get() override { return *systems_; }
    const RendererSystems& get() const override { return *systems_; }

private:
    std::unique_ptr<RendererSystems> systems_;
};

// ============================================================================
// Component Implementations
// ============================================================================

fruit::Component<InitContextFactory> getInitContextFactoryComponent() {
    return fruit::createComponent()
        .registerProvider([]() -> InitContextFactory {
            return [](const VulkanContext& vulkanContext,
                     VkCommandPool commandPool,
                     void* descriptorPool,
                     const std::string& resourcePath,
                     uint32_t framesInFlight) -> InitContext {
                return InitContext::build(
                    vulkanContext,
                    commandPool,
                    static_cast<DescriptorManager::Pool*>(descriptorPool),
                    resourcePath,
                    framesInFlight
                );
            };
        });
}

fruit::Component<IRendererSystems> getRendererSystemsComponent() {
    return fruit::createComponent()
        .bind<IRendererSystems, RendererSystemsImpl>();
}

fruit::Component<IRendererSystems, InitContextFactory> getRenderingComponent() {
    return fruit::createComponent()
        .install(getRendererSystemsComponent)
        .install(getInitContextFactoryComponent);
}
