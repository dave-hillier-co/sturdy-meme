#include "SceneBuilder.h"
#include "Transform.h"
#include "PhysicsSystem.h"
#include "asset/AssetRegistry.h"
#include "npc/NPCSimulation.h"
#include "ecs/EntityFactory.h"
#include "ecs/Systems.h"
#include "core/vulkan/VmaBufferFactory.h"
#include "core/vulkan/CommandBufferUtils.h"
#include <SDL3/SDL_log.h>

// Constructor must be defined in .cpp to allow unique_ptr<NPCSimulation> with incomplete type in header
SceneBuilder::SceneBuilder(ConstructToken) {}

std::unique_ptr<SceneBuilder> SceneBuilder::create(const InitInfo& info) {
    auto instance = std::make_unique<SceneBuilder>(ConstructToken{});
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

SceneBuilder::~SceneBuilder() {
    cleanup();
}

Mesh* SceneBuilder::addGeneratedMesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices) {
    if (vertices.empty() || indices.empty()) return nullptr;
    auto mesh = std::make_unique<Mesh>();
    mesh->setCustomGeometry(vertices, indices);
    if (!mesh->upload(storedAllocator, storedDevice, storedCommandPool, storedGraphicsQueue)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SceneBuilder: failed to upload generated mesh (%zu verts)", vertices.size());
        return nullptr;
    }
    generatedMeshes_.push_back(std::move(mesh));
    return generatedMeshes_.back().get();
}

std::vector<Mesh*> SceneBuilder::addGeneratedMeshes(std::vector<MeshGeometry> batch) {
    std::vector<Mesh*> out(batch.size(), nullptr);

    std::vector<std::unique_ptr<Mesh>> meshes(batch.size());
    VkDeviceSize total = 0;
    for (size_t i = 0; i < batch.size(); ++i) {
        if (batch[i].vertices.empty() || batch[i].indices.empty()) continue;
        meshes[i] = std::make_unique<Mesh>();
        meshes[i]->setCustomGeometry(std::move(batch[i].vertices), std::move(batch[i].indices));
        total += meshes[i]->stagedSize();
    }
    if (total == 0) return out;

    ManagedBuffer staging;
    void* mapped = nullptr;
    if (!VmaBufferFactory::createStagingBuffer(storedAllocator, total, staging) ||
        !(mapped = staging.map())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SceneBuilder: batched staging alloc failed (%llu bytes), "
                     "falling back to per-mesh uploads",
                     static_cast<unsigned long long>(total));
        for (size_t i = 0; i < meshes.size(); ++i) {
            if (!meshes[i]) continue;
            if (meshes[i]->upload(storedAllocator, storedDevice, storedCommandPool,
                                  storedGraphicsQueue)) {
                meshes[i]->releaseCpuGeometry();
                generatedMeshes_.push_back(std::move(meshes[i]));
                out[i] = generatedMeshes_.back().get();
            }
        }
        return out;
    }

    VkDeviceSize offset = 0;
    for (const auto& mesh : meshes) {
        if (!mesh) continue;
        mesh->copyGeometryTo(static_cast<char*>(mapped) + offset);
        offset += mesh->stagedSize();
    }
    staging.unmap();

    CommandScope cmd(storedDevice, storedCommandPool, storedGraphicsQueue);
    if (!cmd.begin()) return out;
    offset = 0;
    std::vector<bool> recorded(meshes.size(), false);
    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!meshes[i]) continue;
        VkDeviceSize meshOffset = offset;
        offset += meshes[i]->stagedSize();
        recorded[i] = meshes[i]->uploadFromStaging(storedAllocator, cmd.get(),
                                                   staging.get(), meshOffset);
    }
    // Single submit + fence wait for the whole batch
    if (!cmd.end()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SceneBuilder: batched mesh upload submit failed (%zu meshes)",
                     meshes.size());
        return out;
    }

    for (size_t i = 0; i < meshes.size(); ++i) {
        if (!meshes[i] || !recorded[i]) continue;
        meshes[i]->releaseCpuGeometry();
        generatedMeshes_.push_back(std::move(meshes[i]));
        out[i] = generatedMeshes_.back().get();
    }
    return out;
}

bool SceneBuilder::initInternal(const InitInfo& info) {
    // Store terrain height function for object placement
    terrainHeightFunc = info.getTerrainHeight;
    storedAllocator = info.allocator;
    storedDevice = info.device;
    storedCommandPool = info.commandPool;
    storedGraphicsQueue = info.graphicsQueue;
    sceneOrigin = info.sceneOrigin;

    // Slice 5: the merged createRenderables() pass creates ECS entities, so the
    // ECS world must be available before it runs. Adopt it from InitInfo if
    // provided. In the deferred path the world is instead set via setECSWorld()
    // (from Application::initECS) before createRenderablesDeferred() is triggered.
    if (info.ecsWorld) {
        ecsWorld_ = info.ecsWorld;
    }

    // Calculate well entrance position immediately (needed for terrain hole creation)
    // This must be available even if renderables are deferred
    wellEntranceX = 20.0f + sceneOrigin.x;
    wellEntranceZ = 20.0f + sceneOrigin.y;

    if (!info.assetRegistry) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: AssetRegistry is required");
        return false;
    }
    assetRegistry_.emplace(*info.assetRegistry);

    if (!createMeshes(info)) return false;
    if (!loadTextures(info)) return false;
    registerMaterials();

    // Create renderables now or defer for later
    if (!info.deferRenderables) {
        createRenderables();
        renderablesCreated_ = true;
    } else {
        SDL_Log("SceneBuilder: Deferring renderables creation until terrain is ready");
    }
    return true;
}

void SceneBuilder::setECSWorld(ecs::World* world) {
    ecsWorld_ = world;
    // NPCs are spawned during init (before the world is set in the deferred path);
    // propagate so the NPC simulation creates its entities now that the world exists.
    if (npcSimulation_) {
        npcSimulation_->setECSWorld(world);
    }
}

void SceneBuilder::createRenderablesDeferred() {
    if (renderablesCreated_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Renderables already created");
        return;
    }
    if (!ecsWorld_) {
        // Slice 5: the merged creation pass needs the ECS world. This indicates an
        // ordering bug (setECSWorld must run before deferred creation); log loudly.
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "SceneBuilder: ECS world not set before deferred renderable creation; "
            "entities will not be created");
    }
    SDL_Log("SceneBuilder: Creating deferred renderables now");
    createRenderables();
    renderablesCreated_ = true;

    // Notify listeners (e.g., SceneManager for physics initialization)
    if (onRenderablesCreated_) {
        onRenderablesCreated_();
    }
}

void SceneBuilder::createEntitiesFromRenderables() {
    // Entity creation is performed inline by createRenderables(), which builds each ECS entity
    // (with all tags/components/hierarchy) directly. This function is retained only as an
    // idempotent shim: it is still invoked from Application::initECS and the deferred lazy
    // path, but does nothing once the creation pass has run.
    if (!ecsWorld_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: ECS world not set");
        return;
    }

    if (!renderablesCreated_) {
        // Renderables not created yet (deferred mode); the creation pass will run
        // when createRenderablesDeferred() is invoked later.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: No renderables to create entities from");
        return;
    }

    if (sceneEntities_.empty()) {
        // Renderables were created but no entities exist: createRenderables() must have run
        // without an ECS world set. Log so the ordering bug is visible.
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneBuilder: renderables created but no entities; "
            "createRenderables() must run with the ECS world set");
    }
}

