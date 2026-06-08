#include "NPCSimulation.h"
#include "AnimatedCharacter.h"
#include "animation/SkinnedMesh.h"
#include "ecs/Systems.h"
#include <SDL3/SDL.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>

// Constructor must be defined in .cpp to allow unique_ptr<AnimatedCharacter> with incomplete type in header
NPCSimulation::NPCSimulation(ConstructToken) {}

std::unique_ptr<NPCSimulation> NPCSimulation::create(const InitInfo& info) {
    auto simulation = std::make_unique<NPCSimulation>(ConstructToken{});
    if (!simulation->initInternal(info)) {
        return nullptr;
    }
    return simulation;
}

NPCSimulation::~NPCSimulation() {
    cleanup();
}

bool NPCSimulation::initInternal(const InitInfo& info) {
    allocator_ = info.allocator;
    device_ = info.device;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    resourcePath_ = info.resourcePath;
    terrainHeightFunc_ = info.getTerrainHeight;
    sceneOrigin_ = info.sceneOrigin;
    ecsWorld_ = info.ecsWorld;

    return true;
}

void NPCSimulation::cleanup() {
    // Destroy ECS entities if ECS is enabled
    if (ecsWorld_) {
        for (ecs::Entity entity : npcEntities_) {
            if (ecsWorld_->valid(entity)) {
                ecsWorld_->destroy(entity);
            }
        }
        npcEntities_.clear();
    }

    // Clean up archetype render data (GPU resources)
    for (auto& [id, data] : archetypeRenderData_) {
        if (data.skinnedMesh) {
            data.skinnedMesh->destroy(allocator_);
        }
    }
    archetypeRenderData_.clear();
    archetypeManager_.clear();

    characters_.clear();
}

size_t NPCSimulation::spawnNPCs(const std::vector<NPCSpawnInfo>& spawnPoints) {
    if (spawnPoints.empty()) {
        return 0;
    }

    // Reserve space
    characters_.reserve(spawnPoints.size());
    if (ecsWorld_) {
        npcEntities_.reserve(spawnPoints.size());
    }

    std::string characterPath = resourcePath_ + "/assets/characters/fbx/Y Bot.fbx";

    // Additional animation files to load (same as player character)
    std::vector<std::string> additionalAnimations = {
        resourcePath_ + "/assets/characters/fbx/sword and shield idle.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield walk.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield run.fbx",
        resourcePath_ + "/assets/characters/fbx/sword and shield jump.fbx"
    };

    size_t createdCount = 0;

    for (const auto& spawn : spawnPoints) {
        AnimatedCharacter::InitInfo charInfo{};
        charInfo.path = characterPath;
        charInfo.allocator = allocator_;
        charInfo.device = device_;
        charInfo.commandPool = commandPool_;
        charInfo.queue = graphicsQueue_;

        auto character = AnimatedCharacter::create(charInfo);
        if (!character) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "NPCSimulation: Failed to create NPC character at (%.1f, %.1f)",
                spawn.x, spawn.z);
            continue;
        }

        // Load animations
        character->loadAdditionalAnimations(additionalAnimations);

        // Calculate world position
        glm::vec3 worldPos(spawn.x + sceneOrigin_.x, 0.0f, spawn.z + sceneOrigin_.y);

        // Get terrain height
        if (terrainHeightFunc_) {
            worldPos.y = terrainHeightFunc_(worldPos.x, worldPos.z);
        }

        // Spawn index: characters_ and npcEntities_ are appended in lockstep below,
        // so the next index equals the current size.
        size_t npcIndex = characters_.size();

        // Convert activity enum for ECS
        ecs::NPCActivity ecsActivity = ecs::NPCActivity::Idle;
        switch (spawn.activity) {
            case NPCActivity::Walking: ecsActivity = ecs::NPCActivity::Walking; break;
            case NPCActivity::Running: ecsActivity = ecs::NPCActivity::Running; break;
            default: break;
        }

        // Retain the character and its spawn data in spawn order. The simulation
        // entity is created now if the ECS world is already set, or later via
        // setECSWorld() (the deferred path spawns NPCs before the world exists).
        characters_.push_back(std::move(character));
        npcSpawnData_.push_back(NPCSpawnData{worldPos, spawn.yawDegrees, ecsActivity, spawn.templateIndex});
        if (ecsWorld_) {
            createNPCSimEntity(npcIndex);
        }

        const char* activityName = spawn.activity == NPCActivity::Idle ? "idle" :
                                   spawn.activity == NPCActivity::Walking ? "walking" : "running";
        SDL_Log("NPCSimulation: Created NPC %zu at (%.1f, %.1f, %.1f) facing %.0f degrees (%s)%s",
                npcIndex, worldPos.x, worldPos.y, worldPos.z, spawn.yawDegrees, activityName,
                ecsWorld_ ? " [ECS]" : "");

        createdCount++;
    }

    SDL_Log("NPCSimulation: Created %zu NPCs%s", createdCount, ecsWorld_ ? " with ECS entities" : "");
    return createdCount;
}

