#include "SettlementBlockoutGenerator.h"
#include "WorldCoords.h"

#include "ecs/EntityFactory.h"
#include "scene/DeterministicRandom.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>

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

// Positive when the XZ ring is counter-clockwise in math orientation
// (interior on the left of each directed edge)
float ringSignedArea(const std::vector<glm::vec2>& ring) {
    float area = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2& a = ring[i];
        const glm::vec2& b = ring[(i + 1) % ring.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

float cross2(const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool pointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                     const glm::vec2& c) {
    float d1 = cross2(a, b, p);
    float d2 = cross2(b, c, p);
    float d3 = cross2(c, a, p);
    bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

// Ear-clip a CCW (positive-area) simple polygon into index triples
std::vector<std::array<size_t, 3>> triangulateRing(const std::vector<glm::vec2>& ring) {
    std::vector<std::array<size_t, 3>> tris;
    std::vector<size_t> idx(ring.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

    while (idx.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            size_t ia = idx[(i + idx.size() - 1) % idx.size()];
            size_t ib = idx[i];
            size_t ic = idx[(i + 1) % idx.size()];
            if (cross2(ring[ia], ring[ib], ring[ic]) <= 1e-6f) continue;  // Reflex/degenerate

            bool earClear = true;
            for (size_t j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                if (pointInTriangle(ring[j], ring[ia], ring[ib], ring[ic])) {
                    earClear = false;
                    break;
                }
            }
            if (!earClear) continue;

            tris.push_back({ia, ib, ic});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // Degenerate polygon: fall back to a fan so we never loop forever
            for (size_t i = 1; i + 1 < idx.size(); ++i) {
                tris.push_back({idx[0], idx[i], idx[i + 1]});
            }
            return tris;
        }
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    return tris;
}

// Append an extruded footprint prism (walls + flat roof, no floor) to the
// merged mesh. Follows the createCube convention: outward faces wound CCW as
// seen from outside.
void appendBuildingPrism(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                         std::vector<glm::vec2> ring, float baseY, float topY) {
    if (ring.size() >= 2 && glm::distance(ring.front(), ring.back()) < 1e-3f) {
        ring.pop_back();  // Drop GeoJSON closing vertex
    }
    if (ring.size() < 3) return;
    if (ringSignedArea(ring) < 0.0f) std::reverse(ring.begin(), ring.end());

    const float wallHeight = topY - baseY;

    // Walls: outward normal is the right side of each directed edge.
    // UVs are world-anchored (0.5/m -> texture repeats every 2m) so tiling is
    // continuous along a wall and consistent across all buildings.
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2& p0 = ring[i];
        const glm::vec2& p1 = ring[(i + 1) % ring.size()];
        glm::vec2 edge = p1 - p0;
        float len = glm::length(edge);
        if (len < 1e-4f) continue;
        glm::vec2 d = edge / len;
        glm::vec3 normal(d.y, 0.0f, -d.x);
        glm::vec4 tangent(-d.x, 0.0f, -d.y, 1.0f);

        const float uvScale = 0.5f;
        float u0 = glm::dot(p1, -d) * uvScale;  // Along the +U (tangent) direction
        float u1 = glm::dot(p0, -d) * uvScale;
        float vBase = -baseY * uvScale;
        float vTop = -topY * uvScale;

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({{p1.x, baseY, p1.y}, normal, {u0, vBase}, tangent});
        verts.push_back({{p0.x, baseY, p0.y}, normal, {u1, vBase}, tangent});
        verts.push_back({{p0.x, topY, p0.y}, normal, {u1, vTop}, tangent});
        verts.push_back({{p1.x, topY, p1.y}, normal, {u0, vTop}, tangent});
        inds.insert(inds.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }

    // Flat roof
    uint32_t roofBase = static_cast<uint32_t>(verts.size());
    for (const auto& p : ring) {
        verts.push_back({{p.x, topY, p.y}, {0.0f, 1.0f, 0.0f},
                         {p.x * 0.1f, p.y * 0.1f}, {1.0f, 0.0f, 0.0f, 1.0f}});
    }
    for (const auto& tri : triangulateRing(ring)) {
        // CCW-from-above needs reversed order relative to the math-CCW ring
        inds.push_back(roofBase + static_cast<uint32_t>(tri[2]));
        inds.push_back(roofBase + static_cast<uint32_t>(tri[1]));
        inds.push_back(roofBase + static_cast<uint32_t>(tri[0]));
    }
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

} // namespace

SettlementBlockoutGenerator::Result SettlementBlockoutGenerator::generate(
    ecs::World& world,
    const std::vector<Settlement>& settlements,
    const Config& config) {

    Result result;
    result.layoutHash = 2166136261u;  // FNV offset basis

    if (!config.buildingMesh || !config.getTerrainHeight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SettlementBlockoutGenerator: missing mesh or height query");
        return result;
    }

    ecs::EntityFactory factory(world);
    int totalBuildings = 0;

    for (const auto& settlement : settlements) {
        if (config.preloadTiles) {
            config.preloadTiles(settlement.worldPos.x, settlement.worldPos.y,
                                settlement.radius + 50.0f);
        }

        // Footprint-driven placement when a town layout exists
        if (!config.townsDir.empty()) {
            std::string townPath = config.townsDir + "/town_" +
                                   std::to_string(settlement.id) + ".geojson";
            auto footprints = loadTownFootprints(townPath);
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
                // world-space mesh per settlement (exact plot shapes, one draw)
                std::vector<Vertex> meshVerts;
                std::vector<uint32_t> meshInds;
                const bool mergedMode = static_cast<bool>(config.createMesh);

                for (size_t slotIdx : selected) {
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

                    if (mergedMode) {
                        // Base sinks 0.2m below the lowest corner so no wall floats.
                        // A single mesh collider is built from this geometry
                        // after the loop (matches concave footprints exactly).
                        appendBuildingPrism(meshVerts, meshInds,
                                            footprints[eligible[slotIdx].footprintIndex].first,
                                            minH - 0.2f, minH - 0.2f + height);
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

                    hashCombine(result.layoutHash, settlement.id);
                    hashCombine(result.layoutHash, quantize(box.center.x));
                    hashCombine(result.layoutHash, quantize(box.center.y));
                    hashCombine(result.layoutHash, quantize(minH));
                    hashCombine(result.layoutHash, quantize(box.width * 1000.0f + height * 10.0f + box.depth));
                }

                if (mergedMode && !meshVerts.empty()) {
                    if (config.addMeshCollider) {
                        config.addMeshCollider(meshVerts, meshInds);
                    }
                    if (Mesh* mesh = config.createMesh(std::move(meshVerts), std::move(meshInds))) {
                        ecs::Entity entity = factory.createBuilding(
                            mesh, config.materialId, glm::mat4(1.0f), settlement.id);
                        result.entities.push_back(entity);
                    } else {
                        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                                     "SettlementBlockout: mesh upload failed for %s",
                                     settlement.displayName().c_str());
                    }
                }

                SDL_Log("SettlementBlockout: %s - %d buildings from town layout "
                        "(%zu footprints, %zu eligible, %zu steep)",
                        settlement.displayName().c_str(), created,
                        footprints.size(), eligible.size(), steepSlots);
                totalBuildings += created;
                continue;  // Next settlement; skip the random fallback
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
            placed.push_back(pos);
            created++;

            hashCombine(result.layoutHash, settlement.id);
            hashCombine(result.layoutHash, quantize(pos.x));
            hashCombine(result.layoutHash, quantize(pos.y));
            hashCombine(result.layoutHash, quantize(groundY));
            hashCombine(result.layoutHash, quantize(width * 1000.0f + height * 10.0f + depth));
        }

        SDL_Log("SettlementBlockout: %s - %d/%d buildings placed",
                settlement.displayName().c_str(), created, targetCount);
        totalBuildings += created;
    }

    SDL_Log("SettlementBlockout: %d buildings in %zu scene entities, %zu slots rejected, "
            "layout hash %08x",
            totalBuildings, result.entities.size(), result.rejectedSlots, result.layoutHash);
    return result;
}
