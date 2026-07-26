// VegetationContentGenerator.cpp - Vegetation content generation

#include "VegetationContentGenerator.h"
#include "TreeSystem.h"
#include "TreeLODSystem.h"
#include "ImpostorCullSystem.h"
#include "TreeRenderer.h"
#include "ThreadedTreeGenerator.h"
#include "TreeOptions.h"
#include "ScatterSystem.h"
#include "ScatterSystemFactory.h"
#include "world/BiomeMap.h"
#include "world/SettlementRegistry.h"
#include "scene/DeterministicRandom.h"
#include <SDL3/SDL.h>
#include <filesystem>
#include <cmath>

VegetationContentGenerator::VegetationContentGenerator(const Config& config)
    : config_(config)
    , presetDir_(config.resourcePath + "/assets/trees/presets/")
{
}

VegetationContentGenerator::~VegetationContentGenerator() = default;

TreeOptions VegetationContentGenerator::loadPresetOrDefault(
    const std::string& presetName,
    TreeOptions (*defaultFn)()
) const {
    std::string path = presetDir_ + presetName;
    if (std::filesystem::exists(path)) {
        return TreeOptions::loadFromJson(path);
    }
    return defaultFn();
}

void VegetationContentGenerator::generateDemoTrees(
    TreeSystem& treeSystem,
    const glm::vec2& sceneOrigin
) {
    const float demoTreeX = sceneOrigin.x;
    const float demoTreeZ = sceneOrigin.y;

    // Oak tree
    float oakX = demoTreeX + 35.0f, oakZ = demoTreeZ + 25.0f;
    glm::vec3 oakPos(oakX, config_.getTerrainHeight(oakX, oakZ), oakZ);
    treeSystem.addTree(oakPos, 0.0f, 1.0f, loadPresetOrDefault("oak_large.json", TreeOptions::defaultOak));

    // Pine tree
    float pineX = demoTreeX + 50.0f, pineZ = demoTreeZ - 30.0f;
    glm::vec3 pinePos(pineX, config_.getTerrainHeight(pineX, pineZ), pineZ);
    treeSystem.addTree(pinePos, 0.5f, 1.0f, loadPresetOrDefault("pine_large.json", TreeOptions::defaultPine));

    // Ash tree
    float ashX = demoTreeX - 40.0f, ashZ = demoTreeZ - 25.0f;
    glm::vec3 ashPos(ashX, config_.getTerrainHeight(ashX, ashZ), ashZ);
    treeSystem.addTree(ashPos, 1.0f, 1.0f, loadPresetOrDefault("ash_large.json", TreeOptions::defaultOak));

    // Aspen tree
    float aspenX = demoTreeX + 30.0f, aspenZ = demoTreeZ + 40.0f;
    glm::vec3 aspenPos(aspenX, config_.getTerrainHeight(aspenX, aspenZ), aspenZ);
    treeSystem.addTree(aspenPos, 1.5f, 1.0f, loadPresetOrDefault("aspen_large.json", TreeOptions::defaultOak));

    SDL_Log("VegetationContentGenerator: Added 4 demo trees");
}

