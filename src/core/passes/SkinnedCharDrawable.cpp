#include "SkinnedCharDrawable.h"

#include "SceneManager.h"
#include "scene/SceneBuilder.h"
#include "SkinnedMeshRenderer.h"
#include "npc/NPCRenderer.h"
#include "npc/NPCSimulation.h"
#include "AnimatedCharacter.h"

#include "ecs/World.h"
#include "ecs/Components.h"

SkinnedCharDrawable::SkinnedCharDrawable(const Resources& resources)
    : resources_(resources)
{
}

void SkinnedCharDrawable::recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                                         float /*time*/, const HDRDrawParams& /*params*/) {
    SceneBuilder& sceneBuilder = resources_.scene->getSceneBuilder();

    // Draw player character (slot 0 is reserved for player)
    constexpr uint32_t PLAYER_BONE_SLOT = 0;
    if (sceneBuilder.hasCharacter()) {
        // Phase 6: Use ECS entity for player transform if available
        ecs::Entity playerEntity = sceneBuilder.getPlayerEntity();
        ecs::World* ecsWorld = sceneBuilder.getECSWorld();
        if (ecsWorld && playerEntity != ecs::NullEntity && ecsWorld->has<ecs::Transform>(playerEntity)) {
            // Get transform from ECS
            const glm::mat4& playerTransform = ecsWorld->get<ecs::Transform>(playerEntity).matrix;
            resources_.skinnedMesh->record(cmd, frameIndex, PLAYER_BONE_SLOT, playerTransform, sceneBuilder.getAnimatedCharacter());
        } else {
            // Fallback: use Renderable (legacy path)
            const auto& sceneObjects = sceneBuilder.getRenderables();
            size_t playerIndex = sceneBuilder.getPlayerObjectIndex();
            if (playerIndex < sceneObjects.size()) {
                const Renderable& playerObj = sceneObjects[playerIndex];
                resources_.skinnedMesh->record(cmd, frameIndex, PLAYER_BONE_SLOT, playerObj, sceneBuilder.getAnimatedCharacter());
            }
        }
    }

    // Draw NPC characters via NPCRenderer (NPCs use slots 1+)
    if (resources_.npcRenderer) {
        if (auto* npcSim = sceneBuilder.getNPCSimulation()) {
            resources_.npcRenderer->prepare(frameIndex, *npcSim);
            resources_.npcRenderer->recordDraw(cmd, frameIndex);
        }
    }
}
