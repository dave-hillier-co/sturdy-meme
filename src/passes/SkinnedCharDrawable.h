#pragma once

// ============================================================================
// SkinnedCharDrawable.h - Skinned character rendering as IHDRDrawable
// ============================================================================
//
// Encapsulates player and NPC skinned mesh rendering with GPU skinning.
// Supports both ECS transform lookup and legacy Renderable path.
//

#include "interfaces/IHDRDrawable.h"

#include <functional>

// Forward declarations
class SceneManager;
class SkinnedMeshRenderer;
class NPCRenderer;

namespace ecs { class World; }

// Callback for drawing additional skinned characters (e.g. ragdolls).
// Parameters: cmd, frameIndex
using RagdollDrawCallback = std::function<void(VkCommandBuffer, uint32_t)>;

/**
 * Renders skinned characters (player + NPCs + ragdolls) in the HDR pass.
 *
 * Handles:
 * - Player character with GPU skinning (bone slot 0)
 * - ECS entity transform lookup for player (Phase 6)
 * - Legacy Renderable fallback for player transform
 * - NPC rendering via NPCRenderer (bone slots 1+)
 * - Ragdoll rendering via callback (bone slots 32+)
 */
class SkinnedCharDrawable : public IHDRDrawable {
public:
    struct Resources {
        SceneManager* scene = nullptr;
        SkinnedMeshRenderer* skinnedMesh = nullptr;
        NPCRenderer* npcRenderer = nullptr;  // Optional - null if no NPCs
        RagdollDrawCallback ragdollDrawCallback;  // Optional - called to draw ragdolls
    };

    explicit SkinnedCharDrawable(const Resources& resources);

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float time, const HDRDrawParams& params) override;

private:
    Resources resources_;
};
