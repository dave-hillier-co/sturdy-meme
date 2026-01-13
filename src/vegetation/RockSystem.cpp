#include "RockSystem.h"
#include "../ecs/Components.h"
#include "../ecs/EnvironmentIntegration.h"
#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

std::unique_ptr<RockSystem> RockSystem::create(const InitInfo& info, const RockConfig& config) {
    std::unique_ptr<RockSystem> system(new RockSystem());
    if (!system->initInternal(info, config)) {
        return nullptr;
    }
    return system;
}

RockSystem::~RockSystem() {
    cleanup();
}

bool RockSystem::initInternal(const InitInfo& info, const RockConfig& cfg) {
    config = cfg;
    storedAllocator = info.allocator;
    storedDevice = info.device;
    registry_ = info.registry;

    if (!registry_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RockSystem: No ECS registry provided");
        return false;
    }

    if (!loadTextures(info)) {
        SDL_Log("RockSystem: Failed to load textures");
        return false;
    }

    if (!createRockMeshes(info)) {
        SDL_Log("RockSystem: Failed to create rock meshes");
        return false;
    }

    generateRockPlacements(info);
    createSceneObjects();

    SDL_Log("RockSystem: Initialized with %zu rocks (%zu mesh variations)",
            getRockCount(), rockMeshes.size());

    return true;
}

void RockSystem::cleanup() {
    if (storedDevice == VK_NULL_HANDLE) return;

    // RAII-managed textures
    rockTexture.reset();
    rockNormalMap.reset();

    // Manually managed mesh vector
    for (auto& mesh : rockMeshes) {
        mesh.releaseGPUResources();
    }
    rockMeshes.clear();

    // Note: ECS entities are managed by the registry, not cleaned up here
    sceneObjects.clear();
}

bool RockSystem::loadTextures(const InitInfo& info) {
    // Use concrete texture as a rock-like surface
    std::string texturePath = info.resourcePath + "/assets/textures/industrial/concrete_1.jpg";
    rockTexture = Texture::loadFromFile(texturePath, info.allocator, info.device, info.commandPool,
                                         info.graphicsQueue, info.physicalDevice);
    if (!rockTexture) {
        SDL_Log("RockSystem: Failed to load rock texture: %s", texturePath.c_str());
        return false;
    }

    std::string normalPath = info.resourcePath + "/assets/textures/industrial/concrete_1_norm.jpg";
    rockNormalMap = Texture::loadFromFile(normalPath, info.allocator, info.device, info.commandPool,
                                           info.graphicsQueue, info.physicalDevice, false);
    if (!rockNormalMap) {
        SDL_Log("RockSystem: Failed to load rock normal map: %s", normalPath.c_str());
        return false;
    }

    return true;
}

bool RockSystem::createRockMeshes(const InitInfo& info) {
    rockMeshes.resize(config.rockVariations);

    for (int i = 0; i < config.rockVariations; ++i) {
        // Use different seeds for each variation
        uint32_t seed = 12345 + i * 7919;  // Prime number for better distribution

        // Vary parameters slightly for each rock type
        float roughnessVariation = config.roughness * (0.8f + 0.4f * hashPosition(float(i), 0.0f, seed));
        float asymmetryVariation = config.asymmetry * (0.7f + 0.6f * hashPosition(float(i), 1.0f, seed + 100));

        rockMeshes[i].createRock(1.0f, config.subdivisions, seed, roughnessVariation, asymmetryVariation);
        rockMeshes[i].upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
    }

    return true;
}

float RockSystem::hashPosition(float x, float z, uint32_t seed) const {
    // Simple hash function for deterministic pseudo-random values
    uint32_t ix = *reinterpret_cast<uint32_t*>(&x);
    uint32_t iz = *reinterpret_cast<uint32_t*>(&z);
    uint32_t n = ix ^ (iz * 1597334673U) ^ seed;
    n = (n << 13U) ^ n;
    n = n * (n * n * 15731U + 789221U) + 1376312589U;
    return float(n & 0x7fffffffU) / float(0x7fffffff);
}