int VegetationContentGenerator::generateForest(
    TreeSystem& treeSystem,
    const glm::vec2& center,
    float radius,
    int maxTrees,
    uint32_t seed
) {
    // Tree presets for forest
    std::vector<std::pair<std::string, TreeOptions(*)()>> treePresets = {
        {"oak_medium.json", TreeOptions::defaultOak},
        {"pine_medium.json", TreeOptions::defaultPine},
        {"ash_medium.json", TreeOptions::defaultOak},
        {"aspen_medium.json", TreeOptions::defaultOak}
    };

    // Poisson disk sampling parameters
    const float minDist = 8.0f;
    const int maxAttempts = 30;

    // Simple LCG for deterministic results
    auto nextRand = [&seed]() -> float {
        seed = seed * 1103515245 + 12345;
        return static_cast<float>((seed >> 16) & 0x7FFF) / 32767.0f;
    };

    // Poisson disk sampling
    std::vector<glm::vec2> placedTrees;
    placedTrees.reserve(maxTrees);
    placedTrees.push_back(center);

    std::vector<size_t> activeList;
    activeList.push_back(0);

    while (!activeList.empty() && static_cast<int>(placedTrees.size()) < maxTrees) {
        size_t activeIdx = static_cast<size_t>(nextRand() * activeList.size());
        if (activeIdx >= activeList.size()) activeIdx = activeList.size() - 1;
        glm::vec2 activePoint = placedTrees[activeList[activeIdx]];

        bool foundValid = false;
        for (int attempt = 0; attempt < maxAttempts; attempt++) {
            float angle = nextRand() * 2.0f * 3.14159265f;
            float dist = minDist + nextRand() * minDist;
            glm::vec2 newPoint = activePoint + glm::vec2(std::cos(angle), std::sin(angle)) * dist;

            float distFromCenter = glm::length(newPoint - center);
            if (distFromCenter > radius) continue;

            bool tooClose = false;
            for (const auto& p : placedTrees) {
                if (glm::length(newPoint - p) < minDist) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) {
                placedTrees.push_back(newPoint);
                activeList.push_back(placedTrees.size() - 1);
                foundValid = true;
                break;
            }
        }

        if (!foundValid) {
            activeList.erase(activeList.begin() + activeIdx);
        }
    }

    // Use threaded generation for large forests
    auto threadedGen = ThreadedTreeGenerator::create(4);
    int treesPlaced = 0;

    if (threadedGen) {
        std::vector<ThreadedTreeGenerator::TreeRequest> requests;
        requests.reserve(placedTrees.size());

        for (size_t i = 0; i < placedTrees.size() && treesPlaced < maxTrees; i++) {
            float x = placedTrees[i].x;
            float z = placedTrees[i].y;
            float y = config_.getTerrainHeight(x, z);

            float rotation = nextRand() * 2.0f * 3.14159265f;
            float scale = 0.7f + 0.6f * nextRand();

            size_t presetIdx = static_cast<size_t>(nextRand() * treePresets.size());
            if (presetIdx >= treePresets.size()) presetIdx = treePresets.size() - 1;
            auto opts = loadPresetOrDefault(treePresets[presetIdx].first, treePresets[presetIdx].second);

            // Determine archetype index
            uint32_t archetypeIndex = 0;
            const std::string& leafType = opts.leaves.type;
            if (leafType == "oak") archetypeIndex = 0;
            else if (leafType == "pine") archetypeIndex = 1;
            else if (leafType == "ash") archetypeIndex = 2;
            else if (leafType == "aspen") archetypeIndex = 3;

            ThreadedTreeGenerator::TreeRequest req;
            req.position = glm::vec3(x, y, z);
            req.rotation = rotation;
            req.scale = scale;
            req.options = opts;
            req.archetypeIndex = archetypeIndex;
            requests.push_back(req);
            treesPlaced++;
        }

        threadedGen->queueTrees(requests);
        SDL_Log("VegetationContentGenerator: Queued %d trees for parallel generation", treesPlaced);

        threadedGen->waitForAll();

        auto stagedTrees = threadedGen->getCompletedTrees();
        int uploadedCount = 0;
        for (auto& staged : stagedTrees) {
            uint32_t treeIdx = treeSystem.addTreeFromStagedData(
                staged.position, staged.rotation, staged.scale,
                staged.options,
                staged.branchVertexData, staged.branchVertexCount,
                staged.branchIndices,
                staged.leafInstanceData, staged.leafInstanceCount,
                staged.archetypeIndex);

            if (treeIdx != UINT32_MAX) {
                uploadedCount++;
            }
        }

        treeSystem.finalizeLeafInstanceBuffer();
        SDL_Log("VegetationContentGenerator: Uploaded %d/%zu trees to GPU", uploadedCount, stagedTrees.size());
    } else {
        // Fallback to serial generation
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Threaded tree generator unavailable, using serial");
        for (size_t i = 0; i < placedTrees.size() && treesPlaced < maxTrees; i++) {
            float x = placedTrees[i].x;
            float z = placedTrees[i].y;
            float y = config_.getTerrainHeight(x, z);

            float rotation = nextRand() * 2.0f * 3.14159265f;
            float scale = 0.7f + 0.6f * nextRand();

            size_t presetIdx = static_cast<size_t>(nextRand() * treePresets.size());
            if (presetIdx >= treePresets.size()) presetIdx = treePresets.size() - 1;
            auto opts = loadPresetOrDefault(treePresets[presetIdx].first, treePresets[presetIdx].second);

            treeSystem.addTree(glm::vec3(x, y, z), rotation, scale, opts);
            treesPlaced++;
        }
    }

    SDL_Log("VegetationContentGenerator: Generated forest with %d trees", treesPlaced);
    return treesPlaced;
}

