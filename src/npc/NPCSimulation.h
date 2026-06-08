#pragma once

#include "NPCTypes.h"
#include "ecs/World.h"
#include "ecs/Components.h"
#include "animation/AnimationArchetypeManager.h"
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

class AnimatedCharacter;
struct SkinnedMesh;

// Forward declare for height query function
using HeightQueryFunc = std::function<float(float, float)>;

// NPC Simulation System
// Handles behavior, state updates, and LOD-based scheduling for NPCs
// Separated from rendering for clean architecture
class NPCSimulation {
public:
    // Passkey for controlled construction
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit NPCSimulation(ConstructToken);

    struct InitInfo {
        VmaAllocator allocator;
        VkDevice device;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        std::string resourcePath;
        HeightQueryFunc getTerrainHeight;  // Query terrain height for placement
        glm::vec2 sceneOrigin = glm::vec2(0.0f);  // World XZ offset for scene objects
        ecs::World* ecsWorld = nullptr;  // Optional ECS world for entity creation
    };

    // Spawn info for creating NPCs
    struct NPCSpawnInfo {
        float x, z;        // Position offset from scene origin
        float yawDegrees;  // Facing direction
        uint32_t templateIndex = 0;  // Which character template to use
        NPCActivity activity = NPCActivity::Idle;  // Initial activity state
    };

    /**
     * Factory: Create and initialize NPCSimulation.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<NPCSimulation> create(const InitInfo& info);

    ~NPCSimulation();

    // Non-copyable, non-movable
    NPCSimulation(const NPCSimulation&) = delete;
    NPCSimulation& operator=(const NPCSimulation&) = delete;
    NPCSimulation(NPCSimulation&&) = delete;
    NPCSimulation& operator=(NPCSimulation&&) = delete;

    // Spawn NPCs at predefined positions (called during scene setup)
    // Returns number of NPCs successfully created
    size_t spawnNPCs(const std::vector<NPCSpawnInfo>& spawnPoints);

    // Get animated character for a specific NPC (for rendering)
    AnimatedCharacter* getCharacter(size_t npcIndex);
    const AnimatedCharacter* getCharacter(size_t npcIndex) const;

    // Check if NPCs are available
    bool hasNPCs() const { return !characters_.empty(); }
    size_t getNPCCount() const { return characters_.size(); }

    // Build world transform matrix for an NPC (reads the NPC's authoritative ECS
    // Transform on its simulation entity).
    glm::mat4 buildNPCTransform(size_t npcIndex) const;

    // LOD configuration (uses CharacterLODConfig thresholds)
    void setLODEnabled(bool enabled) { lodEnabled_ = enabled; }
    bool isLODEnabled() const { return lodEnabled_; }

    // Set (or late-bind) the ECS world. NPCs are spawned during scene init, which in
    // the deferred path runs BEFORE the world is available; calling this once the world
    // exists creates each spawned NPC's simulation entity (idempotent). The renderer
    // queries those entities, so this must run before the first NPC draw.
    void setECSWorld(ecs::World* world);

    // ECS integration - get entity for an NPC
    ecs::Entity getNPCEntity(size_t npcIndex) const {
        return npcIndex < npcEntities_.size() ? npcEntities_[npcIndex] : ecs::NullEntity;
    }

    // Get all NPC entities
    const std::vector<ecs::Entity>& getNPCEntities() const { return npcEntities_; }

    // Check if ECS mode is enabled
    bool isECSEnabled() const { return ecsWorld_ != nullptr; }

    // ECS-based update (alternative to legacy update)
    void updateECS(float deltaTime, const glm::vec3& cameraPos);

    // ==========================================================================
    // Shared Archetype Mode (Phase 2.2)
    // ==========================================================================
    // When enabled, NPCs share animation data via archetypes instead of
    // owning individual AnimatedCharacter instances.

    // Enable shared archetype mode for new NPCs
    // When enabled, spawnNPCs creates archetypes and uses NPCAnimationInstance
    void setUseSharedArchetypes(bool enable) { useSharedArchetypes_ = enable; }
    bool isUsingSharedArchetypes() const { return useSharedArchetypes_; }

    // Spawn NPCs using shared archetypes (memory-efficient mode)
    // Returns number of NPCs successfully created
    size_t spawnNPCsWithArchetypes(const std::vector<NPCSpawnInfo>& spawnPoints);

    // Update NPCs using shared archetypes
    void updateArchetypeMode(float deltaTime, const glm::vec3& cameraPos, uint32_t currentFrame);

    // Get archetype manager (for external access to shared data)
    AnimationArchetypeManager& getArchetypeManager() { return archetypeManager_; }
    const AnimationArchetypeManager& getArchetypeManager() const { return archetypeManager_; }

    // Get skinned mesh for archetype (for rendering)
    SkinnedMesh* getArchetypeSkinnedMesh(uint32_t archetypeId);

    // Statistics for archetype mode
    struct ArchetypeStats {
        size_t archetypeCount = 0;
        size_t totalBones = 0;
        size_t totalAnimations = 0;
        size_t npcCount = 0;
        size_t memorySaved = 0;  // Approximate bytes saved vs per-NPC mode
    };
    ArchetypeStats getArchetypeStats() const;

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Stored initialization data
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::string resourcePath_;
    HeightQueryFunc terrainHeightFunc_;
    glm::vec2 sceneOrigin_ = glm::vec2(0.0f);

    // Character instances, one per NPC, in spawn order. characters_[i] corresponds
    // to npcEntities_[i] (the NPC's simulation entity); this push-order alignment is
    // the only retained index invariant and is never derived from entt view order.
    std::vector<std::unique_ptr<AnimatedCharacter>> characters_;

    // Create the simulation entity for spawned NPC index i (Transform + tags + LOD +
    // SkinnedMeshRef + hue). Requires ecsWorld_ set and characters_[i]/npcSpawnData_[i].
    void createNPCSimEntity(size_t i);

    // Per-NPC spawn data retained so simulation entities can be created when the ECS
    // world arrives after spawn (deferred path). Parallel to characters_ by spawn order.
    struct NPCSpawnData {
        glm::vec3 worldPos = glm::vec3(0.0f);
        float yawDegrees = 0.0f;
        ecs::NPCActivity activity = ecs::NPCActivity::Idle;
        uint32_t templateIndex = 0;
    };
    std::vector<NPCSpawnData> npcSpawnData_;

    // ECS integration
    ecs::World* ecsWorld_ = nullptr;
    std::vector<ecs::Entity> npcEntities_;  // Simulation entity per NPC (spawn order)

    // LOD configuration
    bool lodEnabled_ = true;

    // ==========================================================================
    // Shared Archetype Mode (Phase 2.2)
    // ==========================================================================
    AnimationArchetypeManager archetypeManager_;
    bool useSharedArchetypes_ = false;

    // Archetype-specific data
    struct ArchetypeData {
        std::unique_ptr<SkinnedMesh> skinnedMesh;  // One per archetype
        std::unique_ptr<Mesh> renderMesh;           // For bounds
        size_t idleClipIndex = 0;
        size_t walkClipIndex = 0;
        size_t runClipIndex = 0;
    };
    std::unordered_map<uint32_t, ArchetypeData> archetypeRenderData_;

    // Helper to create archetype from character
    uint32_t createArchetypeFromCharacter(const std::string& name, AnimatedCharacter& character);

    // Helper to find animation indices in archetype
    void findAnimationIndices(const AnimationArchetype& archetype, ArchetypeData& data);
};