void RockSystem::generateRockPlacements(const InitInfo& info) {
    // Collect existing rock positions for distance checking
    std::vector<glm::vec3> placedPositions;

    // Use Poisson disk-like sampling for natural rock distribution
    const int totalRocks = config.rockVariations * config.rocksPerVariation;
    const float minDist = config.minDistanceBetween;
    const float minDistSq = minDist * minDist;

    // Golden angle for spiral distribution
    const float goldenAngle = 2.39996323f;

    int placed = 0;
    int attempts = 0;
    const int maxAttempts = totalRocks * 20;

    while (placed < totalRocks && attempts < maxAttempts) {
        attempts++;

        // Generate candidate position using various methods
        float x, z;

        if (attempts % 3 == 0) {
            // Spiral distribution
            float radius = config.placementRadius * std::sqrt(float(placed + 1) / float(totalRocks + 1));
            float angle = placed * goldenAngle;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        } else {
            // Random with hash
            float angle = hashPosition(float(attempts), 0.0f, 54321) * 2.0f * 3.14159f;
            float radius = std::sqrt(hashPosition(float(attempts), 1.0f, 54322)) * config.placementRadius;
            x = radius * std::cos(angle);
            z = radius * std::sin(angle);
        }

        // Add some jitter
        x += (hashPosition(x, z, 11111) - 0.5f) * minDist * 0.5f;
        z += (hashPosition(x, z, 22222) - 0.5f) * minDist * 0.5f;

        // Offset by placement center
        x += config.placementCenter.x;
        z += config.placementCenter.y;

        // Check bounds (rocks must be inside terrain)
        float halfTerrain = info.terrainSize * 0.48f;  // Stay slightly inside terrain
        if (std::abs(x) > halfTerrain || std::abs(z) > halfTerrain) {
            continue;
        }

        // Check distance from existing rocks
        bool tooClose = false;
        for (const auto& existingPos : placedPositions) {
            float dx = x - existingPos.x;
            float dz = z - existingPos.z;
            if (dx * dx + dz * dz < minDistSq) {
                tooClose = true;
                break;
            }
        }

        if (tooClose) {
            continue;
        }

        // Get terrain height at this position
        float y = 0.0f;
        if (info.getTerrainHeight) {
            y = info.getTerrainHeight(x, z);
        }

        // Skip very steep or very low areas (water level)
        if (y < 0.5f) {
            continue;
        }

        // Calculate rock properties
        glm::vec3 position(x, y, z);
        float rotation = hashPosition(x, z, 33333) * 2.0f * 3.14159f;

        // Random scale within configured range
        float t = hashPosition(x, z, 44444);
        float scale = config.minRadius + t * (config.maxRadius - config.minRadius);

        // Assign mesh variation
        uint32_t meshVariation = placed % config.rockVariations;

        // Add slight random tilt for natural appearance
        float tiltX = (hashPosition(x, z, 55555) - 0.5f) * 0.15f;
        float tiltZ = (hashPosition(x, z, 66666) - 0.5f) * 0.15f;
        glm::vec3 eulerRotation(tiltX, rotation, tiltZ);

        // Create ECS entity
        EnvironmentECS::createRock(
            *registry_,
            position,
            meshVariation,
            scale,
            eulerRotation,
            "Rock_" + std::to_string(placed)
        );

        placedPositions.push_back(position);
        placed++;
    }

    SDL_Log("RockSystem: Placed %d rocks in %d attempts", placed, attempts);
}

void RockSystem::createSceneObjects() {
    sceneObjects.clear();

    if (!registry_) return;

    // Query all rock entities from ECS
    auto view = registry_->view<RockInstance, Transform>();
    sceneObjects.reserve(view.size_hint());

    for (auto [entity, rock, transform] : view.each()) {
        // Build transform matrix: translate, rotate (Euler), scale
        glm::mat4 mat = glm::translate(glm::mat4(1.0f), transform.position);

        // Apply Euler rotations (stored in rock.rotation as vec3)
        mat = glm::rotate(mat, rock.rotation.y, glm::vec3(0.0f, 1.0f, 0.0f));  // Yaw
        mat = glm::rotate(mat, rock.rotation.x, glm::vec3(1.0f, 0.0f, 0.0f));  // Pitch/tilt
        mat = glm::rotate(mat, rock.rotation.z, glm::vec3(0.0f, 0.0f, 1.0f));  // Roll/tilt

        mat = glm::scale(mat, glm::vec3(rock.scale));

        // Sink rock slightly into ground
        mat[3][1] -= rock.scale * 0.15f;

        // Bounds check mesh variant
        uint32_t meshIdx = rock.meshVariant;
        if (meshIdx >= rockMeshes.size()) {
            meshIdx = 0;
        }

        sceneObjects.push_back(RenderableBuilder()
            .withTransform(mat)
            .withMesh(&rockMeshes[meshIdx])
            .withTexture(rockTexture.get())
            .withRoughness(config.materialRoughness)
            .withMetallic(config.materialMetallic)
            .withCastsShadow(rock.castsShadow)
            .build());
    }
}

size_t RockSystem::getRockCount() const {
    if (!registry_) return 0;
    return registry_->view<RockInstance>().size();
}

std::vector<RockInstanceData> RockSystem::getRockInstances() const {
    std::vector<RockInstanceData> result;
    if (!registry_) return result;

    auto view = registry_->view<RockInstance, Transform>();
    result.reserve(view.size_hint());

    for (auto [entity, rock, transform] : view.each()) {
        RockInstanceData data;
        data.position = transform.position;
        data.rotation = rock.rotation.y;  // Y-axis rotation for physics
        data.scale = rock.scale;
        data.meshVariation = static_cast<int>(rock.meshVariant);
        result.push_back(data);
    }

    return result;
}
