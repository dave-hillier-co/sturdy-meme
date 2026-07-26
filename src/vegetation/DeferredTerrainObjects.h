#pragma once

#include "VegetationContentGenerator.h"
#include "ScatterSystemFactory.h"
#include "DescriptorManager.h"
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>
#include <vector>
#include <memory>
#include <functional>

class BiomeMap;
struct Settlement;
class TreeSystem;
class TreeLODSystem;
class ImpostorCullSystem;
class TreeRenderer;
class ScatterSystem;
class SceneManager;

/**
 * DeferredTerrainObjects - Defers creation of trees, rocks, and detritus
 * until terrain tiles are fully loaded.
 *
 * Instead of generating vegetation content during initialization (blocking startup),
 * this class stores the configuration and generates content on the first frame
 * after the terrain system reports tiles are ready.
 *
 * Usage:
 *   1. Create during init with configuration
 *   2. Call tryGenerate() each frame from VegetationUpdater
 *   3. Once generated, the class becomes inactive
 */
class DeferredTerrainObjects {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit DeferredTerrainObjects(ConstructToken) {}

    struct Config {
        std::string resourcePath;
        float terrainSize = 16384.0f;
        std::function<float(float, float)> getTerrainHeight;

        // Scene positioning
        glm::vec2 sceneOrigin{0.0f, 0.0f};

        // Forest configuration (fallback when no biome map is available)
        glm::vec2 forestCenter{0.0f, 0.0f};
        float forestRadius = 80.0f;
        int maxTrees = 500;

        // Biome-driven vegetation: when biomeMap is set, tree placement follows
        // the biome map over biomeRegionRadius instead of the fixed forest disk
        const BiomeMap* biomeMap = nullptr;
        const std::vector<Settlement>* settlements = nullptr;
        float biomeRegionRadius = 600.0f;
        int maxBiomeTrees = 1500;
        float seaLevel = 23.0f;

        // Descriptor resources needed for finalizing tree systems
        std::vector<vk::Buffer> uniformBuffers;
        vk::ImageView shadowView = VK_NULL_HANDLE;
        vk::Sampler shadowSampler = VK_NULL_HANDLE;

        // For creating detritus descriptor sets
        vk::Device device = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        vk::CommandPool commandPool = VK_NULL_HANDLE;
        vk::Queue graphicsQueue = VK_NULL_HANDLE;
        vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
        DescriptorManager::Pool* descriptorPool = nullptr;
        vk::DescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        uint32_t framesInFlight = 3;
    };

    using GetCommonBindingsFunc = std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)>;

    // Callback invoked after trees are generated, passing the TreeSystem with new trees
    using OnTreesGeneratedFunc = std::function<void(TreeSystem&)>;

    /**
     * Factory: Create DeferredTerrainObjects instance.
     */
    static std::unique_ptr<DeferredTerrainObjects> create(const Config& config);

    /**
     * Set the function to get common bindings for descriptor sets.
     * Must be called before tryGenerate() if detritus needs descriptor sets.
     */
    void setCommonBindingsFunc(GetCommonBindingsFunc func) { getCommonBindings_ = std::move(func); }

    /**
     * Set callback invoked after trees are generated.
     * Use this to create physics colliders for the generated trees.
     */
    void setOnTreesGeneratedCallback(OnTreesGeneratedFunc func) { onTreesGenerated_ = std::move(func); }

    /**
     * Set callback invoked once when deferred generation starts (terrain is
     * loaded and heights are queryable; trees may still be generating in the
     * background). Used for other terrain-dependent content such as
     * settlement building blockouts.
     */
    using OnGeneratedFunc = std::function<void()>;
    void setOnGeneratedCallback(OnGeneratedFunc func) { onGenerated_ = std::move(func); }

    /**
     * Attempt to generate terrain objects if not already done and terrain is ready.
     *
     * @param sceneManager SceneManager to trigger deferred scene object creation
     * @param tree TreeSystem to populate with trees
     * @param treeLOD Optional TreeLODSystem for impostor generation
     * @param impostorCull Optional ImpostorCullSystem
     * @param treeRenderer Optional TreeRenderer
     * @param rocks ScatterSystem to check if generation is needed (rocks already exist)
     * @param detritus Output pointer for created detritus system
     * @param terrainReady True if terrain tiles are loaded and ready
     *
     * @return True if generation completed this frame, false otherwise
     */
    bool tryGenerate(
        SceneManager* sceneManager,
        TreeSystem* tree,
        TreeLODSystem* treeLOD,
        ImpostorCullSystem* impostorCull,
        TreeRenderer* treeRenderer,
        ScatterSystem* rocks,
        std::unique_ptr<ScatterSystem>& detritus,
        bool terrainReady
    );

    /**
     * Check if generation has been completed.
     */
    bool isGenerated() const { return generated_; }

    /**
     * Check if currently generating (for progress tracking).
     */
    bool isGenerating() const { return generating_; }

private:
    bool initInternal(const Config& config);

    // First call: quick scene-object/demo-tree setup, then queue the biome
    // forest on background workers. Subsequent calls: upload completed trees
    // under a per-frame time budget, finalize once everything is placed.
    bool startGeneration(SceneManager* sceneManager, TreeSystem* tree);
    bool uploadCompletedTrees(TreeSystem& tree);
    bool finalizeGeneration(
        TreeSystem* tree,
        TreeLODSystem* treeLOD,
        ImpostorCullSystem* impostorCull,
        TreeRenderer* treeRenderer,
        std::unique_ptr<ScatterSystem>& detritus);

    Config config_;
    GetCommonBindingsFunc getCommonBindings_;
    OnTreesGeneratedFunc onTreesGenerated_;
    OnGeneratedFunc onGenerated_;
    bool generated_ = false;
    bool generating_ = false;

    // Incremental biome-forest generation state
    std::unique_ptr<ThreadedTreeGenerator> treeGen_;
    std::vector<ThreadedTreeGenerator::StagedTree> stagedBacklog_;
    int uploadedTreeCount_ = 0;
};
