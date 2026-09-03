#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>
#include <functional>
#include <cstdint>

#include "SceneObjectInstance.h"
#include "Mesh.h"
#include "Texture.h"
#include "ecs/Components.h"

/**
 * SceneMaterial - Represents a material with textures, properties, mesh variations, and instances
 *
 * Manages the complete rendering data for a material type:
 * - Multiple mesh variations (e.g., different rock shapes)
 * - Diffuse and normal textures
 * - Instance transforms (position, rotation, scale, mesh variation)
 * - Renderable generation for the rendering pipeline
 * - Material properties (roughness, metallic, shadow casting)
 *
 * This class uses composition rather than inheritance. Systems like ScatterSystem
 * own a SceneMaterial and delegate common operations to it.
 */
class SceneMaterial {
public:
    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        vk::CommandPool commandPool;
        vk::Queue graphicsQueue;
        vk::PhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;
        float terrainSize;
    };

    struct MaterialProperties {
        float roughness = 0.7f;
        float metallic = 0.0f;
        bool castsShadow = true;

        static MaterialProperties defaults() { return {0.7f, 0.0f, true}; }
    };

    explicit SceneMaterial(MaterialProperties props = MaterialProperties::defaults())
        : materialProps_(props) {}
    // Members release themselves: meshes_ (Mesh frees GPU buffers in ~Mesh) and
    // the unique_ptr textures, in reverse declaration order.
    ~SceneMaterial() = default;

    // Non-copyable, non-movable (owns GPU resources)
    SceneMaterial(const SceneMaterial&) = delete;
    SceneMaterial& operator=(const SceneMaterial&) = delete;
    SceneMaterial(SceneMaterial&&) = delete;
    SceneMaterial& operator=(SceneMaterial&&) = delete;

    /**
     * Set material properties used when rebuilding scene objects
     */
    void setMaterialProperties(const MaterialProperties& matProps) { materialProps_ = matProps; }

    /**
     * Transitional shim for callers that still pass a Vulkan InitInfo: the
     * material owns no raw handles, so only the properties are used.
     * Prefer setMaterialProperties(); InitInfo/init() are scheduled for removal.
     */
    void init(const InitInfo&, const MaterialProperties& matProps = MaterialProperties::defaults()) {
        setMaterialProperties(matProps);
    }

    /**
     * Set the meshes for this material (transfers ownership)
     * Caller should have already uploaded meshes to GPU
     */
    void setMeshes(std::vector<Mesh>&& meshes);

    /**
     * Set the diffuse texture (transfers ownership)
     */
    void setDiffuseTexture(std::unique_ptr<Texture> texture);

    /**
     * Set the normal map texture (transfers ownership)
     */
    void setNormalTexture(std::unique_ptr<Texture> texture);

    /**
     * Add an instance to the material
     */
    void addInstance(const SceneObjectInstance& instance);

    /**
     * Set all instances at once (replaces existing)
     */
    void setInstances(std::vector<SceneObjectInstance>&& instances);

    /**
     * Clear all instances
     */
    void clearInstances();

    /**
     * Rebuild renderable scene objects from current instances and meshes
     * Must be called after adding/modifying instances
     *
     * @param transformModifier Optional callback to modify the transform matrix
     *        (e.g., for sinking rocks into ground or adding terrain-conform tilt)
     */
    void rebuildSceneObjects(
        std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier = nullptr);

    /**
     * Release all content (meshes, textures, instances, scene objects).
     * Idempotent; the destructor releases the same members automatically.
     */
    void cleanup();

    // ========================================================================
    // Accessors
    // ========================================================================

    // Get scene objects for rendering
    const std::vector<ecs::RenderData>& getSceneObjects() const { return sceneObjects_; }
    std::vector<ecs::RenderData>& getSceneObjects() { return sceneObjects_; }

    // Get instances for physics/other systems
    const std::vector<SceneObjectInstance>& getInstances() const { return instances_; }

    // Get meshes for physics collision shapes
    const std::vector<Mesh>& getMeshes() const { return meshes_; }

    // Access textures for descriptor set binding
    Texture* getDiffuseTexture() { return diffuseTexture_.get(); }
    const Texture* getDiffuseTexture() const { return diffuseTexture_.get(); }
    Texture* getNormalTexture() { return normalTexture_.get(); }
    const Texture* getNormalTexture() const { return normalTexture_.get(); }

    // Statistics
    size_t getInstanceCount() const { return instances_.size(); }
    size_t getMeshVariationCount() const { return meshes_.size(); }

    // Material properties
    const MaterialProperties& getMaterialProperties() const { return materialProps_; }

    // Check if the material has content
    bool hasContent() const { return !instances_.empty() && !meshes_.empty(); }

private:
    // Material properties for renderables
    MaterialProperties materialProps_;

    // Mesh variations
    std::vector<Mesh> meshes_;

    // Textures (RAII-managed)
    std::unique_ptr<Texture> diffuseTexture_;
    std::unique_ptr<Texture> normalTexture_;

    // Instance transforms
    std::vector<SceneObjectInstance> instances_;

    // Scene objects for rendering (generated from instances + meshes)
    std::vector<ecs::RenderData> sceneObjects_;
};
