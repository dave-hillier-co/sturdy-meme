#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <algorithm>
#include "Components.h"

// Extended Rendering ECS Integration (Phase 7)
// Factory functions and utilities for decals, sprites, render targets, and probes
namespace RenderingECS {

// ============================================================================
// Decal System
// ============================================================================

// Create a decal entity
inline entt::entity createDecal(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& direction,
    const glm::vec3& size = glm::vec3(1.0f),
    MaterialHandle material = InvalidMaterial,
    const std::string& name = "Decal")
{
    auto entity = registry.create();

    // Transform with rotation to face direction
    Transform transform;
    transform.position = position;
    // Calculate yaw from direction (XZ plane)
    if (glm::length(glm::vec2(direction.x, direction.z)) > 0.001f) {
        transform.yaw = glm::degrees(atan2(direction.x, direction.z));
    }
    registry.emplace<Transform>(entity, transform);

    // Decal component
    Decal decal;
    decal.material = material;
    decal.size = size;
    registry.emplace<Decal>(entity, decal);

    // Entity info
    EntityInfo info;
    info.name = name;
    info.icon = "D";
    registry.emplace<EntityInfo>(entity, info);

    // Bounds for culling
    AABBBounds bounds;
    bounds.min = -size * 0.5f;
    bounds.max = size * 0.5f;
    registry.emplace<AABBBounds>(entity, bounds);

    return entity;
}

// Create a bullet hole decal
inline entt::entity createBulletHole(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& surfaceNormal,
    MaterialHandle material = InvalidMaterial)
{
    auto entity = createDecal(registry, position, surfaceNormal,
                              glm::vec3(0.1f, 0.1f, 0.05f), material, "BulletHole");

    auto& decal = registry.get<Decal>(entity);
    decal.fadeDistance = 20.0f;
    decal.angleFade = 0.3f;
    decal.affectsNormal = true;

    return entity;
}

// Get all decals sorted by sort order
inline std::vector<entt::entity> getSortedDecals(entt::registry& registry) {
    std::vector<entt::entity> decals;
    auto view = registry.view<Decal, Transform>();
    for (auto entity : view) {
        decals.push_back(entity);
    }

    std::sort(decals.begin(), decals.end(), [&registry](entt::entity a, entt::entity b) {
        return registry.get<Decal>(a).sortOrder < registry.get<Decal>(b).sortOrder;
    });

    return decals;
}

// ============================================================================
// Sprite System
// ============================================================================

// Create a sprite entity
inline entt::entity createSprite(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec2& size = glm::vec2(1.0f),
    TextureHandle texture = InvalidTexture,
    const std::string& name = "Sprite")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    SpriteRenderer sprite;
    sprite.texture = texture;
    sprite.size = size;
    registry.emplace<SpriteRenderer>(entity, sprite);

    EntityInfo info;
    info.name = name;
    info.icon = "S";
    registry.emplace<EntityInfo>(entity, info);

    // Bounding sphere for culling
    float maxDim = glm::max(size.x, size.y);
    registry.emplace<BoundingSphere>(entity, BoundingSphere{maxDim * 0.5f});

    return entity;
}

// Create an animated sprite
inline entt::entity createAnimatedSprite(
    entt::registry& registry,
    const glm::vec3& position,
    TextureHandle atlasTexture,
    uint32_t frameCount,
    float fps = 12.0f,
    const glm::vec2& size = glm::vec2(1.0f),
    const std::string& name = "AnimatedSprite")
{
    auto entity = createSprite(registry, position, size, InvalidTexture, name);

    auto& sprite = registry.get<SpriteRenderer>(entity);
    sprite.atlasTexture = atlasTexture;
    sprite.frameCount = frameCount;
    sprite.framesPerSecond = fps;
    sprite.animating = true;
    sprite.loopAnimation = true;

    // Calculate UV rect for first frame (assuming horizontal strip)
    float frameWidth = 1.0f / static_cast<float>(frameCount);
    sprite.uvRect = glm::vec4(0.0f, 0.0f, frameWidth, 1.0f);

    return entity;
}