void SceneBuilder::createSceneEntity(const ecs::RenderData& obj, ObjectRole role) {
    // Merged second-pass entity creation for a single scene object. Produces the
    // exact same components/tags/hierarchy as the former
    // createEntitiesFromRenderables() did, but inline as each Renderable is built.
    if (!ecsWorld_) return;

    // Physics shape parameters
    constexpr float BOX_MASS = 10.0f;
    constexpr float SPHERE_MASS = 5.0f;
    constexpr float ORB_MASS = 1.0f;
    const glm::vec3 cubeHalfExtents(0.5f);

    ecs::EntityFactory factory(*ecsWorld_);

    // Core components (Transform, MeshRef, MaterialRef, CastsShadow, PBRProperties,
    // HueShift, Opacity, GPUSkinned) via value-gated factory.
    ecs::Entity entity = factory.createFromRenderable(obj);
    sceneEntities_.push_back(entity);

    // Bounding sphere for culling (estimated from mesh) + Visible on every entity.
    glm::vec3 pos = glm::vec3(obj.transform[3]);
    float radius = 2.0f;
    if (obj.mesh) {
        float scale = glm::length(glm::vec3(obj.transform[0]));
        radius = scale * 2.0f;
    }
    ecsWorld_->add<ecs::BoundingSphere>(entity, pos, radius);
    ecsWorld_->add<ecs::Visible>(entity);

    // Tag by mesh identity: cubes/spheres get debug names and physics shapes.
    if (obj.mesh == cubeMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Cube");
        ecsWorld_->add<ecs::PhysicsShapeInfo>(entity,
            ecs::PhysicsShapeInfo::box(cubeHalfExtents, BOX_MASS));
    } else if (obj.mesh == sphereMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Sphere");
        float scale = glm::length(glm::vec3(obj.transform[0]));
        if (scale < 0.5f && role == ObjectRole::EmissiveOrb) {
            // The emissive orb is dynamic (its torch light follows it as it settles).
            // Other small emissive spheres (blue/green light indicators) share the
            // scale<0.5 size but are decorative markers and get no physics body.
            ecsWorld_->add<ecs::PhysicsShapeInfo>(entity,
                ecs::PhysicsShapeInfo::sphere(0.5f * scale, ORB_MASS));
        } else if (scale < 0.5f || obj.emissiveIntensity > 1.0f) {
            // Small/emissive indicator spheres (blue/green lights) - no physics
        } else {
            // Normal spheres
            ecsWorld_->add<ecs::PhysicsShapeInfo>(entity,
                ecs::PhysicsShapeInfo::sphere(0.5f, SPHERE_MASS));
        }
    } else if (obj.mesh == capsuleMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Capsule");
    } else if (obj.mesh == flagPoleMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Flag Pole");
    } else if (obj.mesh == swordMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Sword");
    } else if (obj.mesh == shieldMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Shield");
    } else if (obj.mesh == axisLineMesh.get()) {
        ecsWorld_->add<ecs::DebugName>(entity, "Debug Axis");
    } else if (obj.mesh == &flagClothMesh) {
        ecsWorld_->add<ecs::DebugName>(entity, "Flag Cloth");
    } else if (obj.mesh == &capeMesh) {
        ecsWorld_->add<ecs::DebugName>(entity, "Cape");
    }

    // Tag special entities by their role and cache entity handles.
    switch (role) {
        case ObjectRole::Player:
            playerEntity_ = entity;
            ecsWorld_->add<ecs::PlayerTag>(entity);
            break;
        case ObjectRole::EmissiveOrb:
            emissiveOrbEntity_ = entity;
            ecsWorld_->add<ecs::OrbTag>(entity);
            break;
        case ObjectRole::FlagPole:
            flagPoleEntity_ = entity;
            ecsWorld_->add<ecs::FlagPoleTag>(entity);
            break;
        case ObjectRole::FlagCloth:
            flagClothEntity_ = entity;
            ecsWorld_->add<ecs::FlagClothTag>(entity);
            break;
        case ObjectRole::Cape:
            capeEntity_ = entity;
            ecsWorld_->add<ecs::CapeTag>(entity);
            break;
        case ObjectRole::Sword:
            swordEntity_ = entity;
            ecsWorld_->add<ecs::WeaponTag>(entity, ecs::WeaponSlot::RightHand);
            if (rightHandBoneIndex >= 0) {
                ecsWorld_->add<ecs::BoneAttachment>(entity, rightHandBoneIndex, getSwordOffset());
            }
            break;
        case ObjectRole::Shield:
            shieldEntity_ = entity;
            ecsWorld_->add<ecs::WeaponTag>(entity, ecs::WeaponSlot::LeftHand);
            if (leftHandBoneIndex >= 0) {
                ecsWorld_->add<ecs::BoneAttachment>(entity, leftHandBoneIndex, getShieldOffset());
            }
            break;
        case ObjectRole::WellEntrance:
            wellEntranceEntity_ = entity;
            ecsWorld_->add<ecs::WellEntranceTag>(entity);
            // The well entrance is a decorative frame floating above the terrain hole.
            // It uses the cube mesh, so the mesh-identity branch above gave it a dynamic
            // box body - remove it so the frame stays put instead of falling.
            if (ecsWorld_->has<ecs::PhysicsShapeInfo>(entity)) {
                ecsWorld_->remove<ecs::PhysicsShapeInfo>(entity);
            }
            break;
        case ObjectRole::DebugAxisRightX:
            rightHandAxisEntities_[0] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (rightHandBoneIndex >= 0) {
                glm::mat4 off = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1));
                off = glm::translate(off, glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, rightHandBoneIndex, off);
            }
            break;
        case ObjectRole::DebugAxisRightY:
            rightHandAxisEntities_[1] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (rightHandBoneIndex >= 0) {
                glm::mat4 off = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, rightHandBoneIndex, off);
            }
            break;
        case ObjectRole::DebugAxisRightZ:
            rightHandAxisEntities_[2] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (rightHandBoneIndex >= 0) {
                glm::mat4 off = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0));
                off = glm::translate(off, glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, rightHandBoneIndex, off);
            }
            break;
        case ObjectRole::DebugAxisLeftX:
            leftHandAxisEntities_[0] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (leftHandBoneIndex >= 0) {
                glm::mat4 off = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(0,0,1));
                off = glm::translate(off, glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, leftHandBoneIndex, off);
            }
            break;
        case ObjectRole::DebugAxisLeftY:
            leftHandAxisEntities_[1] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (leftHandBoneIndex >= 0) {
                glm::mat4 off = glm::translate(glm::mat4(1.0f), glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, leftHandBoneIndex, off);
            }
            break;
        case ObjectRole::DebugAxisLeftZ:
            leftHandAxisEntities_[2] = entity;
            ecsWorld_->add<ecs::DebugAxisTag>(entity);
            if (leftHandBoneIndex >= 0) {
                glm::mat4 off = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1,0,0));
                off = glm::translate(off, glm::vec3(0, 0.075f, 0));
                ecsWorld_->add<ecs::BoneAttachment>(entity, leftHandBoneIndex, off);
            }
            break;
        case ObjectRole::None:
            break;
    }
}