void NPCSimulation::createNPCSimEntity(size_t i) {
    if (!ecsWorld_ || i >= characters_.size() || i >= npcSpawnData_.size()) return;

    const NPCSpawnData& sd = npcSpawnData_[i];
    AnimatedCharacter* character = characters_[i].get();

    ecs::Entity entity = ecsWorld_->create();

    // Transform - position with height offset for character center
    constexpr float CHARACTER_HEIGHT_OFFSET = 0.9f;
    glm::mat4 transform = glm::mat4(1.0f);
    transform = glm::translate(transform, sd.worldPos + glm::vec3(0.0f, CHARACTER_HEIGHT_OFFSET, 0.0f));
    transform = glm::rotate(transform, glm::radians(sd.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    ecsWorld_->add<ecs::Transform>(entity, transform);

    // NPC identification
    ecsWorld_->add<ecs::NPCTag>(entity, sd.templateIndex);
    ecsWorld_->add<ecs::NPCFacing>(entity, sd.yawDegrees);

    // Per-NPC hue tint (golden-ratio distribution; matches SceneBuilder/NPCRenderer formula).
    {
        constexpr float GOLDEN_RATIO = 1.618033988749895f;
        constexpr float TWO_PI = 6.283185307179586f;
        float hueShift = std::fmod(static_cast<float>(i + 1) * GOLDEN_RATIO, 1.0f) * TWO_PI;
        ecsWorld_->add<ecs::NPCHueShift>(entity, hueShift);
    }

    // Animation state
    ecs::NPCAnimationState animState;
    animState.activity = sd.activity;
    ecsWorld_->add<ecs::NPCAnimationState>(entity, animState);

    // LOD controller
    ecsWorld_->add<ecs::NPCLODController>(entity);

    // Skinned mesh reference (link to AnimatedCharacter)
    ecsWorld_->add<ecs::SkinnedMeshRef>(entity, character, static_cast<uint32_t>(i));

    // Bounding sphere for culling (approximate character bounds)
    ecsWorld_->add<ecs::BoundingSphere>(entity, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);

    // Mark as visible initially
    ecsWorld_->add<ecs::Visible>(entity);

    npcEntities_.push_back(entity);
}

void NPCSimulation::setECSWorld(ecs::World* world) {
    ecsWorld_ = world;
    if (!ecsWorld_) return;
    if (!npcEntities_.empty()) return;  // already created (NPCs spawned with a world)

    // Deferred path: NPCs spawned before the world existed. Create their simulation
    // entities now, in spawn order, so the renderer's ECS view finds them.
    for (size_t i = 0; i < characters_.size(); ++i) {
        createNPCSimEntity(i);
    }
    if (!npcEntities_.empty()) {
        SDL_Log("NPCSimulation: created %zu NPC simulation entities (deferred ECS world)",
                npcEntities_.size());
    }
}

glm::mat4 NPCSimulation::buildNPCTransform(size_t npcIndex) const {
    // The NPC's authoritative world transform lives on its simulation entity.
    if (ecsWorld_ && npcIndex < npcEntities_.size()) {
        ecs::Entity entity = npcEntities_[npcIndex];
        if (ecsWorld_->valid(entity) && ecsWorld_->has<ecs::Transform>(entity)) {
            return ecsWorld_->get<ecs::Transform>(entity).matrix;
        }
    }
    return glm::mat4(1.0f);
}

AnimatedCharacter* NPCSimulation::getCharacter(size_t npcIndex) {
    if (npcIndex >= characters_.size()) return nullptr;
    return characters_[npcIndex].get();
}

const AnimatedCharacter* NPCSimulation::getCharacter(size_t npcIndex) const {
    if (npcIndex >= characters_.size()) return nullptr;
    return characters_[npcIndex].get();
}

void NPCSimulation::updateECS(float deltaTime, const glm::vec3& cameraPos) {
    if (!ecsWorld_ || npcEntities_.empty()) {
        return;
    }

    // Update LOD levels based on camera distance using ECS query
    for (auto [entity, transform, lodCtrl] : ecsWorld_->view<ecs::Transform, ecs::NPCLODController>().each()) {
        glm::vec3 npcPos = transform.position();
        float distance = glm::distance(cameraPos, npcPos);

        ecs::NPCLODLevel newLevel;
        if (distance < ecs::NPCLODController::DISTANCE_REAL) {
            newLevel = ecs::NPCLODLevel::Real;
        } else if (distance < ecs::NPCLODController::DISTANCE_BULK) {
            newLevel = ecs::NPCLODLevel::Bulk;
        } else {
            newLevel = ecs::NPCLODLevel::Virtual;
        }

        // Reset frame counter on LOD change
        if (lodCtrl.level != newLevel) {
            lodCtrl.framesSinceUpdate = 0;
        }

        lodCtrl.level = newLevel;
        lodCtrl.framesSinceUpdate++;
    }

    // Update NPC animations using ECS query
    for (auto [entity, transform, lodCtrl, animState, skinnedRef] :
         ecsWorld_->view<ecs::Transform, ecs::NPCLODController, ecs::NPCAnimationState, ecs::SkinnedMeshRef>().each()) {

        // Check if we should update this frame based on LOD
        if (!lodCtrl.shouldUpdate()) {
            // Skip update, use cached bones
            if (skinnedRef.valid()) {
                auto* character = static_cast<AnimatedCharacter*>(skinnedRef.character);
                if (character) {
                    character->setSkipAnimationUpdate(true);
                }
            }
            continue;
        }

        // Reset frame counter after update
        lodCtrl.framesSinceUpdate = 0;

        if (!skinnedRef.valid()) continue;

        auto* character = static_cast<AnimatedCharacter*>(skinnedRef.character);
        if (!character) continue;

        character->setSkipAnimationUpdate(false);

        // Get movement speed from activity
        float movementSpeed = ecs::NPCLODController::getMovementSpeed(animState.activity);

        // Calculate effective delta time for LOD-adjusted updates
        float effectiveDelta = deltaTime;
        if (lodCtrl.level == ecs::NPCLODLevel::Bulk) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_BULK);
        } else if (lodCtrl.level == ecs::NPCLODLevel::Virtual) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_VIRTUAL);
        }

        // Update animation
        character->update(effectiveDelta, allocator_, device_, commandPool_, graphicsQueue_,
                          movementSpeed, true, false, transform.matrix);
    }
}