// Update sprite animation
inline void updateSpriteAnimations(entt::registry& registry, float deltaTime) {
    auto view = registry.view<SpriteRenderer>();

    for (auto entity : view) {
        auto& sprite = view.get<SpriteRenderer>(entity);

        if (!sprite.animating || sprite.frameCount <= 1) continue;

        sprite.frameTime += deltaTime;
        float frameDuration = 1.0f / sprite.framesPerSecond;

        while (sprite.frameTime >= frameDuration) {
            sprite.frameTime -= frameDuration;
            sprite.currentFrame++;

            if (sprite.currentFrame >= sprite.frameCount) {
                if (sprite.loopAnimation) {
                    sprite.currentFrame = 0;
                } else {
                    sprite.currentFrame = sprite.frameCount - 1;
                    sprite.animating = false;
                }
            }
        }

        // Update UV rect based on current frame
        float frameWidth = 1.0f / static_cast<float>(sprite.frameCount);
        sprite.uvRect.x = frameWidth * sprite.currentFrame;
        sprite.uvRect.z = frameWidth;
    }
}

// ============================================================================
// Render Target System
// ============================================================================

// Create a render target camera
inline entt::entity createRenderTargetCamera(
    entt::registry& registry,
    uint32_t width,
    uint32_t height,
    const glm::vec3& position = glm::vec3(0.0f),
    const std::string& name = "RenderTargetCamera")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    CameraComponent camera;
    camera.fov = 60.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 500.0f;
    registry.emplace<CameraComponent>(entity, camera);

    RenderTarget rt;
    rt.width = width;
    rt.height = height;
    rt.colorFormat = RenderTarget::Format::RGBA8;
    rt.hasDepth = true;
    rt.updateMode = RenderTarget::UpdateMode::EveryFrame;
    registry.emplace<RenderTarget>(entity, rt);

    EntityInfo info;
    info.name = name;
    info.icon = "R";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a security camera entity
inline entt::entity createSecurityCamera(
    entt::registry& registry,
    const glm::vec3& position,
    float yaw,
    uint32_t resolution = 256,
    const std::string& name = "SecurityCamera")
{
    auto entity = createRenderTargetCamera(registry, resolution, resolution, position, name);

    registry.get<Transform>(entity).yaw = yaw;

    auto& rt = registry.get<RenderTarget>(entity);
    rt.updateMode = RenderTarget::UpdateMode::Interval;
    rt.updateInterval = 1.0f / 15.0f;  // 15 FPS

    auto& camera = registry.get<CameraComponent>(entity);
    camera.fov = 90.0f;

    return entity;
}

// Update render targets that need updating
inline std::vector<entt::entity> getRenderTargetsNeedingUpdate(
    entt::registry& registry,
    float deltaTime)
{
    std::vector<entt::entity> needUpdate;
    auto view = registry.view<RenderTarget>();

    for (auto entity : view) {
        auto& rt = view.get<RenderTarget>(entity);

        switch (rt.updateMode) {
            case RenderTarget::UpdateMode::EveryFrame:
                needUpdate.push_back(entity);
                break;

            case RenderTarget::UpdateMode::OnDemand:
                if (rt.needsUpdate) {
                    needUpdate.push_back(entity);
                    rt.needsUpdate = false;
                }
                break;

            case RenderTarget::UpdateMode::Interval:
                rt.timeSinceUpdate += deltaTime;
                if (rt.timeSinceUpdate >= rt.updateInterval) {
                    needUpdate.push_back(entity);
                    rt.timeSinceUpdate = 0.0f;
                }
                break;
        }
    }

    return needUpdate;
}

// ============================================================================
// Reflection Probe System
// ============================================================================