std::vector<ThreadedTreeGenerator::TreeRequest> VegetationContentGenerator::buildBiomeForestRequests(
    const BiomeMap& biomeMap,
    const std::vector<Settlement>* settlements,
    const glm::vec2& center,
    float radius,
    int maxTrees,
    float seaLevel,
    uint32_t seed
) {
    // Tree presets per archetype index (matches generateImpostorArchetypes order)
    std::vector<std::pair<std::string, TreeOptions(*)()>> treePresets = {
        {"oak_medium.json", TreeOptions::defaultOak},
        {"pine_medium.json", TreeOptions::defaultPine},
        {"ash_medium.json", TreeOptions::defaultOak},
        {"aspen_medium.json", TreeOptions::defaultOak}
    };

    // Per-biome tree probability on the sampling grid
    auto densityFor = [](BiomeMap::Zone zone) -> float {
        switch (zone) {
            case BiomeMap::Zone::Woodland:     return 0.55f;
            case BiomeMap::Zone::Grassland:    return 0.05f;
            case BiomeMap::Zone::Agricultural: return 0.03f;
            case BiomeMap::Zone::Wetland:      return 0.10f;
            default:                           return 0.0f;  // Sea/beach/cliff/marsh/river
        }
    };

    // Archetype mix per biome (indices into treePresets)
    auto pickArchetype = [](BiomeMap::Zone zone, float t) -> size_t {
        switch (zone) {
            case BiomeMap::Zone::Woodland:
                // Mixed woodland: oak/ash dominant, some pine and aspen
                if (t < 0.40f) return 0;       // oak
                if (t < 0.70f) return 2;       // ash
                if (t < 0.88f) return 1;       // pine
                return 3;                      // aspen
            case BiomeMap::Zone::Wetland:
                return t < 0.7f ? 3 : 2;       // aspen/ash near water
            case BiomeMap::Zone::Agricultural:
                return 0;                      // lone field oaks
            case BiomeMap::Zone::Grassland:
            default:
                return t < 0.8f ? 0 : 2;       // downs: oak with some ash
        }
    };

    const float cellSize = 12.0f;  // Sampling grid spacing in meters

    // Pass 1: collect every candidate position over the whole region so the
    // tree budget is spread evenly instead of truncating the grid scan partway
    struct Candidate {
        glm::vec2 pos;
        float y;
        size_t presetIdx;
    };
    std::vector<Candidate> candidates;

    int gridCells = static_cast<int>((2.0f * radius) / cellSize);
    glm::vec2 regionMin = center - glm::vec2(radius);

    for (int gz = 0; gz < gridCells; ++gz) {
        for (int gx = 0; gx < gridCells; ++gx) {
            glm::vec2 cellOrigin = regionMin + glm::vec2(gx, gz) * cellSize;

            // Jittered position within the cell, deterministic per cell
            float jx = DeterministicRandom::hashPosition(cellOrigin.x, cellOrigin.y, seed);
            float jz = DeterministicRandom::hashPosition(cellOrigin.x, cellOrigin.y, seed + 1);
            glm::vec2 pos = cellOrigin + glm::vec2(jx, jz) * cellSize;

            if (glm::distance(pos, center) > radius) continue;

            BiomeMap::Zone zone = biomeMap.zoneAtWorld(pos.x, pos.y);
            float density = densityFor(zone);
            if (density <= 0.0f) continue;

            float roll = DeterministicRandom::hashPosition(pos.x, pos.y, seed + 2);
            if (roll >= density) continue;

            // No trees inside settlement radii (buildings live there)
            if (settlements) {
                bool inSettlement = false;
                for (const auto& s : *settlements) {
                    if (glm::distance(pos, s.worldPos) < s.radius) { inSettlement = true; break; }
                }
                if (inSettlement) continue;
            }

            float y = config_.getTerrainHeight(pos.x, pos.y);
            if (y < seaLevel + 0.5f) continue;

            float archRoll = DeterministicRandom::hashPosition(pos.x, pos.y, seed + 3);
            candidates.push_back({pos, y, pickArchetype(zone, archRoll)});
        }
    }

    // Pass 2: if over budget, keep an evenly strided subset (deterministic,
    // preserves spatial coverage since candidates are in grid order)
    size_t keep = std::min(candidates.size(), static_cast<size_t>(maxTrees));
    if (keep < candidates.size()) {
        SDL_Log("VegetationContentGenerator: %zu tree candidates, thinning to %zu",
                candidates.size(), keep);
    }

    // Presets loaded once, not per tree
    std::vector<TreeOptions> presetCache;
    presetCache.reserve(treePresets.size());
    for (const auto& [name, fallback] : treePresets) {
        presetCache.push_back(loadPresetOrDefault(name, fallback));
    }

    std::vector<ThreadedTreeGenerator::TreeRequest> requests;
    requests.reserve(keep);
    for (size_t i = 0; i < keep; ++i) {
        const Candidate& c = candidates[keep == candidates.size()
            ? i : (i * candidates.size()) / keep];
        ThreadedTreeGenerator::TreeRequest req;
        req.position = glm::vec3(c.pos.x, c.y, c.pos.y);
        req.rotation = DeterministicRandom::hashRange(c.pos.x, c.pos.y, seed + 4, 0.0f, 6.2831853f);
        req.scale = DeterministicRandom::hashRange(c.pos.x, c.pos.y, seed + 5, 0.7f, 1.3f);
        req.options = presetCache[c.presetIdx];
        req.archetypeIndex = static_cast<uint32_t>(c.presetIdx);
        requests.push_back(req);
    }

    SDL_Log("VegetationContentGenerator: Biome forest - %zu tree requests built (region radius %.0fm)",
            requests.size(), radius);
    return requests;
}