// =============================================================================
// Shared Archetype Mode Implementation (Phase 2.2)
// =============================================================================

uint32_t NPCSimulation::createArchetypeFromCharacter(const std::string& name, AnimatedCharacter& character) {
    // Create archetype from character's animation data
    uint32_t archetypeId = archetypeManager_.createFromCharacter(name, character);

    // Store render data (skinned mesh, etc.)
    ArchetypeData renderData;

    // Transfer skinned mesh to archetype (share the GPU resources)
    // Note: In a full implementation, we'd upload once and share
    // For now, we keep reference to character's mesh
    renderData.skinnedMesh = std::make_unique<SkinnedMesh>();
    renderData.skinnedMesh->setData(SkinnedMeshData{
        character.getSkinnedMesh().getVertices(),
        character.getSkinnedMesh().getIndices(),
        character.getSkeleton()
    });
    renderData.skinnedMesh->upload(allocator_, device_, commandPool_, graphicsQueue_);

    // Find animation indices
    const AnimationArchetype* archetype = archetypeManager_.getArchetype(archetypeId);
    if (archetype) {
        findAnimationIndices(*archetype, renderData);
    }

    archetypeRenderData_[archetypeId] = std::move(renderData);

    return archetypeId;
}

void NPCSimulation::findAnimationIndices(const AnimationArchetype& archetype, ArchetypeData& data) {
    // Find idle, walk, run animations by name
    for (size_t i = 0; i < archetype.animations.size(); ++i) {
        std::string lowerName = archetype.animations[i].name;
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                       [](unsigned char c) { return std::tolower(c); });

        if (lowerName.find("idle") != std::string::npos) {
            data.idleClipIndex = i;
        } else if (lowerName.find("walk") != std::string::npos) {
            data.walkClipIndex = i;
        } else if (lowerName.find("run") != std::string::npos) {
            data.runClipIndex = i;
        }
    }

    SDL_Log("NPCSimulation: Archetype animation indices - idle=%zu, walk=%zu, run=%zu",
            data.idleClipIndex, data.walkClipIndex, data.runClipIndex);
}

