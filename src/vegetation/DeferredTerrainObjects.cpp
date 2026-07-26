// DeferredTerrainObjects.cpp - Deferred terrain object generation
// Handles scene objects, trees, and detritus creation after terrain is ready

#include "DeferredTerrainObjects.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "TreeRenderer.h"
#include "ScatterSystem.h"
#include "SceneManager.h"
#include "SceneBuilder.h"
#include "world/BiomeMap.h"
#include <SDL3/SDL.h>
#include <chrono>
#include <thread>

std::unique_ptr<DeferredTerrainObjects> DeferredTerrainObjects::create(const Config& config) {
    auto instance = std::make_unique<DeferredTerrainObjects>(ConstructToken{});
    if (!instance->initInternal(config)) {
        return nullptr;
    }
    return instance;
}

bool DeferredTerrainObjects::initInternal(const Config& config) {
    config_ = config;
    return true;
}

bool DeferredTerrainObjects::tryGenerate(
    SceneManager* sceneManager,
    TreeSystem* tree,
    TreeLODSystem* treeLOD,
    ImpostorCullSystem* impostorCull,
    TreeRenderer* treeRenderer,
    ScatterSystem* rocks,
    std::unique_ptr<ScatterSystem>& detritus,
    bool terrainReady
) {
    // Already generated - nothing to do
    if (generated_) {
        return false;
    }

    // Wait for terrain to be ready
    if (!terrainReady) {
        return false;
    }

    // First call: quick setup, then queue background tree generation.
    // The frame loop keeps rendering while workers generate the forest;
    // completed trees are uploaded a budgeted batch per frame.
    if (!generating_) {
        generating_ = true;
        startGeneration(sceneManager, tree);

        // Terrain-dependent world content (settlement buildings, bridges,
        // road/river ribbons) only needs terrain heights, not trees - kick it
        // off now so the world populates while the forest generates.
        if (onGenerated_) {
            onGenerated_();
        }
    }

    if (tree && treeGen_) {
        if (!uploadCompletedTrees(*tree)) {
            return false;  // Still generating/uploading - come back next frame
        }
        treeGen_.reset();
    }

    return finalizeGeneration(tree, treeLOD, impostorCull, treeRenderer, detritus);
}

bool DeferredTerrainObjects::startGeneration(SceneManager* sceneManager, TreeSystem* tree) {
    SDL_Log("DeferredTerrainObjects: Terrain ready, generating scene and vegetation content...");

    // Create scene objects (player, crates, etc.) now that terrain heights are available
    if (sceneManager) {
        SceneBuilder& builder = sceneManager->getSceneBuilder();
        if (!builder.hasRenderables()) {
            builder.createRenderablesDeferred();
            SDL_Log("DeferredTerrainObjects: Scene objects created");
        }
    }

    if (!tree) {
        return true;
    }

    // Trees stream in over many frames; renderers must not touch the
    // unfinalized leaf instance buffer / culling data until finalize
    tree->setRenderReady(false);

    VegetationContentGenerator::Config vegConfig;
    vegConfig.resourcePath = config_.resourcePath;
    vegConfig.getTerrainHeight = config_.getTerrainHeight;
    vegConfig.terrainSize = config_.terrainSize;
    VegetationContentGenerator vegGen(vegConfig);

    vegGen.generateDemoTrees(*tree, config_.sceneOrigin);

    // Biome-driven forest when a biome map is available, otherwise the legacy
    // fixed forest disk (small enough to generate synchronously)
    if (config_.biomeMap && config_.biomeMap->isLoaded()) {
        auto requests = vegGen.buildBiomeForestRequests(
            *config_.biomeMap, config_.settlements, config_.sceneOrigin,
            config_.biomeRegionRadius, config_.maxBiomeTrees, config_.seaLevel);

        if (!requests.empty()) {
            uint32_t workers = std::max(4u, std::thread::hardware_concurrency() > 2
                ? std::thread::hardware_concurrency() - 2 : 4u);
            treeGen_ = ThreadedTreeGenerator::create(workers);
            if (treeGen_) {
                treeGen_->queueTrees(requests);
                SDL_Log("DeferredTerrainObjects: Queued %zu biome trees for background generation",
                        requests.size());
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Threaded tree generator unavailable, generating serially");
                for (const auto& req : requests) {
                    tree->addTree(req.position, req.rotation, req.scale, req.options);
                }
            }
        }
    } else {
        vegGen.generateForest(*tree, config_.forestCenter, config_.forestRadius, config_.maxTrees);
    }

    return true;
}