// Create a reflection probe entity
inline entt::entity createReflectionProbe(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& extents = glm::vec3(10.0f),
    ReflectionProbe::Resolution resolution = ReflectionProbe::Resolution::Medium,
    const std::string& name = "ReflectionProbe")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    ReflectionProbe probe;
    probe.extents = extents;
    probe.resolution = resolution;
    probe.blendDistance = glm::min(extents.x, glm::min(extents.y, extents.z)) * 0.2f;
    registry.emplace<ReflectionProbe>(entity, probe);
    registry.emplace<IsReflectionProbe>(entity);

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "P";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Find reflection probes affecting a position
inline std::vector<entt::entity> findAffectingReflectionProbes(
    entt::registry& registry,
    const glm::vec3& position,
    int maxProbes = 2)
{
    std::vector<std::pair<entt::entity, float>> probesWithWeight;

    auto view = registry.view<ReflectionProbe, Transform>();
    for (auto entity : view) {
        auto& probe = view.get<ReflectionProbe>(entity);
        auto& transform = view.get<Transform>(entity);

        glm::vec3 localPos = position - transform.position;
        glm::vec3 absLocal = glm::abs(localPos);

        // Check if inside influence box
        if (absLocal.x <= probe.extents.x + probe.blendDistance &&
            absLocal.y <= probe.extents.y + probe.blendDistance &&
            absLocal.z <= probe.extents.z + probe.blendDistance)
        {
            // Calculate blend weight based on distance from edge
            float distFromEdge = glm::min(
                glm::min(probe.extents.x + probe.blendDistance - absLocal.x,
                         probe.extents.y + probe.blendDistance - absLocal.y),
                probe.extents.z + probe.blendDistance - absLocal.z
            );
            float weight = glm::clamp(distFromEdge / probe.blendDistance, 0.0f, 1.0f);

            probesWithWeight.push_back({entity, weight * (probe.priority + 1)});
        }
    }

    // Sort by weight (higher first)
    std::sort(probesWithWeight.begin(), probesWithWeight.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });

    // Return top N probes
    std::vector<entt::entity> result;
    for (size_t i = 0; i < std::min(probesWithWeight.size(), static_cast<size_t>(maxProbes)); i++) {
        result.push_back(probesWithWeight[i].first);
    }

    return result;
}

// Get probes that need capture
inline std::vector<entt::entity> getProbesNeedingCapture(
    entt::registry& registry,
    float deltaTime)
{
    std::vector<entt::entity> needCapture;
    auto view = registry.view<ReflectionProbe>();

    for (auto entity : view) {
        auto& probe = view.get<ReflectionProbe>(entity);

        if (probe.needsCapture) {
            needCapture.push_back(entity);
            probe.needsCapture = false;
        } else if (probe.realtime) {
            probe.timeSinceCapture += deltaTime;
            if (probe.timeSinceCapture >= probe.updateInterval) {
                needCapture.push_back(entity);
                probe.timeSinceCapture = 0.0f;
            }
        }
    }

    return needCapture;
}

// ============================================================================
// Light Probe System
// ============================================================================

// Create a light probe entity
inline entt::entity createLightProbe(
    entt::registry& registry,
    const glm::vec3& position,
    float influence = 10.0f,
    const std::string& name = "LightProbe")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    LightProbe probe;
    probe.influence = influence;
    probe.blendDistance = influence * 0.2f;
    registry.emplace<LightProbe>(entity, probe);
    registry.emplace<IsLightProbe>(entity);

    registry.emplace<BoundingSphere>(entity, BoundingSphere{influence});

    EntityInfo info;
    info.name = name;
    info.icon = "L";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a light probe volume with multiple probes
inline std::vector<entt::entity> createLightProbeGrid(
    entt::registry& registry,
    const glm::vec3& center,
    const glm::vec3& extents,
    const glm::ivec3& probeCount,
    const std::string& baseName = "LightProbe")
{
    std::vector<entt::entity> probes;

    // Create volume entity
    auto volumeEntity = registry.create();
    registry.emplace<Transform>(volumeEntity, Transform{center, 0.0f});

    LightProbeVolume volume;
    volume.extents = extents;
    volume.probeCount = probeCount;
    volume.probeSpacing = 2.0f * extents.x / static_cast<float>(probeCount.x - 1);
    registry.emplace<LightProbeVolume>(volumeEntity, volume);

    EntityInfo volumeInfo;
    volumeInfo.name = baseName + "_Volume";
    volumeInfo.icon = "V";
    registry.emplace<EntityInfo>(volumeEntity, volumeInfo);

    // Create individual probes
    glm::vec3 spacing = 2.0f * extents / glm::vec3(probeCount - glm::ivec3(1));
    glm::vec3 start = center - extents;

    int index = 0;
    for (int z = 0; z < probeCount.z; z++) {
        for (int y = 0; y < probeCount.y; y++) {
            for (int x = 0; x < probeCount.x; x++) {
                glm::vec3 pos = start + glm::vec3(x, y, z) * spacing;
                float influence = glm::length(spacing) * 0.6f;

                auto probe = createLightProbe(registry, pos, influence,
                    baseName + "_" + std::to_string(index++));
                probes.push_back(probe);
            }
        }
    }

    return probes;
}

