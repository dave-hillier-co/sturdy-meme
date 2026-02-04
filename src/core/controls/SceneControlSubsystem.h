#pragma once

#include "interfaces/ISceneControl.h"

class SceneManager;
class VulkanContext;
class SceneBuilder;
namespace ecs { class World; }

/**
 * SceneControlSubsystem - Implements ISceneControl
 * Provides access to SceneBuilder, ECS World, and viewport dimensions.
 */
class SceneControlSubsystem : public ISceneControl {
public:
    SceneControlSubsystem(SceneManager& scene, VulkanContext& vulkanContext)
        : scene_(scene)
        , vulkanContext_(vulkanContext) {}

    SceneBuilder& getSceneBuilder() override;
    ecs::World* getECSWorld() override;
    uint32_t getWidth() const override;
    uint32_t getHeight() const override;

private:
    SceneManager& scene_;
    VulkanContext& vulkanContext_;
};