void VegetationContentGenerator::generateImpostorArchetypes(
    TreeSystem& treeSystem,
    TreeLODSystem& treeLOD
) {
    struct ArchetypeInfo {
        uint32_t meshIndex;
        std::string name;
        std::string bark;
        std::string leaves;
    };

    std::vector<ArchetypeInfo> archetypeInfos = {
        {0, "oak",   "oak",   "oak"},
        {1, "pine",  "pine",  "pine"},
        {2, "ash",   "oak",   "ash"},
        {3, "aspen", "birch", "aspen"}
    };

    for (const auto& info : archetypeInfos) {
        if (info.meshIndex >= treeSystem.getMeshCount()) continue;

        const auto& branchMesh = treeSystem.getBranchMesh(info.meshIndex);
        const auto& leafInstances = treeSystem.getLeafInstances(info.meshIndex);
        const auto& treeOpts = treeSystem.getTreeOptions(info.meshIndex);

        auto* barkTex = treeSystem.getBarkTexture(info.bark);
        auto* barkNorm = treeSystem.getBarkNormalMap(info.bark);
        auto* leafTex = treeSystem.getLeafTexture(info.leaves);

        if (barkTex && barkNorm && leafTex) {
            int32_t archetypeIdx = treeLOD.generateImpostor(
                info.name,
                treeOpts,
                branchMesh,
                leafInstances,
                barkTex->getImageView(),
                barkNorm->getImageView(),
                leafTex->getImageView(),
                barkTex->getSampler()
            );
            if (archetypeIdx >= 0) {
                SDL_Log("VegetationContentGenerator: Generated impostor archetype %d: %s", archetypeIdx, info.name.c_str());
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Failed to generate %s impostor", info.name.c_str());
            }
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Missing textures for %s impostor", info.name.c_str());
        }
    }
}