void SceneBuilder::finalizeSceneEntities() {
    // Establish parent-child hierarchy and tag NPC entities once all scene
    // objects (and their entities) have been created in the merged pass.
    if (!ecsWorld_) return;

    // Establish parent-child hierarchy: attach weapons, cape, and debug axes to player
    if (playerEntity_ != ecs::NullEntity) {
        // Ensure player has Children component for top-down traversal
        if (!ecsWorld_->has<ecs::Children>(playerEntity_)) {
            ecsWorld_->add<ecs::Children>(playerEntity_);
        }

        auto attachChild = [&](ecs::Entity child) {
            if (child != ecs::NullEntity) {
                ecs::systems::attachToParent(*ecsWorld_, child, playerEntity_);
            }
        };

        attachChild(swordEntity_);
        attachChild(shieldEntity_);
        attachChild(capeEntity_);
        for (auto e : rightHandAxisEntities_) attachChild(e);
        for (auto e : leftHandAxisEntities_) attachChild(e);
    }

    // NPCs are simulation entities owned by NPCSimulation and rendered by NPCRenderer;
    // they are not part of sceneEntities_ and need no tagging here.

    SDL_Log("SceneBuilder: Created %zu ECS entities successfully", sceneEntities_.size());
}

void SceneBuilder::registerMaterials() {
    // Get textures from registry
    const Texture* crateTex = getCrateTexture();
    const Texture* crateNorm = getCrateNormalMap();
    const Texture* groundTex = getGroundTexture();
    const Texture* groundNorm = getGroundNormalMap();
    const Texture* metalTex = getMetalTexture();
    const Texture* metalNorm = getMetalNormalMap();
    const Texture* whiteTex = getWhiteTexture();

    if (!crateTex || !crateNorm || !groundTex || !groundNorm ||
        !metalTex || !metalNorm || !whiteTex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Missing textures for material registration");
        return;
    }

    // Register crate material
    crateMaterialId = materialRegistry.registerMaterial("crate", *crateTex, *crateNorm);

    // Register ground material (for any ground-related objects)
    groundMaterialId = materialRegistry.registerMaterial("ground", *groundTex, *groundNorm);

    // Register metal material
    metalMaterialId = materialRegistry.registerMaterial("metal", *metalTex, *metalNorm);

    // Register white material (for vertex-colored objects like animated characters)
    // Uses white texture with a flat normal map
    whiteMaterialId = materialRegistry.registerMaterial("white", *whiteTex, *groundNorm);

    // Register cape material (using metal texture)
    capeMaterialId = materialRegistry.registerMaterial("cape", *metalTex, *metalNorm);

    SDL_Log("SceneBuilder: Registered %zu materials", materialRegistry.getMaterialCount());
}

void SceneBuilder::registerKitMaterials(const std::string& resourcePath) {
    TextureLoadConfig srgb{};
    srgb.useSRGB = true;
    srgb.generateMipmaps = true;
    TextureLoadConfig linear{};
    linear.useSRGB = false;
    linear.generateMipmaps = true;

    const std::string dir = resourcePath + "/assets/textures/buildings/";

    // Quaternius Medieval Village kit material -> (BaseColor, Normal) textures.
    // RedBrick has no dedicated normal map, so it borrows the brick normal.
    struct KitMat { const char* name; const char* base; const char* normal; };
    const KitMat kitMats[] = {
        {"MI_Plaster",     "T_Plaster_BaseColor.png",     "T_Plaster_Normal.png"},
        {"MI_WoodTrim",    "T_WoodTrim_BaseColor.png",    "T_WoodTrim_Normal.png"},
        {"MI_Brick",       "T_Brick_BaseColor.png",       "T_Brick_Normal.png"},
        {"MI_RedBrick",    "T_RedBrick_BaseColor.png",    "T_Brick_Normal.png"},
        {"MI_UnevenBrick", "T_UnevenBrick_BaseColor.png", "T_UnevenBrick_Normal.png"},
        {"MI_RockTrim",    "T_RockTrim_BaseColor.png",    "T_RockTrim_Normal.png"},
        {"MI_RoundTiles",  "T_RoundTiles_BaseColor.png",  "T_RoundTiles_Normal.png"},
        // Window panes: gradient sheen with a flat(ish) normal
        {"MI_WindowGlass", "T_WindowGradient.png",        "T_Plaster_Normal.png"},
    };

    auto& registry = assetRegistry_->get();
    for (const auto& m : kitMats) {
        auto base = registry.loadTexture(dir + m.base, srgb);
        auto norm = registry.loadTexture(dir + m.normal, linear);
        if (!base || !norm) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SceneBuilder: kit material '%s' missing textures, skipping", m.name);
            continue;
        }
        MaterialId id = materialRegistry.registerMaterial(m.name, *base, *norm);
        kitMaterials_[m.name] = id;
        // WoodTrim variants (worn) reuse the base wood material
        if (std::string(m.name) == "MI_WoodTrim") kitMaterials_["MI_WoodTrim_Wear"] = id;
        kitTextures_.push_back(std::move(base));
        kitTextures_.push_back(std::move(norm));
    }

    SDL_Log("SceneBuilder: Registered %zu kit materials", kitMaterials_.size());
}

MaterialId SceneBuilder::getKitMaterialId(const std::string& kitMaterialName) const {
    auto it = kitMaterials_.find(kitMaterialName);
    if (it != kitMaterials_.end()) return it->second;
    return whiteMaterialId;  // glass, vine, ornaments: fall back to a plain material
}

float SceneBuilder::getTerrainHeight(float x, float z) const {
    if (terrainHeightFunc) {
        return terrainHeightFunc(x, z);
    }
    return 0.0f;  // Default to ground level if no terrain
}

void SceneBuilder::cleanup() {
    // Release texture shared_ptrs (textures freed automatically when last reference is released)
    crateTexture_.reset();
    crateNormal_.reset();
    groundTexture_.reset();
    groundNormal_.reset();
    metalTexture_.reset();
    metalNormal_.reset();
    defaultEmissive_.reset();
    whiteTexture_.reset();

    // RAII-managed meshes (static)
    cubeMesh.reset();
    sphereMesh.reset();
    capsuleMesh.reset();
    flagPoleMesh.reset();
    swordMesh.reset();
    shieldMesh.reset();
    axisLineMesh.reset();

    // Manually managed meshes (dynamic - re-uploaded during runtime)
    flagClothMesh.releaseGPUResources();
    capeMesh.releaseGPUResources();

    // RAII-managed animated character
    animatedCharacter.reset();

    // RAII-managed NPC simulation
    npcSimulation_.reset();

    sceneEntities_.clear();
}

