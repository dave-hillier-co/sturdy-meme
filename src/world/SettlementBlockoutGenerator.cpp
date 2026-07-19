#include "SettlementBlockoutGenerator.h"
#include "WorldCoords.h"
#include "FootprintRing.h"
#include "KitBuildingAssembler.h"

#include "ecs/EntityFactory.h"
#include "scene/DeterministicRandom.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <fstream>
#include <memory>

namespace {

// Building counts per settlement type; unknown types get the hamlet count
int buildingCountForType(const std::string& type) {
    if (type == "Town") return 28;
    if (type == "Village") return 16;
    if (type == "Fishing Village") return 12;
    return 7;  // Hamlet
}

// Combine a value into a running FNV-1a style hash
void hashCombine(uint32_t& hash, uint32_t value) {
    hash ^= value;
    hash *= 16777619u;
}

uint32_t quantize(float v) {
    return static_cast<uint32_t>(static_cast<int32_t>(std::lround(v * 100.0f)));
}

// Oriented box fitted to a footprint polygon: orientation from its longest
// edge, extents from projecting all vertices onto that frame.
struct FootprintBox {
    glm::vec2 center{0.0f};  // World XZ
    float width = 0.0f;
    float depth = 0.0f;
    float yaw = 0.0f;
};

FootprintBox fitFootprintBox(const std::vector<glm::vec2>& worldRing) {
    FootprintBox box;
    if (worldRing.size() < 3) return box;

    glm::vec2 bestDir(1.0f, 0.0f);
    float bestLen2 = 0.0f;
    for (size_t i = 0; i + 1 < worldRing.size(); ++i) {
        glm::vec2 edge = worldRing[i + 1] - worldRing[i];
        float len2 = glm::dot(edge, edge);
        if (len2 > bestLen2) {
            bestLen2 = len2;
            bestDir = edge;
        }
    }
    if (bestLen2 <= 0.0f) return box;
    bestDir = glm::normalize(bestDir);
    glm::vec2 perp(-bestDir.y, bestDir.x);

    float minU = 1e9f, maxU = -1e9f, minV = 1e9f, maxV = -1e9f;
    for (const auto& p : worldRing) {
        float u = glm::dot(p, bestDir);
        float v = glm::dot(p, perp);
        minU = std::min(minU, u); maxU = std::max(maxU, u);
        minV = std::min(minV, v); maxV = std::max(maxV, v);
    }

    float midU = (minU + maxU) * 0.5f;
    float midV = (minV + maxV) * 0.5f;
    box.center = bestDir * midU + perp * midV;
    box.width = maxU - minU;
    box.depth = maxV - minV;
    // atan2 of the direction gives rotation of local +X onto bestDir
    box.yaw = std::atan2(-bestDir.y, bestDir.x);
    return box;
}

// One merged mesh buffer per building-layer material.
struct LayerBuf {
    std::vector<Vertex> verts;
    std::vector<uint32_t> inds;
};

// Extrude a footprint into a below-ground foundation plinth plus stacked floor
// bands, appending each band to its layer buffer (layers[0] = foundation,
// layers[1..] = floors cycled per storey). The full above-ground prism is also
// appended to the collider buffers so the physics mesh matches the plot shape.
// groundY is the (already slightly sunk) lowest ground corner.
void appendLayeredBuilding(std::vector<LayerBuf>& layers,
                           std::vector<Vertex>& colVerts, std::vector<uint32_t>& colInds,
                           std::vector<glm::vec2> ring,
                           float groundY, float height,
                           float foundationDepth, float storeyHeight) {
    if (layers.empty() || !footprint::normalizeRing(ring)) return;

    const float topY = groundY + height;
    // Foundation plinth: sunk below the lowest corner, capped with a small lip
    // above ground so it stays visible where the terrain falls away.
    const float lip = std::min(0.3f, height * 0.1f);
    const float foundTop = groundY + lip;
    // Foundation material is the rock trim sheet: V pins to its brick-course
    // strip, only U tiles along the wall.
    footprint::appendBandStrip(layers[0].verts, layers[0].inds, ring,
                               groundY - foundationDepth, foundTop,
                               /*vBottom*/ 0.98f, /*vTop*/ 0.60f);

    // Stacked floor bands between the plinth lip and the eaves
    const int floors = std::max(1,
        static_cast<int>(std::lround((topY - foundTop) / storeyHeight)));
    const size_t floorMats = layers.size() > 1 ? layers.size() - 1 : 1;
    float y = foundTop;
    for (int f = 0; f < floors; ++f) {
        float bandTop = (f == floors - 1)
            ? topY
            : foundTop + (topY - foundTop) * static_cast<float>(f + 1) / floors;
        size_t li = layers.size() > 1 ? 1 + (static_cast<size_t>(f) % floorMats) : 0;
        footprint::appendBandWalls(layers[li].verts, layers[li].inds, ring, y, bandTop);
        if (f == floors - 1) {
            footprint::appendRoofCap(layers[li].verts, layers[li].inds, ring, bandTop, 0.5f);
        }
        y = bandTop;
    }

    // Collider: full above-ground prism, capped so the roof is walkable and
    // nothing falls into the hollow interior
    footprint::appendBandWalls(colVerts, colInds, ring, groundY, topY);
    footprint::appendRoofCap(colVerts, colInds, ring, topY, 0.5f);
}

// Load building footprints (world-space rings) from town_<id>.geojson.
// Returns pairs of (ring, special flag). Coordinates in the file are content
// space; converted to world space here.
std::vector<std::pair<std::vector<glm::vec2>, bool>> loadTownFootprints(const std::string& path) {
    std::vector<std::pair<std::vector<glm::vec2>, bool>> footprints;

    std::ifstream file(path);
    if (!file.is_open()) return footprints;

    try {
        nlohmann::json j;
        file >> j;
        for (const auto& feature : j.value("features", nlohmann::json::array())) {
            const auto& props = feature["properties"];
            if (props.value("kind", "") != "building") continue;
            if (feature["geometry"]["type"] != "Polygon") continue;

            const auto& rings = feature["geometry"]["coordinates"];
            if (rings.empty()) continue;

            std::vector<glm::vec2> ring;
            for (const auto& coord : rings[0]) {
                glm::vec2 content(coord[0].get<float>(), coord[1].get<float>());
                ring.push_back(WorldCoords::contentToWorld(content));
            }
            if (ring.size() >= 3) {
                footprints.emplace_back(std::move(ring), props.value("special", false));
            }
        }
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SettlementBlockout: Failed to parse %s: %s", path.c_str(), e.what());
        footprints.clear();
    }

    return footprints;
}

// Fallback plots from the street generator's lots.geojson (settlements with
// no town layout). Lot polygons are full burgage plots; the building
// occupies a shrunk core so gardens/yards remain around it.
std::vector<std::pair<std::vector<glm::vec2>, bool>> loadLotFootprints(
    const std::string& streetsDir, uint32_t settlementId) {
    std::vector<std::pair<std::vector<glm::vec2>, bool>> footprints;
    if (streetsDir.empty()) return footprints;

    std::string path = streetsDir + "/settlement_" + std::to_string(settlementId) +
                       "/lots.geojson";
    std::ifstream file(path);
    if (!file.is_open()) return footprints;

    try {
        nlohmann::json j;
        file >> j;
        for (const auto& feature : j.value("features", nlohmann::json::array())) {
            if (feature["geometry"]["type"] != "Polygon") continue;
            const auto& rings = feature["geometry"]["coordinates"];
            if (rings.empty()) continue;

            std::vector<glm::vec2> ring;
            glm::vec2 centroid(0.0f);
            for (const auto& coord : rings[0]) {
                glm::vec2 content(coord[0].get<float>(), coord[1].get<float>());
                ring.push_back(WorldCoords::contentToWorld(content));
                centroid += ring.back();
            }
            if (ring.size() < 3) continue;
            centroid /= static_cast<float>(ring.size());
            for (auto& p : ring) {
                p = centroid + (p - centroid) * 0.65f;
            }
            footprints.emplace_back(std::move(ring), false);
        }
    } catch (const std::exception& e) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SettlementBlockout: Failed to parse %s: %s", path.c_str(), e.what());
        footprints.clear();
    }