bool DeferredTerrainObjects::uploadCompletedTrees(TreeSystem& tree) {
    // GPU-upload budget per frame; generation continues on workers regardless
    constexpr float kUploadBudgetMs = 4.0f;
    const auto start = std::chrono::steady_clock::now();

    auto newlyStaged = treeGen_->getCompletedTrees();
    stagedBacklog_.insert(stagedBacklog_.end(),
                          std::make_move_iterator(newlyStaged.begin()),
                          std::make_move_iterator(newlyStaged.end()));

    size_t uploaded = 0;
    while (uploaded < stagedBacklog_.size()) {
        auto& staged = stagedBacklog_[uploaded];
        uint32_t treeIdx = tree.addTreeFromStagedData(
            staged.position, staged.rotation, staged.scale,
            staged.options,
            staged.branchVertexData, staged.branchVertexCount,
            staged.branchIndices,
            staged.leafInstanceData, staged.leafInstanceCount,
            staged.archetypeIndex);
        if (treeIdx != UINT32_MAX) {
            uploadedTreeCount_++;
        }
        ++uploaded;

        float elapsedMs = std::chrono::duration<float, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsedMs > kUploadBudgetMs) {
            break;
        }
    }
    stagedBacklog_.erase(stagedBacklog_.begin(), stagedBacklog_.begin() + uploaded);

    if (!treeGen_->isComplete() || !stagedBacklog_.empty()) {
        return false;
    }

    tree.finalizeLeafInstanceBuffer();
    SDL_Log("DeferredTerrainObjects: Biome forest - %d trees placed", uploadedTreeCount_);
    return true;
}

bool DeferredTerrainObjects::finalizeGeneration(
    TreeSystem* tree,
    TreeLODSystem* treeLOD,
    ImpostorCullSystem* impostorCull,
    TreeRenderer* treeRenderer,
    std::unique_ptr<ScatterSystem>& detritus
) {
    VegetationContentGenerator::Config vegConfig;
    vegConfig.resourcePath = config_.resourcePath;
    vegConfig.getTerrainHeight = config_.getTerrainHeight;
    vegConfig.terrainSize = config_.terrainSize;
    VegetationContentGenerator vegGen(vegConfig);

    if (tree) {
        // Generate impostor archetypes
        if (treeLOD) {
            vegGen.generateImpostorArchetypes(*tree, *treeLOD);
        }

        // Finalize tree systems
        vegGen.finalizeTreeSystems(
            *tree,
            treeLOD,
            impostorCull,
            treeRenderer,
            config_.uniformBuffers,
            config_.shadowView,
            config_.shadowSampler
        );

        // Invoke callback to create physics colliders for the generated trees
        if (onTreesGenerated_) {
            onTreesGenerated_(*tree);
        }

        tree->setRenderReady(true);
        SDL_Log("DeferredTerrainObjects: Tree generation complete");
    }

    // Create detritus system (fallen branches scattered near trees)
    if (tree && config_.device != VK_NULL_HANDLE) {
        VegetationContentGenerator::DetritusCreateInfo detritusInfo{
            config_.device,
            config_.allocator,
            config_.commandPool,
            config_.graphicsQueue,
            config_.physicalDevice
        };

        detritus = vegGen.createDetritusSystem(detritusInfo, *tree);

        // Create descriptor sets for detritus if we have a pool
        if (detritus && config_.descriptorPool && getCommonBindings_) {
            if (!detritus->createDescriptorSets(
                    config_.device,
                    *config_.descriptorPool,
                    config_.descriptorSetLayout,
                    config_.framesInFlight,
                    getCommonBindings_)) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "DeferredTerrainObjects: Failed to create detritus descriptor sets");
            }
        }

        SDL_Log("DeferredTerrainObjects: Detritus generation complete");
    }

    // Mark as done (onGenerated_ already fired when generation started -
    // settlements/bridges/ribbons depend on terrain only, not trees)
    generated_ = true;
    generating_ = false;

    SDL_Log("DeferredTerrainObjects: All vegetation content generated");
    return true;
}
