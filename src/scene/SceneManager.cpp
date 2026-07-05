#include "SceneManager.h"
#include "lighting/LightSystem.h"
#include "ecs/Components.h"
#include <SDL3/SDL.h>
#include <algorithm>

std::unique_ptr<SceneManager> SceneManager::create(SceneBuilder::InitInfo& builderInfo) {
    auto system = std::make_unique<SceneManager>(ConstructToken{});
    if (!system->initInternal(builderInfo)) {
        return nullptr;
    }
    return system;
}

SceneManager::~SceneManager() {
    cleanup();
}

bool SceneManager::initInternal(SceneBuilder::InitInfo& builderInfo) {
    // Store for cleanup
    storedAllocator = builderInfo.allocator;
    storedDevice = builderInfo.device;

    // Store terrain height function for physics placement
    terrainHeightFunc = builderInfo.getTerrainHeight;

    // Store scene origin for light positioning
    sceneOrigin = builderInfo.sceneOrigin;

    // Initialize scene builder (meshes, textures, objects)
    sceneBuilder = SceneBuilder::create(builderInfo);
    if (!sceneBuilder) {
        SDL_Log("Failed to initialize SceneBuilder");
        return false;
    }

    // Initialize scene lights
    initializeSceneLights();

    SDL_Log("SceneManager initialized successfully");
    return true;
}

float SceneManager::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc) {
        return terrainHeightFunc(x, z);
    }
    return 0.0f;
}

void SceneManager::initPhysics(PhysicsWorld& physics) {
    // Store physics pointer for deferred initialization callback
    storedPhysics_ = &physics;

    // Register callback for when deferred renderables are created. initializeScenePhysics is
    // idempotent (skips entities that already have a body), so it is safe to call from here
    // and from ensureScenePhysics().
    sceneBuilder->setOnRenderablesCreated([this]() {
        if (storedPhysics_) {
            SDL_Log("SceneManager: Deferred renderables created, initializing physics bodies...");
            initializeScenePhysics(*storedPhysics_);
        }
    });

    // Initialize immediately if renderables already exist
    if (sceneBuilder->hasRenderables()) {
        initializeScenePhysics(physics);
    }
}

void SceneManager::initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                                       uint32_t sampleCount, float worldSize, float heightScale) {
    // Call overload with no hole mask
    initTerrainPhysics(physics, heightSamples, nullptr, sampleCount, worldSize, heightScale);
}

void SceneManager::initTerrainPhysics(PhysicsWorld& physics, const float* heightSamples,
                                       const uint8_t* holeMask, uint32_t sampleCount,
                                       float worldSize, float heightScale) {
    // Create heightfield collision shape from terrain data (with optional hole mask)
    PhysicsBodyID terrainBody = physics.createTerrainHeightfield(
        heightSamples, holeMask, sampleCount, worldSize, heightScale
    );

    if (terrainBody != INVALID_BODY_ID) {
        SDL_Log("Terrain heightfield physics initialized%s", holeMask ? " (with hole mask)" : "");
    } else {
        SDL_Log("Failed to create terrain heightfield, falling back to flat ground");
        physics.createTerrainDisc(worldSize * 0.5f, 0.0f);
    }
}

void SceneManager::cleanup() {
    // SceneBuilder is RAII-managed, just reset the unique_ptr
    sceneBuilder.reset();
}

void SceneManager::update(PhysicsWorld& physics) {
    updatePhysicsToScene(physics);
}

void SceneManager::updatePlayerTransform(const glm::mat4& transform) {
    sceneBuilder->updatePlayerTransform(transform);
}

void SceneManager::initializeScenePhysics(PhysicsWorld& physics) {
    // NOTE: Terrain physics is now initialized separately via initTerrainPhysics()
    // which creates a heightfield from the TerrainSystem's height data

    // Physics bodies live entirely on their entities as ecs::PhysicsBody components; there is
    // no parallel index-aligned array. This is idempotent: entities that already have a body
    // are skipped, so it can safely run from both the deferred callback and the post-ECS re-run.
    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneManager: No ECS world during physics init - bodies will be linked later");
        return;
    }

    const float spawnOffset = 0.1f;
    size_t bodyCount = 0;
    for (auto [entity, shapeInfo, transform] :
         ecsWorld_->view<ecs::PhysicsShapeInfo, ecs::Transform>().each()) {

        if (ecsWorld_->has<ecs::PhysicsBody>(entity)) continue;  // already created

        glm::vec3 pos = transform.position();

        PhysicsBodyID bodyId;
        if (shapeInfo.shapeType == ecs::PhysicsShapeType::Box) {
            bodyId = physics.createBox(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                shapeInfo.halfExtents, shapeInfo.mass);
        } else {
            bodyId = physics.createSphere(
                glm::vec3(pos.x, pos.y + spawnOffset, pos.z),
                shapeInfo.radius(), shapeInfo.mass);
        }

        if (bodyId != INVALID_BODY_ID) {
            ecsWorld_->add<ecs::PhysicsBody>(entity, static_cast<ecs::PhysicsBodyId>(bodyId));
            bodyCount++;
        }
    }
    if (bodyCount > 0) {
        SDL_Log("Scene physics initialized with %zu bodies from ECS components", bodyCount);
    }
}