    return footprints;
}

} // namespace

SettlementBlockoutGenerator::SettlementBlockoutGenerator(Config config)
    : config_(std::move(config)) {
    result_.layoutHash = 2166136261u;  // FNV offset basis

    // One assembler for all settlements: kit glTF pieces load once and are
    // reused; accumulated meshes are flushed per spatial chunk.
    if (!config_.kitModelsDir.empty() && config_.createMeshes && config_.kitMaterial) {
        kit_ = std::make_unique<KitBuildingAssembler>(config_.kitModelsDir);
        // Probe an essential piece: if the kit assets are missing/broken,
        // fall back to prisms everywhere instead of emitting invisible
        // buildings with solid colliders.
        if (kit_->probe("Wall_Plaster_Straight").empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "SettlementBlockoutGenerator: kit pieces not loadable from "
                         "'%s', using prism buildings", config_.kitModelsDir.c_str());
            kit_.reset();
        }
    }
}

SettlementBlockoutGenerator::~SettlementBlockoutGenerator() = default;

void SettlementBlockoutGenerator::logSummary() const {
    SDL_Log("SettlementBlockout: %d buildings in %zu scene entities, %zu slots "
            "rejected, %zu kit verts total, layout hash %08x",
            result_.totalBuildings, result_.entities.size(), result_.rejectedSlots,
            totalKitVerts_, result_.layoutHash);
}