size_t NPCSimulation::spawnNPCsWithArchetypes(const std::vector<NPCSpawnInfo>& spawnPoints) {
    if (spawnPoints.empty() || !ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "NPCSimulation: spawnNPCsWithArchetypes requires ECS world and spawn points");
        return 0;
    }

    // Load a single character to create the archetype from
    std::string characterPath = resourcePath_ + "/assets/characters/fbx/Y Bot.fbx";
    std::vector<std::string> additionalAnimations = {
        resourcePath_ + "/assets/characters/fbx/ss_idle.fbx",
        resourcePath_ + "/assets/characters/fbx/ss_walk.fbx",
        resourcePath_ + "/assets/characters/fbx/ss_run.fbx",
        resourcePath_ + "/assets/characters/fbx/ss_jump.fbx"
    };

    // Create a single character to extract archetype data from
    AnimatedCharacter::InitInfo charInfo{};
    charInfo.path = characterPath;
    charInfo.allocator = allocator_;
    charInfo.device = device_;
    charInfo.commandPool = commandPool_;
    charInfo.queue = graphicsQueue_;

    auto templateCharacter = AnimatedCharacter::create(charInfo);
    if (!templateCharacter) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "NPCSimulation: Failed to create template character for archetype");
        return 0;
    }

    templateCharacter->loadAdditionalAnimations(additionalAnimations);
    templateCharacter->buildBoneLODMasks();

    // Create archetype from this character
    uint32_t archetypeId = createArchetypeFromCharacter("humanoid", *templateCharacter);

    // Get archetype for bone count
    const AnimationArchetype* archetype = archetypeManager_.getArchetype(archetypeId);
    if (!archetype) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "NPCSimulation: Failed to get created archetype");
        return 0;
    }

    // Get archetype render data for animation indices
    auto& renderData = archetypeRenderData_[archetypeId];

    // Reserve space
    npcEntities_.reserve(spawnPoints.size());

    size_t createdCount = 0;

    for (const auto& spawn : spawnPoints) {
        // Calculate world position
        glm::vec3 worldPos(spawn.x + sceneOrigin_.x, 0.0f, spawn.z + sceneOrigin_.y);
        if (terrainHeightFunc_) {
            worldPos.y = terrainHeightFunc_(worldPos.x, worldPos.z);
        }

        // Spawn index: npcEntities_ is appended at the end of this iteration.
        size_t npcIndex = npcEntities_.size();

        // Create ECS entity
        ecs::Entity entity = ecsWorld_->create();

        // Transform
        constexpr float CHARACTER_HEIGHT_OFFSET = 0.9f;
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, worldPos + glm::vec3(0.0f, CHARACTER_HEIGHT_OFFSET, 0.0f));
        transform = glm::rotate(transform, glm::radians(spawn.yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
        ecsWorld_->add<ecs::Transform>(entity, transform);

        // NPC identification
        ecsWorld_->add<ecs::NPCTag>(entity, spawn.templateIndex);
        ecsWorld_->add<ecs::NPCFacing>(entity, spawn.yawDegrees);

        // Per-NPC hue tint (golden-ratio distribution; matches SceneBuilder/NPCRenderer formula).
        {
            constexpr float GOLDEN_RATIO = 1.618033988749895f;
            constexpr float TWO_PI = 6.283185307179586f;
            float hueShift = std::fmod(static_cast<float>(npcIndex + 1) * GOLDEN_RATIO, 1.0f) * TWO_PI;
            ecsWorld_->add<ecs::NPCHueShift>(entity, hueShift);
        }

        // Archetype reference (new in Phase 2.2)
        ecsWorld_->add<ecs::AnimationArchetypeRef>(entity, archetypeId);

        // Animation instance (per-NPC state using archetype)
        ecs::NPCAnimationInstance animInstance;
        animInstance.resizeBoneMatrices(archetype->getBoneCount());

        // Set initial animation based on activity
        ecs::NPCActivity ecsActivity = ecs::NPCActivity::Idle;
        switch (spawn.activity) {
            case NPCActivity::Walking:
                ecsActivity = ecs::NPCActivity::Walking;
                animInstance.currentClipIndex = renderData.walkClipIndex;
                break;
            case NPCActivity::Running:
                ecsActivity = ecs::NPCActivity::Running;
                animInstance.currentClipIndex = renderData.runClipIndex;
                break;
            default:
                animInstance.currentClipIndex = renderData.idleClipIndex;
                break;
        }

        // Randomize initial animation time for variety
        const AnimationClip* clip = archetype->getAnimation(animInstance.currentClipIndex);
        if (clip && clip->duration > 0.0f) {
            animInstance.currentTime = static_cast<float>(rand()) / RAND_MAX * clip->duration;
        }

        ecsWorld_->add<ecs::NPCAnimationInstance>(entity, std::move(animInstance));

        // Animation state (legacy component, kept for compatibility)
        ecs::NPCAnimationState animState;
        animState.activity = ecsActivity;
        ecsWorld_->add<ecs::NPCAnimationState>(entity, animState);

        // LOD controller
        ecsWorld_->add<ecs::NPCLODController>(entity);

        // Bounding sphere for culling
        ecsWorld_->add<ecs::BoundingSphere>(entity, glm::vec3(0.0f, 1.0f, 0.0f), 1.0f);

        // Mark as visible initially
        ecsWorld_->add<ecs::Visible>(entity);

        npcEntities_.push_back(entity);

        const char* activityName = spawn.activity == NPCActivity::Idle ? "idle" :
                                   spawn.activity == NPCActivity::Walking ? "walking" : "running";
        SDL_Log("NPCSimulation: Created NPC %zu with archetype at (%.1f, %.1f, %.1f) [%s]",
                npcIndex, worldPos.x, worldPos.y, worldPos.z, activityName);

        createdCount++;
    }

    SDL_Log("NPCSimulation: Created %zu NPCs using shared archetype (memory efficient mode)", createdCount);
    SDL_Log("NPCSimulation: Archetype has %u bones, %zu animations",
            archetype->getBoneCount(), archetype->animations.size());

    return createdCount;
}