bool SceneBuilder::createMeshes(const InitInfo& info) {

    cubeMesh = std::make_unique<Mesh>();
    cubeMesh->createCube();
    if (!cubeMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    sphereMesh = std::make_unique<Mesh>();
    sphereMesh->createSphere(0.5f, 32, 32);
    if (!sphereMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Player capsule mesh (1.8m tall, 0.3m radius)
    capsuleMesh = std::make_unique<Mesh>();
    capsuleMesh->createCapsule(0.3f, 1.8f, 16, 16);
    if (!capsuleMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Flag pole mesh (cylinder: 0.05m radius, 3m height)
    flagPoleMesh = std::make_unique<Mesh>();
    flagPoleMesh->createCylinder(0.05f, 3.0f, 16);
    if (!flagPoleMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Sword mesh (long thin cylinder: 0.02m radius, 0.8m length)
    swordMesh = std::make_unique<Mesh>();
    swordMesh->createCylinder(0.02f, 0.8f, 12);
    if (!swordMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Shield mesh (flat wide cylinder: 0.2m radius, 0.03m thickness)
    shieldMesh = std::make_unique<Mesh>();
    shieldMesh->createCylinder(0.2f, 0.03f, 16);
    if (!shieldMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Axis line mesh for debug visualization (thin cylinder: 0.005m radius, 0.15m length)
    axisLineMesh = std::make_unique<Mesh>();
    axisLineMesh->createCylinder(0.005f, 0.15f, 8);
    if (!axisLineMesh->upload(info.allocator, info.device, info.commandPool, info.graphicsQueue)) return false;

    // Flag cloth mesh will be initialized later by ClothSimulation
    // (it's dynamic and will be updated each frame)

    // Load animated character from FBX
    std::string characterPath = info.resourcePath + "/assets/characters/fbx/Y Bot.fbx";
    std::vector<std::string> additionalAnimations = {
        // Core locomotion - idle
        info.resourcePath + "/assets/characters/fbx/sword and shield idle.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield idle (2).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield idle (3).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield idle (4).fbx",
        // Core locomotion - walk/run
        info.resourcePath + "/assets/characters/fbx/sword and shield walk.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield walk (2).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield run.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield run (2).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield jump.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield jump (2).fbx",
        // Strafe animations
        info.resourcePath + "/assets/characters/fbx/sword and shield strafe.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield strafe (2).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield strafe (3).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield strafe (4).fbx",
        // Turn animations
        info.resourcePath + "/assets/characters/fbx/sword and shield turn.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield turn (2).fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield 180 turn.fbx",
        info.resourcePath + "/assets/characters/fbx/sword and shield 180 turn (2).fbx"
    };

    AnimatedCharacter::InitInfo charInfo{};
    charInfo.path = characterPath;
    charInfo.allocator = info.allocator;
    charInfo.device = info.device;
    charInfo.commandPool = info.commandPool;
    charInfo.queue = info.graphicsQueue;

    animatedCharacter = AnimatedCharacter::create(charInfo);

    if (animatedCharacter) {
        // Load additional animations (sword and shield locomotion set)
        animatedCharacter->loadAdditionalAnimations(additionalAnimations);

        // Setup default IK chains for arms, legs, look-at, and foot placement
        animatedCharacter->setupDefaultIKChains();

        hasAnimatedCharacter = true;
        SDL_Log("SceneBuilder: Loaded FBX animated character");

        // Setup ground query for foot placement IK
        if (terrainHeightFunc) {
            auto& ikSystem = animatedCharacter->getIKSystem();
            ikSystem.setGroundQueryFunc([this](const glm::vec3& position, float maxDistance) -> GroundQueryResult {
                GroundQueryResult result;
                result.hit = true;

                // Get height at position
                float h = getTerrainHeight(position.x, position.z);
                result.position = glm::vec3(position.x, h, position.z);
                result.distance = glm::abs(position.y - h);

                // Compute terrain normal using finite differences
                const float delta = 0.1f;  // 10cm sample distance
                float hPosX = getTerrainHeight(position.x + delta, position.z);
                float hNegX = getTerrainHeight(position.x - delta, position.z);
                float hPosZ = getTerrainHeight(position.x, position.z + delta);
                float hNegZ = getTerrainHeight(position.x, position.z - delta);

                // Tangent vectors
                glm::vec3 tangentX(2.0f * delta, hPosX - hNegX, 0.0f);
                glm::vec3 tangentZ(0.0f, hPosZ - hNegZ, 2.0f * delta);

                // Normal is cross product of tangents
                result.normal = glm::normalize(glm::cross(tangentZ, tangentX));

                return result;
            });
            SDL_Log("SceneBuilder: Setup ground query for foot IK");
        }

        // Initialize player cape attached to the character
        playerCape.create(8, 12, 0.08f);  // 8x12 grid, 8cm spacing
        playerCape.setupDefaultColliders();
        playerCape.setupDefaultAttachments();
        playerCape.createMesh(capeMesh);
        capeMesh.upload(info.allocator, info.device, info.commandPool, info.graphicsQueue);
        hasCapeEnabled = true;
        SDL_Log("SceneBuilder: Initialized player cape");

        // Find hand bone indices for weapon attachment
        const auto& skeleton = animatedCharacter->getSkeleton();
        // Use middle finger bones for better hand positioning (Mixamo uses "mixamorig:" prefix)
        const std::vector<std::string> rightHandNames = {
            "mixamorig:RightHandMiddle1", "RightHandMiddle1",  // Middle finger base
            "mixamorig:RightHand", "RightHand", "R_Hand", "hand.R"  // Fallback to wrist
        };
        const std::vector<std::string> leftHandNames = {
            "mixamorig:LeftHandMiddle1", "LeftHandMiddle1",  // Middle finger base
            "mixamorig:LeftHand", "LeftHand", "L_Hand", "hand.L"  // Fallback to wrist
        };

        for (const auto& name : rightHandNames) {
            rightHandBoneIndex = skeleton.findJointIndex(name);
            if (rightHandBoneIndex >= 0) {
                SDL_Log("SceneBuilder: Found right hand bone '%s' at index %d", name.c_str(), rightHandBoneIndex);
                break;
            }
        }
        for (const auto& name : leftHandNames) {
            leftHandBoneIndex = skeleton.findJointIndex(name);
            if (leftHandBoneIndex >= 0) {
                SDL_Log("SceneBuilder: Found left hand bone '%s' at index %d", name.c_str(), leftHandBoneIndex);
                break;
            }
        }
        if (rightHandBoneIndex < 0 || leftHandBoneIndex < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Could not find hand bones for weapon attachment");
        }
    } else {
        hasAnimatedCharacter = false;
        SDL_Log("SceneBuilder: Failed to load FBX character, using capsule fallback");
    }

    // Create NPCs after main character is loaded
    createNPCs(info);

    return true;
}

void SceneBuilder::createNPCs(const InitInfo& info) {
    // Only create NPCs if the main character loaded successfully
    if (!hasAnimatedCharacter) {
        SDL_Log("SceneBuilder: Skipping NPC creation (no character model available)");
        return;
    }

    // Create NPCSimulation
    NPCSimulation::InitInfo simInfo{};
    simInfo.allocator = info.allocator;
    simInfo.device = info.device;
    simInfo.commandPool = info.commandPool;
    simInfo.graphicsQueue = info.graphicsQueue;
    simInfo.resourcePath = info.resourcePath;
    simInfo.getTerrainHeight = terrainHeightFunc;
    simInfo.sceneOrigin = sceneOrigin;
    simInfo.ecsWorld = info.ecsWorld;  // Pass ECS world for entity creation

    npcSimulation_ = NPCSimulation::create(simInfo);
    if (!npcSimulation_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SceneBuilder: Failed to create NPCSimulation");
        return;
    }

    // NPC spawn positions relative to scene origin with varied activities
    std::vector<NPCSimulation::NPCSpawnInfo> spawnPoints = {
        {  5.0f,  5.0f,  45.0f, 0, NPCActivity::Idle },     // NPC 1: Standing idle
        { -4.0f,  3.0f, 180.0f, 0, NPCActivity::Walking },  // NPC 2: Walking animation
        {  3.0f, -4.0f, -90.0f, 0, NPCActivity::Running },  // NPC 3: Running animation
    };

    size_t created = npcSimulation_->spawnNPCs(spawnPoints);
    SDL_Log("SceneBuilder: Created %zu NPCs via NPCSimulation", created);
}

bool SceneBuilder::loadTextures(const InitInfo& info) {
    // Load textures via AssetRegistry (with caching and deduplication)
    TextureLoadConfig srgbConfig{};
    srgbConfig.useSRGB = true;
    srgbConfig.generateMipmaps = true;

    TextureLoadConfig linearConfig{};
    linearConfig.useSRGB = false;  // Normal maps are linear
    linearConfig.generateMipmaps = true;

    // Crate textures
    std::string texturePath = info.resourcePath + "/assets/textures/crates/crate1/crate1_diffuse.png";
    crateTexture_ = assetRegistry_->get().loadTexture(texturePath, srgbConfig);
    if (!crateTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture: %s", texturePath.c_str());
        return false;
    }

    std::string crateNormalPath = info.resourcePath + "/assets/textures/crates/crate1/crate1_normal.png";
    crateNormal_ = assetRegistry_->get().loadTexture(crateNormalPath, linearConfig);
    if (!crateNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load crate normal map: %s", crateNormalPath.c_str());
        return false;
    }

    // Ground/grass textures
    std::string grassTexturePath = info.resourcePath + "/assets/textures/grass/grass/grass01.jpg";
    groundTexture_ = assetRegistry_->get().loadTexture(grassTexturePath, srgbConfig);
    if (!groundTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass texture: %s", grassTexturePath.c_str());
        return false;
    }

    std::string grassNormalPath = info.resourcePath + "/assets/textures/grass/grass/grass01_n.jpg";
    groundNormal_ = assetRegistry_->get().loadTexture(grassNormalPath, linearConfig);
    if (!groundNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load grass normal map: %s", grassNormalPath.c_str());
        return false;
    }

    // Metal textures
    std::string metalTexturePath = info.resourcePath + "/assets/textures/industrial/metal_1.jpg";
    metalTexture_ = assetRegistry_->get().loadTexture(metalTexturePath, srgbConfig);
    if (!metalTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load metal texture: %s", metalTexturePath.c_str());
        return false;
    }

    std::string metalNormalPath = info.resourcePath + "/assets/textures/industrial/metal_1_norm.jpg";
    metalNormal_ = assetRegistry_->get().loadTexture(metalNormalPath, linearConfig);
    if (!metalNormal_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load metal normal map: %s", metalNormalPath.c_str());
        return false;
    }

    // Create default black emissive map for objects without emissive textures
    defaultEmissive_ = assetRegistry_->get().createSolidColorTexture(0, 0, 0, 255, "default_emissive");
    if (!defaultEmissive_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create default emissive map");
        return false;
    }

    // Create white texture for vertex-colored objects (like glTF characters)
    whiteTexture_ = assetRegistry_->get().createSolidColorTexture(255, 255, 255, 255, "white");
    if (!whiteTexture_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create white texture");
        return false;
    }

    // Load and register the modular building kit's PBR materials
    registerKitMaterials(info.resourcePath);

    SDL_Log("SceneBuilder: Loaded 8 textures via AssetRegistry");
    return true;
}

void SceneBuilder::createRenderables() {
    // Entity creation happens in this pass. Reset the entity list so the pass starts clean
    // (createRenderables runs once, either eagerly or via the deferred path).
    sceneEntities_.clear();
    if (ecsWorld_) {
        sceneEntities_.reserve(32);
    }

    // Ground disc removed - terrain system provides the ground now

    // Scene objects are placed relative to sceneOrigin (settlement location)
    const float originX = sceneOrigin.x;
    const float originZ = sceneOrigin.y;

    // Helper: get world position with scene origin offset
    auto worldPos = [originX, originZ](float localX, float localZ) -> std::pair<float, float> {
        return {localX + originX, localZ + originZ};
    };

    // Helper: get Y position for object sitting on terrain
    // objectHeight is the distance from object origin to its bottom
    auto getGroundY = [this](float x, float z, float objectHeight) {
        return getTerrainHeight(x, z) + objectHeight;
    };

    // Helper to add a scene object: builds its ECS entity (with all tags/components) directly
    // from a transient RenderData. There is no persistent RenderData mirror; the RenderData
    // seeds the entity's components and is then discarded. The ECS world must be set first
    // (guaranteed by both the eager and deferred creation paths).
    auto addObject = [this](ecs::RenderData&& r, ObjectRole role = ObjectRole::None) {
        if (ecsWorld_) {
            createSceneEntity(r, role);
        }
    };

    // Build an ecs::RenderData scene-object mirror entry. Defaults match the former
    // RenderableBuilder defaults (texture was inspection-only and is dropped).
    auto makeObject = [](const glm::mat4& transform, Mesh* mesh, MaterialId materialId,
                         float roughness = 0.5f, float metallic = 0.0f,
                         float emissiveIntensity = 0.0f,
                         const glm::vec3& emissiveColor = glm::vec3(1.0f),
                         bool castsShadow = true, float hueShift = 0.0f,
                         bool gpuSkinned = false) -> ecs::RenderData {
        ecs::RenderData r{};
        r.transform = transform;
        r.mesh = mesh;
        r.materialId = materialId;
        r.roughness = roughness;
        r.metallic = metallic;
        r.emissiveIntensity = emissiveIntensity;
        r.emissiveColor = emissiveColor;
        r.castsShadow = castsShadow;
        r.hueShift = hueShift;
        r.gpuSkinned = gpuSkinned;
        return r;
    };

    // Wooden crate - slightly shiny, non-metallic (unit cube, half-extent 0.5)
    auto [crateX, crateZ] = worldPos(2.0f, 0.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(crateX, getGroundY(crateX, crateZ, 0.5f), crateZ)),
        cubeMesh.get(), crateMaterialId, 0.4f, 0.0f));

    // Rotated wooden crate
    auto [rotatedCrateX, rotatedCrateZ] = worldPos(-1.5f, 1.0f);
    addObject(makeObject(
        Transform(
            glm::vec3(rotatedCrateX, getGroundY(rotatedCrateX, rotatedCrateZ, 0.5f), rotatedCrateZ),
            Transform::yRotation(glm::radians(30.0f))).toMatrix(),
        cubeMesh.get(), crateMaterialId, 0.4f, 0.0f));

    // Polished metal sphere - smooth, fully metallic (radius 0.5)
    auto [polishedSphereX, polishedSphereZ] = worldPos(0.0f, -2.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(polishedSphereX, getGroundY(polishedSphereX, polishedSphereZ, 0.5f), polishedSphereZ)),
        sphereMesh.get(), metalMaterialId, 0.1f, 1.0f));

    // Rough/brushed metal sphere - moderately rough, metallic (radius 0.5)
    auto [roughSphereX, roughSphereZ] = worldPos(-3.0f, -1.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(roughSphereX, getGroundY(roughSphereX, roughSphereZ, 0.5f), roughSphereZ)),
        sphereMesh.get(), metalMaterialId, 0.5f, 1.0f));

    // Polished metal cube - smooth, fully metallic (half-extent 0.5)
    auto [polishedCubeX, polishedCubeZ] = worldPos(3.0f, -2.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(polishedCubeX, getGroundY(polishedCubeX, polishedCubeZ, 0.5f), polishedCubeZ)),
        cubeMesh.get(), metalMaterialId, 0.1f, 1.0f));

    // Brushed metal cube - rough, metallic (half-extent 0.5)
    auto [brushedCubeX, brushedCubeZ] = worldPos(-3.0f, -3.0f);
    addObject(makeObject(
        Transform(
            glm::vec3(brushedCubeX, getGroundY(brushedCubeX, brushedCubeZ, 0.5f), brushedCubeZ),
            Transform::yRotation(glm::radians(45.0f))).toMatrix(),
        cubeMesh.get(), metalMaterialId, 0.6f, 1.0f));

    // Glowing emissive sphere on top of the first crate - demonstrates bloom effect
    // Sits 0.8m above crate (crate top at terrain+1.0, sphere center at terrain+1.0+0.3)
    // This object has physics AND is tracked as the emissive orb for light sync
    float glowSphereScale = 0.3f;
    addObject(makeObject(
        Transform(
            glm::vec3(crateX, getGroundY(crateX, crateZ, 1.0f + glowSphereScale), crateZ),
            glm::quat(1, 0, 0, 0),  // Identity rotation
            glowSphereScale).toMatrix(),
        sphereMesh.get(), metalMaterialId, 0.2f, 0.0f,
        25.0f, glm::vec3(1.0f, 0.9f, 0.7f), /*castsShadow*/ false),
        ObjectRole::EmissiveOrb);

    // Blue light indicator sphere - saturated blue, floating above terrain
    auto [blueLightX, blueLightZ] = worldPos(-3.0f, 2.0f);
    addObject(makeObject(
        Transform(
            glm::vec3(blueLightX, getGroundY(blueLightX, blueLightZ, 1.5f), blueLightZ),
            glm::quat(1, 0, 0, 0), 0.2f).toMatrix(),
        sphereMesh.get(), metalMaterialId, 0.2f, 0.0f,
        4.0f, glm::vec3(0.0f, 0.3f, 1.0f), /*castsShadow*/ false));

    // Green light indicator sphere - saturated green, floating above terrain
    auto [greenLightX, greenLightZ] = worldPos(4.0f, -2.0f);
    addObject(makeObject(
        Transform(
            glm::vec3(greenLightX, getGroundY(greenLightX, greenLightZ, 1.5f), greenLightZ),
            glm::quat(1, 0, 0, 0), 0.2f).toMatrix(),
        sphereMesh.get(), metalMaterialId, 0.2f, 0.0f,
        3.0f, glm::vec3(0.0f, 1.0f, 0.2f), /*castsShadow*/ false));

    // Debug cube - red emissive cube for testing (half-extent 0.5)
    auto [debugCubeX, debugCubeZ] = worldPos(5.0f, -5.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(debugCubeX, getGroundY(debugCubeX, debugCubeZ, 0.5f), debugCubeZ)),
        cubeMesh.get(), crateMaterialId, 0.3f, 0.0f,
        5.0f, glm::vec3(1.0f, 0.0f, 0.0f)));

    // Player character - uses animated character if loaded, otherwise capsule
    // Player position is controlled by physics, so we place at scene origin
    auto [playerX, playerZ] = worldPos(0.0f, 0.0f);
    float playerTerrainY = getTerrainHeight(playerX, playerZ);
    if (hasAnimatedCharacter) {
        // Use materials from FBX if available, otherwise use defaults
        float charRoughness = 0.5f;  // Default roughness for more specular highlights
        float charMetallic = 0.0f;
        glm::vec3 charEmissiveColor = glm::vec3(0.0f);
        float charEmissiveIntensity = 0.0f;

        const auto& materials = animatedCharacter->getMaterials();
        if (!materials.empty()) {
            // Use first material's properties (most characters have a primary material)
            const auto& mat = materials[0];
            charRoughness = mat.roughness;
            charMetallic = mat.metallic;
            charEmissiveColor = mat.emissiveColor;
            charEmissiveIntensity = mat.emissiveFactor;
            SDL_Log("SceneBuilder: Using FBX material '%s' - roughness=%.2f metallic=%.2f",
                    mat.name.c_str(), charRoughness, charMetallic);
        }

        addObject(makeObject(
            buildCharacterTransform(glm::vec3(playerX, playerTerrainY, playerZ), 10.0f),
            &animatedCharacter->getMesh(), whiteMaterialId, charRoughness, charMetallic,
            charEmissiveIntensity, charEmissiveColor, /*castsShadow*/ true,
            /*hueShift*/ 0.0f, /*gpuSkinned*/ true),
            ObjectRole::Player);
    } else {
        // Capsule fallback - capsule height 1.8m, center at 0.9m above ground
        addObject(makeObject(
            glm::translate(glm::mat4(1.0f), glm::vec3(playerX, playerTerrainY + 0.9f, playerZ)),
            capsuleMesh.get(), metalMaterialId, 0.3f, 0.8f),
            ObjectRole::Player);
    }

    // NPCs are rendered by NPCRenderer over the NPCSimulation simulation entities
    // (created in NPCSimulation::createNPCSimEntity). No scene-object render proxies
    // are created here.

    // Player weapons - attached to hand bones, transforms updated each frame
    if (hasAnimatedCharacter && rightHandBoneIndex >= 0) {
        addObject(makeObject(
            glm::mat4(1.0f),  // Updated per frame
            swordMesh.get(), metalMaterialId, 0.2f, 0.95f),
            ObjectRole::Sword);
    }
    if (hasAnimatedCharacter && leftHandBoneIndex >= 0) {
        addObject(makeObject(
            glm::mat4(1.0f),  // Updated per frame
            shieldMesh.get(), metalMaterialId, 0.3f, 0.9f),
            ObjectRole::Shield);
    }

    // Debug axis indicators for right hand (R=X, G=Y, B=Z)
    if (hasAnimatedCharacter && rightHandBoneIndex >= 0) {
        // X axis - Red
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(1.0f, 0.0f, 0.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisRightX);
        // Y axis - Green
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(0.0f, 1.0f, 0.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisRightY);
        // Z axis - Blue
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(0.0f, 0.0f, 1.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisRightZ);
        SDL_Log("SceneBuilder: Added debug axis indicators for right hand");
    }

    // Debug axis indicators for left hand (R=X, G=Y, B=Z)
    if (hasAnimatedCharacter && leftHandBoneIndex >= 0) {
        // X axis - Red
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(1.0f, 0.0f, 0.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisLeftX);
        // Y axis - Green
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(0.0f, 1.0f, 0.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisLeftY);
        // Z axis - Blue
        addObject(makeObject(glm::mat4(1.0f), axisLineMesh.get(), whiteMaterialId,
            1.0f, 0.0f, 5.0f, glm::vec3(0.0f, 0.0f, 1.0f), /*castsShadow*/ false),
            ObjectRole::DebugAxisLeftZ);
        SDL_Log("SceneBuilder: Added debug axis indicators for left hand");
    }

    // Flag pole - 3m pole, center at 1.5m above ground
    auto [flagPoleX, flagPoleZ] = worldPos(5.0f, 0.0f);
    addObject(makeObject(
        glm::translate(glm::mat4(1.0f), glm::vec3(flagPoleX, getGroundY(flagPoleX, flagPoleZ, 1.5f), flagPoleZ)),
        flagPoleMesh.get(), metalMaterialId, 0.4f, 0.9f),
        ObjectRole::FlagPole);

    // Flag cloth - will be positioned and updated by ClothSimulation
    addObject(makeObject(
        glm::mat4(1.0f),  // Identity, will be handled differently
        &flagClothMesh, crateMaterialId, 0.6f, 0.0f),
        ObjectRole::FlagCloth);

    // Player cape - attached to character, updated each frame (using metal texture)
    if (hasCapeEnabled) {
        addObject(makeObject(
            glm::mat4(1.0f),  // Identity, cloth positions are in world space
            &capeMesh, capeMaterialId, 0.3f, 0.8f),
            ObjectRole::Cape);
    }

    // Well entrance - demonstrates terrain hole mask system
    // A stone-like frame floating above the terrain hole
    // wellEntranceX/Z are pre-calculated in initInternal() for terrain hole creation
    float wellY = getTerrainHeight(wellEntranceX, wellEntranceZ);
    // Frame floats 3m above terrain so hole is visible
    glm::mat4 wellTransform = glm::translate(glm::mat4(1.0f),
        glm::vec3(wellEntranceX, wellY + 3.0f, wellEntranceZ));
    wellTransform = glm::scale(wellTransform, glm::vec3(2.0f, 0.5f, 12.0f));
    addObject(makeObject(
        wellTransform,
        cubeMesh.get(), metalMaterialId, 0.8f, 0.1f),
        ObjectRole::WellEntrance);

    // Slice 5: finalize merged entity creation - establish player hierarchy
    // (weapons/cape/debug axes) and tag NPC entities now that all entities exist.
    if (ecsWorld_) {
        finalizeSceneEntities();
    }
}