std::vector<ecs::Entity> SettlementBlockoutGenerator::generateSettlement(
    ecs::World& world, const Settlement& settlement) {

    const Config& config = config_;
    Result& result = result_;
    KitBuildingAssembler* kit = kit_.get();
    size_t& totalKitVerts = totalKitVerts_;
    const size_t entitiesBefore = result_.entities.size();

    if (!config.buildingMesh || !config.getTerrainHeight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SettlementBlockoutGenerator: missing mesh or height query");
        return {};
    }

    ecs::EntityFactory factory(world);

    // Meshes accumulate here and upload in ONE batch (single staging buffer
    // and queue submit) at the end of the settlement.
    std::vector<MeshGeometry> pendingGeo;
    std::vector<MaterialId> pendingMats;

    {
        // Move the assembler's accumulated per-material meshes into the
        // pending batch (world-space geometry, identity transform). Called
        // per spatial chunk so each mesh keeps a chunk-local AABB.
        auto flushKit = [&]() {
            if (!kit) return;
            for (auto& mm : kit->takeMaterialMeshes()) {
                if (mm.vertices.empty()) continue;
                pendingMats.push_back(config.kitMaterial(mm.materialName));
                pendingGeo.push_back({std::move(mm.vertices), std::move(mm.indices)});
            }
        };
        // Upload the pending batch and create one entity per merged mesh.
        auto uploadPending = [&]() {
            if (pendingGeo.empty()) return;
            std::vector<Mesh*> meshes = config.createMeshes(std::move(pendingGeo));
            for (size_t i = 0; i < meshes.size(); ++i) {
                if (meshes[i]) {
                    result.entities.push_back(factory.createBuilding(
                        meshes[i], pendingMats[i], glm::mat4(1.0f), settlement.id));
                } else {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                 "SettlementBlockout: mesh upload failed for %s",
                                 settlement.displayName().c_str());
                }
            }
            pendingGeo.clear();
            pendingMats.clear();
        };

        if (config.preloadTiles) {
            config.preloadTiles(settlement.worldPos.x, settlement.worldPos.y,
                                settlement.radius + 50.0f);
        }

        // Footprint-driven placement when a town layout exists
        if (!config.townsDir.empty()) {
            std::string townPath = config.townsDir + "/town_" +
                                   std::to_string(settlement.id) + ".geojson";
            auto footprints = loadTownFootprints(townPath);
            if (footprints.empty()) {
                // No town layout: fall back to the street generator's burgage
                // lots so the settlement still gets plot-aligned buildings.
                footprints = loadLotFootprints(config.streetsDir, settlement.id);
            }
            if (!footprints.empty()) {
                const uint32_t seed = settlement.id * 7919u + 1u;
                int created = 0;
                size_t steepSlots = 0;

                // Fit boxes up front and drop sub-minimum footprints, THEN pick
                // which to build. The file is ordered ward-by-ward, so taking
                // the first N would cluster buildings in a corner of town: keep
                // every special building (church, castle) and stride evenly
                // through the rest so the budget covers the whole layout.
                struct Slot { size_t footprintIndex; FootprintBox box; bool special; };
                std::vector<Slot> eligible;
                eligible.reserve(footprints.size());
                for (size_t fi = 0; fi < footprints.size(); ++fi) {
                    FootprintBox box = fitFootprintBox(footprints[fi].first);
                    if (std::min(box.width, box.depth) < config.minFootprintSize) continue;
                    eligible.push_back({fi, box, footprints[fi].second});
                }

                std::vector<size_t> selected;
                const size_t cap = static_cast<size_t>(config.maxBuildingsPerSettlement);
                if (eligible.size() <= cap) {
                    for (size_t i = 0; i < eligible.size(); ++i) selected.push_back(i);
                } else {
                    std::vector<size_t> ordinary;
                    for (size_t i = 0; i < eligible.size(); ++i) {
                        if (eligible[i].special) selected.push_back(i);
                        else ordinary.push_back(i);
                    }
                    size_t remaining = cap > selected.size() ? cap - selected.size() : 0;
                    remaining = std::min(remaining, ordinary.size());
                    for (size_t i = 0; i < remaining; ++i) {
                        selected.push_back(ordinary[(i * ordinary.size()) / remaining]);
                    }
                    std::sort(selected.begin(), selected.end());
                }

                // Merged mode: extrude the actual footprint polygons into one
                // world-space mesh per layer material (foundation + floor bands)
                // for the whole settlement, so each storey reads distinctly while
                // draw calls stay ~O(layers) rather than O(buildings).
                std::vector<MaterialId> layerMats = config.layerMaterials;
                if (layerMats.empty()) layerMats.push_back(config.materialId);
                std::vector<LayerBuf> layers(layerMats.size());
                std::vector<Vertex> colVerts;
                std::vector<uint32_t> colInds;
                const bool mergedMode = static_cast<bool>(config.createMeshes);

                // Kit mode: order slots by spatial chunk so each flush yields
                // meshes with chunk-local AABBs (GPU/shadow culling can reject
                // most of a town). Chunks run centre-outward so when the kit
                // vertex budget runs dry the prism fallbacks land on the town
                // outskirts rather than splitting it along a chunk line.
                // Order stays deterministic.
                std::vector<size_t> order = selected;
                auto cellOf = [&](size_t idx) {
                    const glm::vec2& c = eligible[idx].box.center;
                    return glm::ivec2(
                        static_cast<int>(std::floor(c.x / config.kitChunkSize)),
                        static_cast<int>(std::floor(c.y / config.kitChunkSize)));
                };
                if (kit) {
                    auto cellDist2 = [&](const glm::ivec2& cell) {
                        glm::vec2 centre = (glm::vec2(cell) + 0.5f) * config.kitChunkSize;
                        glm::vec2 d = centre - settlement.worldPos;
                        return glm::dot(d, d);
                    };
                    std::stable_sort(order.begin(), order.end(),
                                     [&](size_t a, size_t b) {
                        glm::ivec2 ca = cellOf(a), cb = cellOf(b);
                        if (ca == cb) return a < b;
                        float da = cellDist2(ca), db = cellDist2(cb);
                        if (da != db) return da < db;
                        if (ca.x != cb.x) return ca.x < cb.x;
                        return ca.y < cb.y;
                    });
                }
                glm::ivec2 currentCell{INT_MIN, INT_MIN};
                size_t settlementKitVerts = 0;
                int kitBuildings = 0;

                for (size_t slotIdx : order) {
                    const FootprintBox& box = eligible[slotIdx].box;
                    const bool special = eligible[slotIdx].special;
                    if (created >= config.maxBuildingsPerSettlement) break;

                    if (config.exclusionRadius > 0.0f &&
                        glm::distance(box.center, config.exclusionCenter) < config.exclusionRadius) {
                        result.rejectedSlots++;
                        continue;
                    }

                    float halfDiag = 0.5f * std::max(box.width, box.depth);
                    float h0 = config.getTerrainHeight(box.center.x - halfDiag, box.center.y - halfDiag);
                    float h1 = config.getTerrainHeight(box.center.x + halfDiag, box.center.y - halfDiag);
                    float h2 = config.getTerrainHeight(box.center.x - halfDiag, box.center.y + halfDiag);
                    float h3 = config.getTerrainHeight(box.center.x + halfDiag, box.center.y + halfDiag);
                    float minH = std::min(std::min(h0, h1), std::min(h2, h3));
                    float maxH = std::max(std::max(h0, h1), std::max(h2, h3));

                    if (minH < config.seaLevel + 0.5f) {
                        result.rejectedSlots++;
                        continue;
                    }
                    // Layouts keep steep footprints (terracing is future work); log them
                    if (maxH - minH > config.maxCornerHeightSpread) {
                        steepSlots++;
                    }

                    // Eaves heights: 1.5-2 storey cottages, taller specials
                    // (churches, castles)
                    float height = special
                        ? DeterministicRandom::hashRange(box.center.x, box.center.y, seed + 6, 10.0f, 16.0f)
                        : DeterministicRandom::hashRange(box.center.x, box.center.y, seed + 4, 4.5f, 7.5f);

                    // Kit wall shell when the plot is gentle enough and the
                    // settlement's kit vertex budget has room; anything else
                    // falls back to the extruded prism so every plot builds.
                    std::vector<glm::vec2> ring;
                    bool kitBuilt = false;
                    if (kit && (maxH - minH) <= config.maxCornerHeightSpread &&
                        settlementKitVerts < config.kitVertexBudgetPerSettlement &&
                        totalKitVerts < config.kitVertexBudgetTotal) {
                        ring = footprints[eligible[slotIdx].footprintIndex].first;
                        kitBuilt = footprint::normalizeRing(ring);
                    }

                    if (kitBuilt) {
                        glm::ivec2 cell = cellOf(slotIdx);
                        if (cell != currentCell) {
                            flushKit();
                            currentCell = cell;
                        }
                        const uint32_t buildingSeed = static_cast<uint32_t>(
                            DeterministicRandom::hashRange(box.center.x, box.center.y,
                                                           seed + 7, 0.0f, 16777216.0f));
                        const float baseY = minH - 0.2f;
                        const size_t before = kit->vertexCount();
                        kit->addFootprintShell(
                            ring,
                            KitBuildingAssembler::OrientedBox{box.center, box.width,
                                                              box.depth, box.yaw},
                            baseY, height, minH - config.foundationDepth, buildingSeed);
                        settlementKitVerts += kit->vertexCount() - before;
                        totalKitVerts += kit->vertexCount() - before;
                        kitBuildings++;
                        // Collider prism matches the module-quantized shell
                        // top, capped so nothing falls into the hollow shell
                        const float colTop =
                            baseY + KitBuildingAssembler::shellTopHeight(height);
                        footprint::appendBandWalls(colVerts, colInds, ring, baseY, colTop);
                        footprint::appendRoofCap(colVerts, colInds, ring, colTop, 0.5f);
                    } else if (mergedMode) {
                        // Base sinks 0.2m below the lowest corner so no wall floats.
                        // The mesh collider is built from the accumulated prism
                        // geometry after the loop (matches concave footprints).
                        appendLayeredBuilding(layers, colVerts, colInds,
                                              footprints[eligible[slotIdx].footprintIndex].first,
                                              minH - 0.2f, height,
                                              config.foundationDepth, config.storeyHeight);
                    } else {
                        if (config.addCollider) {
                            config.addCollider(
                                glm::vec3(box.center.x, minH - 0.2f + height * 0.5f, box.center.y),
                                glm::vec3(box.width * 0.5f, height * 0.5f, box.depth * 0.5f),
                                box.yaw);
                        }
                        glm::mat4 transform = glm::translate(glm::mat4(1.0f),
                            glm::vec3(box.center.x, minH + height * 0.5f - 0.2f, box.center.y));
                        transform = glm::rotate(transform, box.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                        transform = glm::scale(transform, glm::vec3(box.width, height, box.depth));

                        ecs::Entity entity = factory.createBuilding(
                            config.buildingMesh, config.materialId, transform, settlement.id);
                        result.entities.push_back(entity);
                    }
                    created++;

                    // Per-building hashes combine with XOR so the layout hash
                    // is independent of emission order (which varies with kit
                    // chunking) — it fingerprints placements only.
                    uint32_t bh = 2166136261u;
                    hashCombine(bh, settlement.id);
                    hashCombine(bh, quantize(box.center.x));
                    hashCombine(bh, quantize(box.center.y));
                    hashCombine(bh, quantize(minH));
                    hashCombine(bh, quantize(box.width * 1000.0f + height * 10.0f + box.depth));
                    result.layoutHash ^= bh;
                }

                flushKit();  // Last chunk of kit-wall meshes

                if (mergedMode && !colVerts.empty()) {
                    if (config.addMeshCollider) {
                        config.addMeshCollider(colVerts, colInds);
                    }
                    for (size_t li = 0; li < layers.size(); ++li) {
                        if (layers[li].verts.empty()) continue;
                        pendingMats.push_back(layerMats[li]);
                        pendingGeo.push_back({std::move(layers[li].verts),
                                              std::move(layers[li].inds)});
                    }
                }
                uploadPending();

                SDL_Log("SettlementBlockout: %s - %d buildings from town layout "
                        "(%zu footprints, %zu eligible, %zu steep, %d kit shells, "
                        "%zu kit verts)",
                        settlement.displayName().c_str(), created,
                        footprints.size(), eligible.size(), steepSlots,
                        kitBuildings, settlementKitVerts);
                result.totalBuildings += created;
                return {result.entities.begin() + entitiesBefore, result.entities.end()};
            }
        }

        const int targetCount = buildingCountForType(settlement.type);
        const float placementRadius = settlement.radius * 0.85f;
        const uint32_t seed = settlement.id * 7919u + 1u;

        std::vector<glm::vec2> placed;
        int created = 0;

        // Deterministic rejection sampling: fixed attempt sequence per settlement
        const int maxAttempts = targetCount * 8;
        for (int attempt = 0; attempt < maxAttempts && created < targetCount; ++attempt) {
            glm::vec2 offset = DeterministicRandom::hashDiskPoint(
                static_cast<float>(attempt), static_cast<float>(settlement.id),
                seed, placementRadius);
            glm::vec2 pos = settlement.worldPos + offset;

            // Building footprint and height, deterministic per slot
            float width = DeterministicRandom::hashRange(pos.x, pos.y, seed + 2, 4.0f, 8.0f);
            float depth = DeterministicRandom::hashRange(pos.x, pos.y, seed + 3, 4.0f, 8.0f);
            float height = DeterministicRandom::hashRange(pos.x, pos.y, seed + 4, 3.0f, 5.5f);
            float yaw = DeterministicRandom::hashRange(pos.x, pos.y, seed + 5, 0.0f, glm::two_pi<float>());

            // Keep clear of hand-placed content (spawn/well at Town 1)
            if (config.exclusionRadius > 0.0f &&
                glm::distance(pos, config.exclusionCenter) < config.exclusionRadius) {
                result.rejectedSlots++;
                continue;
            }

            // Min spacing against already placed buildings
            float minSpacing = (width + depth) * 0.5f + 2.0f;
            bool tooClose = false;
            for (const auto& other : placed) {
                if (glm::distance(pos, other) < minSpacing) { tooClose = true; break; }
            }
            if (tooClose) continue;

            // Sample the four footprint corners; reject steep or submerged slots
            float halfDiag = 0.5f * std::max(width, depth);
            float h0 = config.getTerrainHeight(pos.x - halfDiag, pos.y - halfDiag);
            float h1 = config.getTerrainHeight(pos.x + halfDiag, pos.y - halfDiag);
            float h2 = config.getTerrainHeight(pos.x - halfDiag, pos.y + halfDiag);
            float h3 = config.getTerrainHeight(pos.x + halfDiag, pos.y + halfDiag);
            float minH = std::min(std::min(h0, h1), std::min(h2, h3));
            float maxH = std::max(std::max(h0, h1), std::max(h2, h3));

            if (minH < config.seaLevel + 0.5f) {
                result.rejectedSlots++;
                continue;
            }
            if (maxH - minH > config.maxCornerHeightSpread) {
                result.rejectedSlots++;
                continue;
            }

            // Sink slightly into the lowest corner so no edge floats
            float groundY = minH;
            if (kit && totalKitVerts < config.kitVertexBudgetTotal) {
                // Kit wall shell on a module-quantized rectangle (2m bays).
                // Half extents in metres equal the module counts.
                const float hx = static_cast<float>(
                    std::max(2, static_cast<int>(std::lround(width * 0.5f))));
                const float hz = static_cast<float>(
                    std::max(2, static_cast<int>(std::lround(depth * 0.5f))));
                const float c = std::cos(yaw), s = std::sin(yaw);
                auto world = [&](float lx, float lz) {
                    return pos + glm::vec2(c * lx + s * lz, -s * lx + c * lz);
                };
                // CCW in math orientation, same convention as town rings
                std::vector<glm::vec2> ring{world(-hx, -hz), world(hx, -hz),
                                            world(hx, hz), world(-hx, hz)};
                const float baseY = groundY - 0.2f;
                const float shellTop = KitBuildingAssembler::shellTopHeight(height);
                const uint32_t buildingSeed = static_cast<uint32_t>(
                    DeterministicRandom::hashRange(pos.x, pos.y, seed + 7,
                                                   0.0f, 16777216.0f));
                const size_t before = kit->vertexCount();
                kit->addFootprintShell(
                    ring,
                    KitBuildingAssembler::OrientedBox{pos, hx * 2.0f, hz * 2.0f, yaw},
                    baseY, height, groundY - config.foundationDepth, buildingSeed);
                totalKitVerts += kit->vertexCount() - before;
                if (config.addCollider) {
                    config.addCollider(glm::vec3(pos.x, baseY + shellTop * 0.5f, pos.y),
                                       glm::vec3(hx, shellTop * 0.5f, hz), yaw);
                }
            } else {
                if (config.addCollider) {
                    config.addCollider(glm::vec3(pos.x, groundY - 0.2f + height * 0.5f, pos.y),
                                       glm::vec3(width * 0.5f, height * 0.5f, depth * 0.5f), yaw);
                }
                glm::mat4 transform = glm::translate(glm::mat4(1.0f),
                    glm::vec3(pos.x, groundY + height * 0.5f - 0.2f, pos.y));
                transform = glm::rotate(transform, yaw, glm::vec3(0.0f, 1.0f, 0.0f));
                transform = glm::scale(transform, glm::vec3(width, height, depth));

                ecs::Entity entity = factory.createBuilding(
                    config.buildingMesh, config.materialId, transform, settlement.id);
                result.entities.push_back(entity);
            }
            placed.push_back(pos);
            created++;

            hashCombine(result.layoutHash, settlement.id);
            hashCombine(result.layoutHash, quantize(pos.x));
            hashCombine(result.layoutHash, quantize(pos.y));
            hashCombine(result.layoutHash, quantize(groundY));
            hashCombine(result.layoutHash, quantize(width * 1000.0f + height * 10.0f + depth));
        }

        flushKit();  // Fallback-mode kit meshes for this settlement
        uploadPending();

        SDL_Log("SettlementBlockout: %s - %d/%d buildings placed",
                settlement.displayName().c_str(), created, targetCount);
        result.totalBuildings += created;
    }

    return {result.entities.begin() + entitiesBefore, result.entities.end()};
}