void NPCSimulation::updateArchetypeMode(float deltaTime, const glm::vec3& cameraPos, uint32_t currentFrame) {
    if (!ecsWorld_ || npcEntities_.empty()) {
        return;
    }

    // Update LOD levels based on camera distance
    ecs::systems::updateNPCLODLevels(*ecsWorld_, cameraPos);
    ecs::systems::tickNPCFrameCounters(*ecsWorld_);

    // Update NPC animations using archetype data
    for (auto [entity, transform, archetypeRef, animInstance, lodCtrl, animState] :
         ecsWorld_->view<ecs::Transform, ecs::AnimationArchetypeRef, ecs::NPCAnimationInstance,
                         ecs::NPCLODController, ecs::NPCAnimationState>().each()) {

        if (!archetypeRef.valid()) continue;

        // Check if we should update this frame based on LOD
        if (!lodCtrl.shouldUpdate()) {
            continue;
        }

        // Reset frame counter after update
        lodCtrl.framesSinceUpdate = 0;

        // Get archetype
        const AnimationArchetype* archetype = archetypeManager_.getArchetype(archetypeRef.archetypeId);
        if (!archetype) continue;

        // Update LOD level for bone detail
        float distance = glm::distance(cameraPos, transform.position());
        ecs::systems::updateNPCAnimationLOD(lodCtrl, animInstance, distance);

        // Update animation selection based on activity
        auto& renderData = archetypeRenderData_[archetypeRef.archetypeId];
        size_t targetClip = ecs::systems::selectAnimationForActivity(
            animState.activity,
            renderData.idleClipIndex,
            renderData.walkClipIndex,
            renderData.runClipIndex
        );

        // Start blend if animation changed
        if (targetClip != animInstance.currentClipIndex && !animInstance.isBlending) {
            animInstance.startBlend(targetClip, 0.2f);  // 200ms blend
        }

        // Calculate effective delta time for LOD-adjusted updates
        float effectiveDelta = deltaTime;
        if (lodCtrl.level == ecs::NPCLODLevel::Bulk) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_BULK);
        } else if (lodCtrl.level == ecs::NPCLODLevel::Virtual) {
            effectiveDelta *= static_cast<float>(ecs::NPCLODController::INTERVAL_VIRTUAL);
        }

        // Update animation instance (advances time and computes bone matrices)
        updateAnimationInstance(animInstance, *archetype, effectiveDelta, currentFrame);
    }
}

