#pragma once

// ============================================================================
// SkinnedCharDrawable.h - Skinned character rendering as IHDRDrawable
// ============================================================================
//
// Encapsulates player and NPC skinned mesh rendering with GPU skinning.
// Supports both ECS transform lookup and legacy Renderable path.
//

#include "interfaces/IHDRDrawable.h"

// Forward declarations
class SceneManager;
class SkinnedMeshRenderer;
class NPCRenderer;

namespace ecs { class World; }

/**
 * Renders skinned characters (player + NPCs) in the HDR pass.
 *
 * Handles:
 * - Player character with GPU skinning (bone slot 0)
 * - ECS entity transform lookup for player (Phase 6)
 * - Legacy Renderable fallback for player transform
 * - NPC rendering via NPCRenderer (bone slots 1+)
 */
class SkinnedCharDrawable : public IHDRDrawable {
public:
    struct Resources {
        SceneManager* scene = nullptr;
        SkinnedMeshRenderer* skinnedMesh = nullptr;
        NPCRenderer* npcRenderer = nullptr;  // Optional - null if no NPCs
    };

    explicit SkinnedCharDrawable(const Resources& resources);

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float time, const HDRDrawParams& params) override;

private:
    Resources resources_;
};