void SceneManager::initializeECSLights() {
    // Only initialize if ECS world is available
    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "initializeECSLights called without ECS world");
        return;
    }

    // Re-initialize scene lights using ECS
    initializeSceneLights();
}

void SceneManager::initializeSceneLights() {
    if (!ecsWorld_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "initializeSceneLights: No ECS world available, skipping light creation");
        return;
    }

    // Helper to apply scene origin offset
    auto worldPos = [this](float localX, float localZ) -> glm::vec2 {
        return glm::vec2(localX + sceneOrigin.x, localZ + sceneOrigin.y);
    };

    // Orb light - flickering torch that follows the emissive sphere
    auto orbPos = worldPos(2.0f, 0.0f);
    glm::vec3 orbPosition(orbPos.x, 1.3f, orbPos.y);
    orbLightEntity_ = ecs::light::createTorch(*ecsWorld_, orbPosition, 5.0f);
    ecsWorld_->add<ecs::DebugName>(orbLightEntity_, "Orb Torch");

    // Blue point light
    auto bluePos = worldPos(-3.0f, 2.0f);
    glm::vec3 bluePosition(bluePos.x, 2.0f, bluePos.y);
    blueLightEntity_ = ecs::light::createPointLight(
        *ecsWorld_, bluePosition,
        glm::vec3(0.3f, 0.5f, 1.0f),  // Blue color
        3.0f,                          // Intensity
        6.0f);                         // Radius
    ecsWorld_->add<ecs::DebugName>(blueLightEntity_, "Blue Light");

    // Green point light
    auto greenPos = worldPos(4.0f, -2.0f);
    glm::vec3 greenPosition(greenPos.x, 1.5f, greenPos.y);
    greenLightEntity_ = ecs::light::createPointLight(
        *ecsWorld_, greenPosition,
        glm::vec3(0.4f, 1.0f, 0.4f),  // Green color
        2.5f,                          // Intensity
        5.0f);                         // Radius
    ecsWorld_->add<ecs::DebugName>(greenLightEntity_, "Green Light");

    SDL_Log("ECS scene lights initialized (3 light entities)");
}

void SceneManager::updatePhysicsToScene(PhysicsWorld& physics) {
    if (!ecsWorld_) return;

    for (auto [entity, physBody] : ecsWorld_->view<ecs::PhysicsBody>().each()) {
        if (!physBody.valid()) continue;

        // Skip player (handled separately by physics character controller)
        if (ecsWorld_->has<ecs::PlayerTag>(entity)) continue;

        PhysicsBodyID bodyID = static_cast<PhysicsBodyID>(physBody.bodyId);
        glm::mat4 physicsTransform = physics.getBodyTransform(bodyID);

        // ecs::Transform is the sole authority. Recover the (physics-less) scale from the
        // current ECS transform, reapply it to the new rigid-body transform, and write back.
        if (ecsWorld_->has<ecs::Transform>(entity)) {
            glm::mat4& matrix = ecsWorld_->get<ecs::Transform>(entity).matrix;
            glm::vec3 scale;
            scale.x = glm::length(glm::vec3(matrix[0]));
            scale.y = glm::length(glm::vec3(matrix[1]));
            scale.z = glm::length(glm::vec3(matrix[2]));

            physicsTransform = glm::scale(physicsTransform, scale);
            matrix = physicsTransform;
        }

        // Update orb light position to follow the emissive sphere
        if (ecsWorld_->has<ecs::OrbTag>(entity)) {
            glm::vec3 orbPosition = glm::vec3(physicsTransform[3]);
            orbLightPosition = orbPosition;

            if (orbLightEntity_ != ecs::NullEntity && ecsWorld_->valid(orbLightEntity_)) {
                if (ecsWorld_->has<ecs::Transform>(orbLightEntity_)) {
                    ecsWorld_->get<ecs::Transform>(orbLightEntity_).matrix =
                        ecs::Transform::fromPosition(orbPosition).matrix;
                }
            }
        }
    }
}