SkinnedMesh* NPCSimulation::getArchetypeSkinnedMesh(uint32_t archetypeId) {
    auto it = archetypeRenderData_.find(archetypeId);
    if (it != archetypeRenderData_.end()) {
        return it->second.skinnedMesh.get();
    }
    return nullptr;
}

NPCSimulation::ArchetypeStats NPCSimulation::getArchetypeStats() const {
    ArchetypeStats stats;
    stats.archetypeCount = archetypeManager_.getArchetypeCount();
    stats.totalBones = archetypeManager_.getTotalBoneCount();
    stats.totalAnimations = archetypeManager_.getTotalAnimationCount();
    stats.npcCount = npcEntities_.size();

    // Estimate memory savings
    // Per-NPC AnimatedCharacter is roughly:
    //   - Skeleton: ~1KB per bone * 67 bones = ~67KB
    //   - Animations: ~50KB per clip * 5 clips = ~250KB
    //   - State machine, IK, etc: ~10KB
    //   Total: ~320KB per NPC
    // With archetypes, we only store ~8KB bone matrices per NPC
    constexpr size_t PER_NPC_WITHOUT_ARCHETYPE = 320 * 1024;
    constexpr size_t PER_NPC_WITH_ARCHETYPE = 8 * 1024;
    constexpr size_t PER_ARCHETYPE = 320 * 1024;  // Archetype itself

    if (stats.npcCount > 0 && stats.archetypeCount > 0) {
        size_t withoutArchetypes = stats.npcCount * PER_NPC_WITHOUT_ARCHETYPE;
        size_t withArchetypes = stats.archetypeCount * PER_ARCHETYPE +
                                 stats.npcCount * PER_NPC_WITH_ARCHETYPE;
        stats.memorySaved = (withoutArchetypes > withArchetypes) ?
                            (withoutArchetypes - withArchetypes) : 0;
    }

    return stats;
}
