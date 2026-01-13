#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <entt/entt.hpp>

#include "Mesh.h"
#include "Texture.h"
#include "RenderableBuilder.h"
#include <optional>

// Configuration for rock generation and placement
struct RockConfig {
    int rockVariations = 5;           // Number of unique rock mesh variations
    int rocksPerVariation = 8;        // How many instances of each variation
    float minRadius = 0.3f;           // Minimum rock base radius
    float maxRadius = 1.5f;           // Maximum rock base radius
    float placementRadius = 80.0f;    // Radius from center to place rocks
    glm::vec2 placementCenter = glm::vec2(0.0f);  // Center point for rock placement (world coords)
    float minDistanceBetween = 3.0f;  // Minimum distance between rocks
    float roughness = 0.35f;          // Surface roughness for rock generation
    float asymmetry = 0.25f;          // How non-spherical rocks should be
    int subdivisions = 3;             // Icosphere subdivision level (3 = ~320 triangles)
    float materialRoughness = 0.7f;   // PBR roughness for rendering
    float materialMetallic = 0.0f;    // PBR metallic for rendering
};

// Local struct for physics queries (returned by getRockInstances)
// Note: Actual rock data is stored in ECS (RockInstance component in Components.h)
struct RockInstanceData {
    glm::vec3 position;
    float rotation;         // Y-axis rotation
    float scale;            // Uniform scale factor
    int meshVariation;      // Which mesh to use
};

class RockSystem {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkCommandPool commandPool;
        VkQueue graphicsQueue;
        VkPhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;  // Terrain height query
        float terrainSize;
        entt::registry* registry = nullptr;  // ECS registry for rock entities
    };

    /**
     * Factory: Create and initialize RockSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<RockSystem> create(const InitInfo& info, const RockConfig& config = {});

    ~RockSystem();

    // Non-copyable, non-movable
    RockSystem(const RockSystem&) = delete;
    RockSystem& operator=(const RockSystem&) = delete;
    RockSystem(RockSystem&&) = delete;
    RockSystem& operator=(RockSystem&&) = delete;

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<Renderable>& getSceneObjects() const { return sceneObjects; }
    std::vector<Renderable>& getSceneObjects() { return sceneObjects; }

    // Access to textures for descriptor set binding
    Texture& getRockTexture() { return *rockTexture; }
    Texture& getRockNormalMap() { return *rockNormalMap; }

    // Get rock count for statistics (queries ECS)
    size_t getRockCount() const;
    size_t getMeshVariationCount() const { return rockMeshes.size(); }

    // Get rock instances for physics integration (builds from ECS)
    std::vector<RockInstanceData> getRockInstances() const;

    // Get rock meshes for physics collision shapes
    const std::vector<Mesh>& getRockMeshes() const { return rockMeshes; }

private:
    RockSystem() = default;  // Private: use factory

    bool initInternal(const InitInfo& info, const RockConfig& config);
    void cleanup();
    bool createRockMeshes(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    void generateRockPlacements(const InitInfo& info);
    void createSceneObjects();

    // Hash function for deterministic random placement
    float hashPosition(float x, float z, uint32_t seed) const;

    RockConfig config;

    // Stored for RAII cleanup
    VmaAllocator storedAllocator = VK_NULL_HANDLE;
    VkDevice storedDevice = VK_NULL_HANDLE;

    // ECS registry for rock entities (not owned)
    entt::registry* registry_ = nullptr;

    // Rock mesh variations
    std::vector<Mesh> rockMeshes;

    // Rock textures (RAII-managed)
    std::unique_ptr<Texture> rockTexture;
    std::unique_ptr<Texture> rockNormalMap;

    // Scene objects for rendering (built from ECS queries)
    std::vector<Renderable> sceneObjects;
};
