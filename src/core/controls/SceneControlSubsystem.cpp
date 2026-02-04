#include "SceneControlSubsystem.h"
#include "SceneManager.h"
#include "VulkanContext.h"
#include "scene/SceneBuilder.h"

SceneBuilder& SceneControlSubsystem::getSceneBuilder() {
    return scene_.getSceneBuilder();
}

ecs::World* SceneControlSubsystem::getECSWorld() {
    return scene_.getSceneBuilder().getECSWorld();
}

uint32_t SceneControlSubsystem::getWidth() const {
    return vulkanContext_.getWidth();
}

uint32_t SceneControlSubsystem::getHeight() const {
    return vulkanContext_.getHeight();
}