// Find light probes affecting a position and calculate interpolation weights
struct LightProbeWeight {
    entt::entity entity;
    float weight;
};

inline std::vector<LightProbeWeight> findAffectingLightProbes(
    entt::registry& registry,
    const glm::vec3& position,
    int maxProbes = 4)
{
    std::vector<LightProbeWeight> probesWithWeight;

    auto view = registry.view<LightProbe, Transform>();
    for (auto entity : view) {
        auto& probe = view.get<LightProbe>(entity);
        auto& transform = view.get<Transform>(entity);

        float dist = glm::distance(position, transform.position);
        float maxDist = probe.influence + probe.blendDistance;

        if (dist <= maxDist) {
            float weight = 1.0f - glm::clamp((dist - probe.influence) / probe.blendDistance, 0.0f, 1.0f);
            if (dist < probe.influence) weight = 1.0f;

            probesWithWeight.push_back({entity, weight * (probe.priority + 1)});
        }
    }

    // Sort by weight
    std::sort(probesWithWeight.begin(), probesWithWeight.end(),
              [](const auto& a, const auto& b) { return a.weight > b.weight; });

    // Return top N and normalize weights
    std::vector<LightProbeWeight> result;
    float totalWeight = 0.0f;

    for (size_t i = 0; i < std::min(probesWithWeight.size(), static_cast<size_t>(maxProbes)); i++) {
        result.push_back(probesWithWeight[i]);
        totalWeight += probesWithWeight[i].weight;
    }

    // Normalize
    if (totalWeight > 0.0f) {
        for (auto& pw : result) {
            pw.weight /= totalWeight;
        }
    }

    return result;
}

// Interpolate SH coefficients from multiple probes
inline void interpolateLightProbeSH(
    entt::registry& registry,
    const std::vector<LightProbeWeight>& probes,
    glm::vec3 outSH[9])
{
    // Initialize to zero
    for (int i = 0; i < 9; i++) {
        outSH[i] = glm::vec3(0.0f);
    }

    // Weighted sum
    for (const auto& pw : probes) {
        if (!registry.valid(pw.entity)) continue;
        auto& probe = registry.get<LightProbe>(pw.entity);

        for (int i = 0; i < 9; i++) {
            outSH[i] += probe.shCoefficients[i] * pw.weight;
        }
    }
}

// ============================================================================
// Portal/Mirror System
// ============================================================================