void SceneBuilder::uploadFlagClothMesh(VmaAllocator allocator, VkDevice device, VkCommandPool commandPool, VkQueue queue) {
    flagClothMesh.releaseGPUResources();
    flagClothMesh.upload(allocator, device, commandPool, queue);
}

glm::mat4 SceneBuilder::buildCharacterTransform(const glm::vec3& position, float yRotation) const {
    // Character model transform:
    // 1. Translate to world position
    // 2. Apply Y rotation (facing direction)
    // Note: Scale is now handled by FBX post-import processing
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    transform = glm::rotate(transform, yRotation, glm::vec3(0.0f, 1.0f, 0.0f));
    return transform;
}

void SceneBuilder::setShowSword(bool show) {
    if (ecsWorld_ && swordEntity_ != ecs::NullEntity && ecsWorld_->has<ecs::WeaponTag>(swordEntity_)) {
        ecsWorld_->get<ecs::WeaponTag>(swordEntity_).visible = show;
    }
}

void SceneBuilder::setShowShield(bool show) {
    if (ecsWorld_ && shieldEntity_ != ecs::NullEntity && ecsWorld_->has<ecs::WeaponTag>(shieldEntity_)) {
        ecsWorld_->get<ecs::WeaponTag>(shieldEntity_).visible = show;
    }
}

