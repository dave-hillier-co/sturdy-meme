#include "ScatterSystem.h"
#include "ecs/Components.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<ScatterSystem> ScatterSystem::create(
    const InitInfo& info,
    const Config& config,
    std::vector<Mesh>&& meshes,
    std::vector<SceneObjectInstance>&& instances,
    std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier)
{
    auto system = std::make_unique<ScatterSystem>(ConstructToken{});
    if (!system->initInternal(info, config, std::move(meshes), std::move(instances), transformModifier)) {
        return nullptr;
    }
    return system;
}

ScatterSystem::~ScatterSystem() {
    material_.cleanup();
}

bool ScatterSystem::initInternal(
    const InitInfo& info,
    const Config& config,
    std::vector<Mesh>&& meshes,
    std::vector<SceneObjectInstance>&& instances,
    std::function<glm::mat4(const SceneObjectInstance&, const glm::mat4&)> transformModifier)
{
    name_ = config.name;

    // Initialize the material with Vulkan context
    SceneMaterial::InitInfo materialInfo;
    materialInfo.device = info.device;
    materialInfo.allocator = info.allocator;
    materialInfo.commandPool = info.commandPool;
    materialInfo.graphicsQueue = info.graphicsQueue;
    materialInfo.physicalDevice = info.physicalDevice;
    materialInfo.resourcePath = info.resourcePath;
    materialInfo.getTerrainHeight = info.getTerrainHeight;
    materialInfo.terrainSize = info.terrainSize;

    SceneMaterial::MaterialProperties matProps;
    matProps.roughness = config.materialRoughness;
    matProps.metallic = config.materialMetallic;
    matProps.castsShadow = config.castsShadow;

    material_.init(materialInfo, matProps);

    if (!loadTextures(info, config)) {
        SDL_Log("ScatterSystem[%s]: Failed to load textures", name_.c_str());
        return false;
    }

    // Set meshes and instances
    material_.setMeshes(std::move(meshes));
    material_.setInstances(std::move(instances));

    // Build scene objects with optional transform modifier
    material_.rebuildSceneObjects(transformModifier);

    SDL_Log("ScatterSystem[%s]: Initialized with %zu instances (%zu mesh variations)",
            name_.c_str(), material_.getInstanceCount(), material_.getMeshVariationCount());

    return true;
}

bool ScatterSystem::loadTextures(const InitInfo& info, const Config& config) {
    std::string diffusePath = info.resourcePath + "/" + config.diffuseTexturePath;
    auto diffuseTexture = Texture::loadFromFile(diffusePath, info.allocator, info.device,
                                                 info.commandPool, info.graphicsQueue, info.physicalDevice);
    if (!diffuseTexture) {
        SDL_Log("ScatterSystem[%s]: Failed to load diffuse texture: %s", name_.c_str(), diffusePath.c_str());
        return false;
    }
    material_.setDiffuseTexture(std::move(diffuseTexture));

    std::string normalPath = info.resourcePath + "/" + config.normalTexturePath;
    auto normalTexture = Texture::loadFromFile(normalPath, info.allocator, info.device,
                                                info.commandPool, info.graphicsQueue, info.physicalDevice, false);
    if (!normalTexture) {
        SDL_Log("ScatterSystem[%s]: Failed to load normal texture: %s", name_.c_str(), normalPath.c_str());
        return false;
    }
    material_.setNormalTexture(std::move(normalTexture));

    return true;
}

