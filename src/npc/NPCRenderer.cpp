#include "NPCRenderer.h"
#include "NPCSimulation.h"
#include "AnimatedCharacter.h"
#include "SkinnedMeshRenderer.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include <SDL3/SDL.h>

NPCRenderer::NPCRenderer(ConstructToken) {}

std::unique_ptr<NPCRenderer> NPCRenderer::create(const InitInfo& info) {
    auto renderer = std::make_unique<NPCRenderer>(ConstructToken{});
    if (!renderer->initInternal(info)) {
        return nullptr;
    }
    return renderer;
}

NPCRenderer::~NPCRenderer() = default;

bool NPCRenderer::initInternal(const InitInfo& info) {
    if (!info.skinnedMeshRenderer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "NPCRenderer: skinnedMeshRenderer is required");
        return false;
    }

    skinnedMeshRenderer_ = info.skinnedMeshRenderer;
    return true;
}

void NPCRenderer::prepare(uint32_t frameIndex,
                          NPCSimulation& npcSim,
                          ecs::World* ecsWorld) {
    currentNpcSim_ = &npcSim;
    currentFrameIndex_ = frameIndex;

    // Clear previous frame's render data
    renderData_.clear();

    // The ECS world is the single source of truth for per-NPC render state.
    if (!ecsWorld) {
        visibleNPCCount_ = 0;
        drawCallCount_ = 0;
        return;
    }

    // Bone slot allocation: slot 0 is reserved for player, NPCs use slots 1+.
    // Slots are assigned sequentially in this single ordered pass and stored in
    // renderData_; recordDraw replays the same ordered list within the same frame,
    // so each slot stays paired with the matrices uploaded here.
    uint32_t nextBoneSlot = 1;  // Start at 1 (slot 0 reserved for player)
    const uint32_t maxSlots = SkinnedMeshRenderer::getMaxSlots();

    // Iterate the dense ECS view of skinned NPC entities. The view covers exactly
    // the simulation NPC entities (Transform/NPCLODController/SkinnedMeshRef/NPCHueShift).
    for (auto [entity, transform, lodCtrl, skinnedRef, hue] :
         ecsWorld->view<ecs::Transform, ecs::NPCLODController,
                        ecs::SkinnedMeshRef, ecs::NPCHueShift>().each()) {
        // Skip Virtual LOD NPCs (not rendered)
        if (lodCtrl.level == ecs::NPCLODLevel::Virtual) {
            continue;
        }

        // Skip NPCs without a valid character
        auto* character = static_cast<AnimatedCharacter*>(skinnedRef.character);
        if (!character) {
            continue;
        }

        // Check if we have slots available
        if (nextBoneSlot >= maxSlots) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "NPCRenderer: Exceeded max character slots (%u), skipping remaining NPCs", maxSlots);
            break;
        }

        NPCRenderData data{};
        data.character = character;
        data.transform = transform.matrix;
        data.boneSlot = nextBoneSlot;
        data.hueShift = hue.hueShift;

        // Update bone matrices for this NPC in its assigned slot
        skinnedMeshRenderer_->updateBoneMatrices(frameIndex, nextBoneSlot, character);

        renderData_.push_back(data);
        nextBoneSlot++;
    }

    visibleNPCCount_ = renderData_.size();
    drawCallCount_ = visibleNPCCount_;  // Currently 1:1, will improve with batching
}

void NPCRenderer::recordDraw(vk::CommandBuffer cmd, uint32_t frameIndex) {
    if (!skinnedMeshRenderer_) {
        return;
    }

    // Record draw calls for each visible NPC using their assigned bone slot.
    // The dynamic offset in bindDescriptorSets selects the correct bone matrices.
    // This replays renderData_ in the same order prepare() built it, keeping each
    // bone slot paired with the matrices uploaded for that character.
    for (const auto& data : renderData_) {
        if (!data.character) continue;
        skinnedMeshRenderer_->record(cmd, frameIndex, data.boneSlot, data.transform,
                                     *data.character, data.hueShift);
    }
}