void SceneBuilder::setShowWeaponAxes(bool show) {
    if (!ecsWorld_) return;
    for (auto [entity, axis] : ecsWorld_->view<ecs::DebugAxisTag>().each()) {
        axis.visible = show;
    }
}

void SceneBuilder::updatePlayerTransform(const glm::mat4& transform) {
    // ecs::Transform is the sole authority (skinned colour + shadow passes and GUI read it).
    if (ecsWorld_ && ecsWorld_->valid(playerEntity_) && ecsWorld_->has<ecs::Transform>(playerEntity_)) {
        ecsWorld_->get<ecs::Transform>(playerEntity_).matrix = transform;
    }
}

void SceneBuilder::updateAnimatedCharacter(float deltaTime, VmaAllocator allocator, VkDevice device,
                                            VkCommandPool commandPool, VkQueue queue,
                                            float movementSpeed, bool isGrounded, bool isJumping,
                                            const glm::vec3& position, const glm::vec3& facing,
                                            const glm::vec3& inputDirection,
                                            bool strafeMode, const glm::vec3& cameraDirection) {
    if (!hasAnimatedCharacter) return;

    // Get the character's current world transform for IK ground queries (ECS authoritative).
    glm::mat4 worldTransform = glm::mat4(1.0f);
    if (ecsWorld_ && ecsWorld_->valid(playerEntity_) && ecsWorld_->has<ecs::Transform>(playerEntity_)) {
        worldTransform = ecsWorld_->get<ecs::Transform>(playerEntity_).matrix;
    }

    // Update motion matching if enabled (must be called before update())
    if (animatedCharacter->isUsingMotionMatching()) {
        auto& controller = animatedCharacter->getMotionMatchingController();

        // Derive airborne state from position vs terrain height
        // If character's Y is significantly above terrain, they're airborne
        constexpr float AIRBORNE_THRESHOLD = 0.5f;  // 50cm above terrain = airborne
        float terrainY = getTerrainHeight(position.x, position.z);
        bool isAirborne = (position.y - terrainY) > AIRBORNE_THRESHOLD;

        // When airborne, require jump animations; otherwise exclude them
        if (isAirborne) {
            controller.setExcludedTags({});  // Don't exclude jump
            controller.setRequiredTags({"jump"});  // Require jump animations
        } else {
            controller.setExcludedTags({"jump"});  // Exclude jump during normal locomotion
            controller.setRequiredTags({});
        }

        // Configure strafe mode (Unreal-style orientation lock)
        controller.setStrafeMode(strafeMode);
        if (strafeMode && glm::length(cameraDirection) > 0.001f) {
            // In strafe mode, the character should face the camera direction
            glm::vec3 normalizedCamDir = glm::normalize(cameraDirection);
            normalizedCamDir.y = 0.0f;  // Keep horizontal only
            if (glm::length(normalizedCamDir) > 0.001f) {
                controller.setDesiredFacing(glm::normalize(normalizedCamDir));
            }
        }

        // Get actual speed from input direction
        float actualSpeed = glm::length(inputDirection);
        // Normalize direction to unit vector
        glm::vec3 normalizedInput = actualSpeed > 0.001f ? inputDirection / actualSpeed : glm::vec3(0.0f);
        // Normalize magnitude to 0-1 range (maxSpeed = 6.0 m/s in TrajectoryPredictor)
        constexpr float MAX_LOCOMOTION_SPEED = 6.0f;
        float inputMagnitude = std::min(actualSpeed / MAX_LOCOMOTION_SPEED, 1.0f);
        animatedCharacter->updateMotionMatching(position, facing, normalizedInput, inputMagnitude, deltaTime);
    }

    animatedCharacter->update(deltaTime, allocator, device, commandPool, queue,
                                  movementSpeed, isGrounded, isJumping, worldTransform);
    // The player is drawn by SkinnedCharDrawable straight from animatedCharacter->getMesh(),
    // not via its MeshRef, so no per-frame mesh-pointer sync is needed.

    // Update player cape if enabled
    if (hasCapeEnabled) {
        // Update cape simulation with current skeleton pose
        playerCape.update(animatedCharacter->getSkeleton(), worldTransform, deltaTime, nullptr);

        // Update cape mesh and re-upload. The cape entity's MeshRef points at the stable
        // capeMesh member, so re-uploading its GPU resources needs no MeshRef update.
        playerCape.updateMesh(capeMesh);
        capeMesh.releaseGPUResources();
        capeMesh.upload(allocator, device, commandPool, queue);
    }

    // Update weapon transforms using the same worldTransform as the cape
    updateWeaponTransforms(worldTransform);
}