bool ScatterSystem::createDescriptorSets(
    VkDevice device,
    DescriptorManager::Pool& pool,
    VkDescriptorSetLayout layout,
    uint32_t frameCount,
    std::function<MaterialDescriptorFactory::CommonBindings(uint32_t)> getCommonBindings)
{
    descriptorSets_ = pool.allocate(layout, frameCount);
    if (descriptorSets_.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "ScatterSystem[%s]: Failed to allocate descriptor sets", name_.c_str());
        return false;
    }

    MaterialDescriptorFactory factory(device);
    for (uint32_t i = 0; i < frameCount; i++) {
        MaterialDescriptorFactory::CommonBindings common = getCommonBindings(i);

        MaterialDescriptorFactory::MaterialTextures mat{};
        mat.diffuseView = getDiffuseTexture().getImageView();
        mat.diffuseSampler = getDiffuseTexture().getSampler();
        mat.normalView = getNormalTexture().getImageView();
        mat.normalSampler = getNormalTexture().getSampler();

        factory.writeDescriptorSet(descriptorSets_[i], common, mat);
    }

    SDL_Log("ScatterSystem[%s]: Created %u descriptor sets", name_.c_str(), frameCount);
    return true;
}

size_t ScatterSystem::createInstanceEntities(ecs::World& world, bool isRock) {
    if (areaEntity_ == ecs::NullEntity) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "ScatterSystem[%s]: Cannot create instance entities without area entity", name_.c_str());
        return 0;
    }

    const auto& instances = material_.getInstances();
    const auto& meshes = material_.getMeshes();

    instanceEntities_.clear();
    instanceEntities_.reserve(instances.size());

    for (const auto& instance : instances) {
        ecs::Entity entity = world.create();

        // Transform from the instance's computed matrix
        world.add<ecs::Transform>(entity, instance.getTransformMatrix());

        // Mesh reference (pointer to the mesh for this variation)
        if (instance.meshVariation < meshes.size()) {
            world.add<ecs::MeshRef>(entity, const_cast<Mesh*>(&meshes[instance.meshVariation]));
        }

        // Mesh variation index
        world.add<ecs::MeshVariation>(entity, instance.meshVariation);

        // Shadow casting
        world.add<ecs::CastsShadow>(entity);

        // Tag component
        if (isRock) {
            world.add<ecs::RockTag>(entity);
        } else {
            world.add<ecs::DetritusTag>(entity);
        }

        // Bounding sphere from mesh and scale
        float boundRadius = instance.scale();
        world.add<ecs::BoundingSphere>(entity, instance.position(), boundRadius);

        // Parent-child relationship
        world.add<ecs::Parent>(entity, areaEntity_);
        world.add<ecs::HierarchyDepth>(entity, uint16_t(1));

        if (world.has<ecs::Children>(areaEntity_)) {
            world.get<ecs::Children>(areaEntity_).add(entity);
        }

        instanceEntities_.push_back(entity);
    }

    SDL_Log("ScatterSystem[%s]: Created %zu instance entities (parent: area entity)",
            name_.c_str(), instanceEntities_.size());

    // Clear internal instances - ECS is now the source of truth
    material_.clearInstances();

    return instanceEntities_.size();
}

void ScatterSystem::rebuildFromECS(ecs::World& world) {
    auto& sceneObjects = material_.getSceneObjects();
    sceneObjects.clear();
    sceneObjects.reserve(instanceEntities_.size());

    const auto& matProps = material_.getMaterialProperties();

    for (ecs::Entity entity : instanceEntities_) {
        if (!world.valid(entity)) continue;
        if (!world.has<ecs::Transform>(entity) || !world.has<ecs::MeshRef>(entity)) continue;

        const auto& transform = world.get<ecs::Transform>(entity);
        const auto& meshRef = world.get<ecs::MeshRef>(entity);
        if (!meshRef.mesh) continue;

        sceneObjects.push_back(RenderableBuilder()
            .withTransform(transform.matrix)
            .withMesh(meshRef.mesh)
            .withTexture(material_.getDiffuseTexture())
            .withRoughness(matProps.roughness)
            .withMetallic(matProps.metallic)
            .withCastsShadow(matProps.castsShadow)
            .build());
    }

    SDL_Log("ScatterSystem[%s]: Rebuilt %zu renderables from ECS entities",
            name_.c_str(), sceneObjects.size());
}