void VegetationContentGenerator::finalizeTreeSystems(
    TreeSystem& treeSystem,
    TreeLODSystem* treeLOD,
    ImpostorCullSystem* impostorCull,
    TreeRenderer* treeRenderer,
    const std::vector<vk::Buffer>& uniformBuffers,
    vk::ImageView shadowView,
    vk::Sampler shadowSampler
) {
    // Update ImpostorCullSystem with tree data
    if (impostorCull && treeLOD) {
        impostorCull->updateTreeData(treeSystem, treeLOD->getImpostorAtlas());
        impostorCull->updateArchetypeData(treeLOD->getImpostorAtlas());
        impostorCull->initializeDescriptorSets();
        SDL_Log("VegetationContentGenerator: ImpostorCullSystem updated with %u trees", impostorCull->getTreeCount());
    }

    // Update TreeRenderer spatial index and branch culling data
    if (treeRenderer) {
        treeRenderer->updateSpatialIndex(treeSystem);
        treeRenderer->updateBranchCullingData(treeSystem, treeLOD);
    }

    // Initialize TreeLODSystem descriptor sets
    if (treeLOD) {
        treeLOD->initializeDescriptorSets(uniformBuffers, shadowView, shadowSampler);

        if (impostorCull) {
            treeLOD->initializeGPUCulledDescriptors(impostorCull->getVisibleImpostorBuffer());
        }
    }
}

std::unique_ptr<ScatterSystem> VegetationContentGenerator::createDetritusSystem(
    const DetritusCreateInfo& info,
    const TreeSystem& treeSystem
) {
    ScatterSystem::InitInfo scatterInfo{};
    scatterInfo.device = info.device;
    scatterInfo.allocator = info.allocator;
    scatterInfo.commandPool = info.commandPool;
    scatterInfo.graphicsQueue = info.graphicsQueue;
    scatterInfo.physicalDevice = info.physicalDevice;
    scatterInfo.resourcePath = config_.resourcePath;
    scatterInfo.terrainSize = config_.terrainSize;
    scatterInfo.getTerrainHeight = config_.getTerrainHeight;

    // Gather tree positions for scattering detritus nearby
    std::vector<glm::vec3> treePositions = getTreePositionsForDetritus(treeSystem);

    ScatterSystemFactory::DetritusConfig detritusConfig{};
    detritusConfig.branchVariations = 8;
    detritusConfig.branchesPerVariation = 4;
    detritusConfig.minLength = 0.5f;
    detritusConfig.maxLength = 2.5f;
    detritusConfig.minRadius = 0.03f;
    detritusConfig.maxRadius = 0.12f;
    detritusConfig.placementRadius = 8.0f;
    detritusConfig.minDistanceBetween = 1.5f;
    detritusConfig.materialRoughness = 0.85f;
    detritusConfig.materialMetallic = 0.0f;

    auto detritusSystem = ScatterSystemFactory::createDetritus(scatterInfo, detritusConfig, treePositions);
    if (detritusSystem) {
        SDL_Log("VegetationContentGenerator: Created detritus with %zu branches near %zu trees",
                detritusSystem->getInstanceCount(), treePositions.size());
    }
    return detritusSystem;
}

ScatterSystemFactory::DetritusConfig VegetationContentGenerator::getDetritusConfig() const {
    ScatterSystemFactory::DetritusConfig config{};
    config.branchVariations = 8;
    config.branchesPerVariation = 4;
    config.minLength = 0.5f;
    config.maxLength = 2.5f;
    return config;
}

std::vector<glm::vec3> VegetationContentGenerator::getTreePositionsForDetritus(
    const TreeSystem& treeSystem
) const {
    const auto& treeInstances = treeSystem.getTreeInstances();
    std::vector<glm::vec3> positions;
    positions.reserve(treeInstances.size());
    for (const auto& tree : treeInstances) {
        positions.push_back(tree.position());
    }
    return positions;
}
