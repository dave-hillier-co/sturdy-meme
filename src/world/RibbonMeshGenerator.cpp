#include "RibbonMeshGenerator.h"
#include "ecs/EntityFactory.h"

#include <SDL3/SDL_log.h>
#include <algorithm>
#include <cmath>

namespace {

struct Sample {
    glm::vec3 position;   // Centerline point (y = surface height)
    glm::vec2 direction;  // Normalized XZ direction
    float width;
    bool skip;            // Inside a skip zone - break the strip here
};

} // namespace

RibbonMeshGenerator::Result RibbonMeshGenerator::generate(
    ecs::World& world, const std::vector<Ribbon>& ribbons,
    const std::vector<SkipZone>& skipZones) {
    Result result;
    if (!config_.createMeshes || !config_.getTerrainHeight) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "RibbonMeshGenerator: missing mesh sink or height query");
        return result;
    }

    std::vector<MeshGeometry> pendingGeo;
    struct PendingMeta {
        MaterialId material;
        bool castsShadow;
    };
    std::vector<PendingMeta> pendingMeta;

    auto inSkipZone = [&](glm::vec2 xz) {
        for (const auto& zone : skipZones) {
            if (glm::distance(xz, zone.center) < zone.radius) return true;
        }
        return false;
    };

    for (const auto& ribbon : ribbons) {
        if (ribbon.points.size() < 2) continue;

        if (config_.preloadTiles) {
            // Preload around the ribbon midpoint; per-sample preloads would
            // thrash the cache for long roads.
            const glm::vec3& mid = ribbon.points[ribbon.points.size() / 2];
            float extent = 0.0f;
            for (const auto& p : ribbon.points) {
                extent = std::max(extent, glm::distance(glm::vec2(p.x, p.z),
                                                        glm::vec2(mid.x, mid.z)));
            }
            config_.preloadTiles(mid.x, mid.z, extent + 50.0f);
        }

        // Resample the polyline at a fixed spacing
        std::vector<Sample> samples;
        auto widthAt = [&](size_t i) {
            if (ribbon.widths.empty()) return 4.0f;
            return ribbon.widths[std::min(i, ribbon.widths.size() - 1)];
        };
        for (size_t i = 0; i + 1 < ribbon.points.size(); ++i) {
            glm::vec3 a = ribbon.points[i];
            glm::vec3 b = ribbon.points[i + 1];
            glm::vec2 abXZ(b.x - a.x, b.z - a.z);
            float segLength = glm::length(abXZ);
            if (segLength < 0.001f) continue;
            glm::vec2 dir = abXZ / segLength;
            int steps = std::max(1, static_cast<int>(segLength / config_.sampleSpacing));
            for (int s = 0; s < steps; ++s) {
                float t = static_cast<float>(s) / steps;
                glm::vec3 p = glm::mix(a, b, t);
                float w = glm::mix(widthAt(i), widthAt(i + 1), t);
                samples.push_back({p, dir, w, inSkipZone(glm::vec2(p.x, p.z))});
            }
        }
        if (!samples.empty()) {
            const glm::vec3& last = ribbon.points.back();
            samples.push_back({last, samples.back().direction,
                               widthAt(ribbon.points.size() - 1),
                               inSkipZone(glm::vec2(last.x, last.z))});
        }
        if (samples.size() < 2) continue;

        // Smooth directions with central differences for clean miters
        for (size_t i = 1; i + 1 < samples.size(); ++i) {
            glm::vec2 d(samples[i + 1].position.x - samples[i - 1].position.x,
                        samples[i + 1].position.z - samples[i - 1].position.z);
            float len = glm::length(d);
            if (len > 0.001f) samples[i].direction = d / len;
        }

        // Emit strip chunks, breaking at skip zones and chunk boundaries
        MeshGeometry geo;
        float chunkDistance = 0.0f;
        bool stripOpen = false;

        auto flushChunk = [&]() {
            if (geo.vertices.size() >= 4) {
                pendingGeo.push_back(std::move(geo));
                pendingMeta.push_back({ribbon.material, ribbon.castsShadow});
                ++result.chunksBuilt;
            }
            geo = MeshGeometry{};
            chunkDistance = 0.0f;
            stripOpen = false;
        };

        auto emitCrossSection = [&](const Sample& s) {
            glm::vec2 perp(-s.direction.y, s.direction.x);
            float halfWidth = s.width * 0.5f;
            float v = chunkDistance * config_.uvScaleAlong;

            for (float side : {-1.0f, 1.0f}) {
                glm::vec2 xz(s.position.x + perp.x * halfWidth * side,
                             s.position.z + perp.y * halfWidth * side);
                float y = ribbon.followTerrain
                              ? config_.getTerrainHeight(xz.x, xz.y) + config_.lift
                              : s.position.y + config_.lift;
                Vertex vert{};
                vert.position = glm::vec3(xz.x, y, xz.y);
                vert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vert.texCoord = glm::vec2(side < 0.0f ? 0.0f : 1.0f, v);
                vert.tangent = glm::vec4(s.direction.x, 0.0f, s.direction.y, 1.0f);
                vert.color = ribbon.color;
                geo.vertices.push_back(vert);
            }
            if (stripOpen) {
                uint32_t base = static_cast<uint32_t>(geo.vertices.size()) - 4;
                // Two triangles between the previous and current cross-sections
                geo.indices.insert(geo.indices.end(),
                                   {base, base + 2, base + 1,
                                    base + 1, base + 2, base + 3});
            }
            stripOpen = true;
        };

        for (size_t i = 0; i < samples.size(); ++i) {
            const Sample& s = samples[i];
            if (s.skip) {
                flushChunk();
                continue;
            }
            if (i > 0 && stripOpen) {
                chunkDistance += glm::distance(
                    glm::vec2(s.position.x, s.position.z),
                    glm::vec2(samples[i - 1].position.x, samples[i - 1].position.z));
            }
            emitCrossSection(s);
            if (chunkDistance >= config_.chunkLength) {
                // Close this chunk and restart the strip at the same sample so
                // chunks stay watertight.
                flushChunk();
                emitCrossSection(s);
            }
        }
        flushChunk();
    }

    if (pendingGeo.empty()) {
        return result;
    }

    std::vector<Mesh*> meshes = config_.createMeshes(std::move(pendingGeo));
    ecs::EntityFactory factory(world);
    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!meshes[i]) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "RibbonMeshGenerator: mesh upload failed");
            continue;
        }
        result.entities.push_back(factory.createStaticMesh(
            meshes[i], pendingMeta[i].material, glm::mat4(1.0f),
            pendingMeta[i].castsShadow));
    }

    SDL_Log("RibbonMeshGenerator: built %zu ribbon chunks", result.chunksBuilt);
    return result;
}