void SceneBuilder::startCharacterJump(const glm::vec3& startPos, const glm::vec3& velocity, float gravity, const PhysicsWorld* physics) {
    if (!hasAnimatedCharacter) return;
    animatedCharacter->startJump(startPos, velocity, gravity, physics);
}

void SceneBuilder::updateNPCs(float deltaTime, const glm::vec3& cameraPos) {
    if (!npcSimulation_) return;

    // ECS is the single source of truth for per-NPC state, consumed by NPCRenderer
    // directly from the NPCSimulation simulation entities.
    npcSimulation_->updateECS(deltaTime, cameraPos);
}

size_t SceneBuilder::getNPCCount() const {
    return npcSimulation_ ? npcSimulation_->getNPCCount() : 0;
}

bool SceneBuilder::hasNPCs() const {
    return npcSimulation_ && npcSimulation_->hasNPCs();
}


void SceneBuilder::updateWeaponTransforms(const glm::mat4& worldTransform) {
    if (!hasAnimatedCharacter || !ecsWorld_) return;

    // Compute global bone transforms from the freshly-animated skeleton
    const auto& skeleton = animatedCharacter->getSkeleton();
    std::vector<glm::mat4> globalTransforms;
    skeleton.computeGlobalTransforms(globalTransforms);

    glm::mat4 hideTransform = glm::scale(glm::mat4(1.0f), glm::vec3(0.0f));

    // Position a bone-attached entity via its authoritative ECS Transform.
    auto updateAttached = [&](ecs::Entity entity, int boneIndex, const glm::mat4& offset, bool visible) {
        if (entity == ecs::NullEntity) return;
        if (!ecsWorld_->valid(entity) || !ecsWorld_->has<ecs::Transform>(entity)) return;
        glm::mat4 transform = hideTransform;
        if (visible && boneIndex >= 0 && static_cast<size_t>(boneIndex) < globalTransforms.size()) {
            transform = worldTransform * globalTransforms[boneIndex] * offset;
        }
        ecsWorld_->get<ecs::Transform>(entity).matrix = transform;
    };

    // Sword
    if (swordEntity_ != ecs::NullEntity) {
        bool visible = ecsWorld_->has<ecs::WeaponTag>(swordEntity_) &&
                       ecsWorld_->get<ecs::WeaponTag>(swordEntity_).visible;
        updateAttached(swordEntity_, rightHandBoneIndex, getSwordOffset(), visible);
    }

    // Shield
    if (shieldEntity_ != ecs::NullEntity) {
        bool visible = ecsWorld_->has<ecs::WeaponTag>(shieldEntity_) &&
                       ecsWorld_->get<ecs::WeaponTag>(shieldEntity_).visible;
        updateAttached(shieldEntity_, leftHandBoneIndex, getShieldOffset(), visible);
    }

    // Debug axes - right hand
    for (int a = 0; a < 3; ++a) {
        ecs::Entity e = rightHandAxisEntities_[a];
        if (e == ecs::NullEntity) continue;
        bool visible = ecsWorld_->has<ecs::DebugAxisTag>(e) &&
                       ecsWorld_->get<ecs::DebugAxisTag>(e).visible;
        glm::mat4 off = (ecsWorld_->has<ecs::BoneAttachment>(e))
            ? ecsWorld_->get<ecs::BoneAttachment>(e).localOffset : glm::mat4(1.0f);
        updateAttached(e, rightHandBoneIndex, off, visible);
    }

    // Debug axes - left hand
    for (int a = 0; a < 3; ++a) {
        ecs::Entity e = leftHandAxisEntities_[a];
        if (e == ecs::NullEntity) continue;
        bool visible = ecsWorld_->has<ecs::DebugAxisTag>(e) &&
                       ecsWorld_->get<ecs::DebugAxisTag>(e).visible;
        glm::mat4 off = (ecsWorld_->has<ecs::BoneAttachment>(e))
            ? ecsWorld_->get<ecs::BoneAttachment>(e).localOffset : glm::mat4(1.0f);
        updateAttached(e, leftHandBoneIndex, off, visible);
    }
}

// Texture accessors (return raw pointer from shared_ptr)
const Texture* SceneBuilder::getGroundTexture() const {
    return groundTexture_.get();
}

const Texture* SceneBuilder::getGroundNormalMap() const {
    return groundNormal_.get();
}

const Texture* SceneBuilder::getCrateTexture() const {
    return crateTexture_.get();
}

const Texture* SceneBuilder::getCrateNormalMap() const {
    return crateNormal_.get();
}

const Texture* SceneBuilder::getMetalTexture() const {
    return metalTexture_.get();
}

const Texture* SceneBuilder::getMetalNormalMap() const {
    return metalNormal_.get();
}

const Texture* SceneBuilder::getDefaultEmissiveMap() const {
    return defaultEmissive_.get();
}

const Texture* SceneBuilder::getWhiteTexture() const {
    return whiteTexture_.get();
}