// Create a mirror entity
inline entt::entity createMirror(
    entt::registry& registry,
    const glm::vec3& position,
    float yaw,
    const glm::vec2& size = glm::vec2(2.0f, 3.0f),
    uint32_t resolution = 512,
    const std::string& name = "Mirror")
{
    auto entity = registry.create();

    registry.emplace<Transform>(entity, Transform{position, yaw});

    // Render target for mirror view
    RenderTarget rt;
    rt.width = resolution;
    rt.height = resolution;
    rt.updateMode = RenderTarget::UpdateMode::EveryFrame;
    registry.emplace<RenderTarget>(entity, rt);

    // Portal surface configured as mirror
    PortalSurface portal;
    portal.isMirror = true;
    portal.twoSided = false;
    registry.emplace<PortalSurface>(entity, portal);

    // Mesh for display
    MeshRenderer mesh;
    mesh.castsShadow = false;
    registry.emplace<MeshRenderer>(entity, mesh);

    AABBBounds bounds;
    bounds.min = glm::vec3(-size.x * 0.5f, 0.0f, -0.05f);
    bounds.max = glm::vec3(size.x * 0.5f, size.y, 0.05f);
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "M";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create linked portal pair
inline std::pair<entt::entity, entt::entity> createPortalPair(
    entt::registry& registry,
    const glm::vec3& posA,
    float yawA,
    const glm::vec3& posB,
    float yawB,
    uint32_t resolution = 512,
    const std::string& nameA = "PortalA",
    const std::string& nameB = "PortalB")
{
    auto portalA = registry.create();
    auto portalB = registry.create();

    // Portal A
    registry.emplace<Transform>(portalA, Transform{posA, yawA});

    RenderTarget rtA;
    rtA.width = resolution;
    rtA.height = resolution;
    registry.emplace<RenderTarget>(portalA, rtA);

    PortalSurface surfaceA;
    surfaceA.targetPortal = portalB;
    surfaceA.isMirror = false;
    registry.emplace<PortalSurface>(portalA, surfaceA);

    registry.emplace<MeshRenderer>(portalA, MeshRenderer{});

    EntityInfo infoA;
    infoA.name = nameA;
    infoA.icon = "O";
    registry.emplace<EntityInfo>(portalA, infoA);

    // Portal B
    registry.emplace<Transform>(portalB, Transform{posB, yawB});

    RenderTarget rtB;
    rtB.width = resolution;
    rtB.height = resolution;
    registry.emplace<RenderTarget>(portalB, rtB);

    PortalSurface surfaceB;
    surfaceB.targetPortal = portalA;
    surfaceB.isMirror = false;
    registry.emplace<PortalSurface>(portalB, surfaceB);

    registry.emplace<MeshRenderer>(portalB, MeshRenderer{});

    EntityInfo infoB;
    infoB.name = nameB;
    infoB.icon = "O";
    registry.emplace<EntityInfo>(portalB, infoB);

    return {portalA, portalB};
}

// ============================================================================
// Query Functions
// ============================================================================

// Get all decals
inline std::vector<entt::entity> getDecals(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<Decal>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all sprites
inline std::vector<entt::entity> getSprites(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<SpriteRenderer>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all reflection probes
inline std::vector<entt::entity> getReflectionProbes(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsReflectionProbe>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all light probes
inline std::vector<entt::entity> getLightProbes(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsLightProbe>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all portals/mirrors
inline std::vector<entt::entity> getPortals(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<PortalSurface>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// ============================================================================
// Statistics
// ============================================================================

struct RenderingStats {
    uint32_t decalCount;
    uint32_t spriteCount;
    uint32_t animatedSpriteCount;
    uint32_t renderTargetCount;
    uint32_t reflectionProbeCount;
    uint32_t lightProbeCount;
    uint32_t portalCount;
    uint32_t mirrorCount;
};

inline RenderingStats getRenderingStats(entt::registry& registry) {
    RenderingStats stats{};

    stats.decalCount = static_cast<uint32_t>(registry.view<Decal>().size());
    stats.spriteCount = static_cast<uint32_t>(registry.view<SpriteRenderer>().size());
    stats.renderTargetCount = static_cast<uint32_t>(registry.view<RenderTarget>().size());
    stats.reflectionProbeCount = static_cast<uint32_t>(registry.view<IsReflectionProbe>().size());
    stats.lightProbeCount = static_cast<uint32_t>(registry.view<IsLightProbe>().size());

    // Count animated sprites
    auto spriteView = registry.view<SpriteRenderer>();
    for (auto entity : spriteView) {
        if (spriteView.get<SpriteRenderer>(entity).animating) {
            stats.animatedSpriteCount++;
        }
    }

    // Count portals vs mirrors
    auto portalView = registry.view<PortalSurface>();
    for (auto entity : portalView) {
        if (portalView.get<PortalSurface>(entity).isMirror) {
            stats.mirrorCount++;
        } else {
            stats.portalCount++;
        }
    }

    return stats;
}

}  // namespace RenderingECS
