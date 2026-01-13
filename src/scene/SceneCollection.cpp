#include "SceneCollection.h"
#include <SDL3/SDL_log.h>

void SceneCollection::init(const InitInfo& info) {
    initInfo_ = info;
    initialized_ = true;
}

SceneMaterial& SceneCollection::createMaterial(const std::string& name,
                                                const MaterialProperties& props) {
    if (materials_.find(name) != materials_.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneCollection: Material '%s' already exists, returning existing", name.c_str());
        return *materials_[name];
    }

    auto material = std::make_unique<SceneMaterial>();
    material->init(initInfo_, props);

    SceneMaterial* ptr = material.get();
    materials_[name] = std::move(material);
    materialOrder_.push_back(name);

    SDL_Log("SceneCollection: Created material '%s'", name.c_str());
    return *ptr;
}

void SceneCollection::registerMaterial(const std::string& name, SceneMaterial& material) {
    if (hasMaterial(name)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneCollection: Material '%s' already exists, skipping registration", name.c_str());
        return;
    }

    registeredMaterials_[name] = &material;
    materialOrder_.push_back(name);

    SDL_Log("SceneCollection: Registered external material '%s'", name.c_str());
}

void SceneCollection::registerRenderables(const std::string& name, std::vector<Renderable>& renderables) {
    if (hasMaterial(name) || registeredRenderables_.find(name) != registeredRenderables_.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "SceneCollection: '%s' already exists, skipping renderables registration", name.c_str());
        return;
    }

    registeredRenderables_[name] = &renderables;
    materialOrder_.push_back(name);

    SDL_Log("SceneCollection: Registered external renderables '%s' (%zu objects)",
            name.c_str(), renderables.size());
}

SceneMaterial* SceneCollection::getMaterial(const std::string& name) {
    // Check owned materials first
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return it->second.get();
    }

    // Check registered materials
    auto regIt = registeredMaterials_.find(name);
    return regIt != registeredMaterials_.end() ? regIt->second : nullptr;
}

const SceneMaterial* SceneCollection::getMaterial(const std::string& name) const {
    // Check owned materials first
    auto it = materials_.find(name);
    if (it != materials_.end()) {
        return it->second.get();
    }

    // Check registered materials
    auto regIt = registeredMaterials_.find(name);
    return regIt != registeredMaterials_.end() ? regIt->second : nullptr;
}

bool SceneCollection::hasMaterial(const std::string& name) const {
    return materials_.find(name) != materials_.end() ||
           registeredMaterials_.find(name) != registeredMaterials_.end();
}

std::vector<Renderable> SceneCollection::collectAllSceneObjects() const {
    std::vector<Renderable> all;

    // Reserve space (owned materials, registered materials, and registered renderables)
    size_t total = 0;
    for (const auto& [name, material] : materials_) {
        total += material->getSceneObjects().size();
    }
    for (const auto& [name, material] : registeredMaterials_) {
        total += material->getSceneObjects().size();
    }
    for (const auto& [name, renderables] : registeredRenderables_) {
        total += renderables->size();
    }
    all.reserve(total);

    // Collect in registration order for deterministic behavior
    for (const auto& name : materialOrder_) {
        // Check SceneMaterial first
        const SceneMaterial* material = getMaterial(name);
        if (material) {
            const auto& objects = material->getSceneObjects();
            all.insert(all.end(), objects.begin(), objects.end());
            continue;
        }

        // Check registered renderables
        auto rendIt = registeredRenderables_.find(name);
        if (rendIt != registeredRenderables_.end()) {
            all.insert(all.end(), rendIt->second->begin(), rendIt->second->end());
        }
    }

    return all;
}

void SceneCollection::setDescriptorSets(const std::string& name, std::vector<VkDescriptorSet>&& sets) {
    descriptorSets_[name] = std::move(sets);
}

VkDescriptorSet SceneCollection::getDescriptorSet(const std::string& name, uint32_t frameIndex) const {
    auto it = descriptorSets_.find(name);
    if (it == descriptorSets_.end() || frameIndex >= it->second.size()) {
        return VK_NULL_HANDLE;
    }
    return it->second[frameIndex];
}

std::vector<VkDescriptorSet>& SceneCollection::getDescriptorSets(const std::string& name) {
    return descriptorSets_[name];
}

const std::vector<VkDescriptorSet>& SceneCollection::getDescriptorSets(const std::string& name) const {
    static const std::vector<VkDescriptorSet> empty;
    auto it = descriptorSets_.find(name);
    return it != descriptorSets_.end() ? it->second : empty;
}

bool SceneCollection::hasDescriptorSets(const std::string& name) const {
    auto it = descriptorSets_.find(name);
    return it != descriptorSets_.end() && !it->second.empty();
}

size_t SceneCollection::getTotalInstanceCount() const {
    size_t total = 0;
    for (const auto& [name, material] : materials_) {
        total += material->getInstanceCount();
    }
    for (const auto& [name, material] : registeredMaterials_) {
        total += material->getInstanceCount();
    }
    return total;
}

void SceneCollection::cleanup() {
    // Only cleanup owned materials (registered ones are owned elsewhere)
    for (auto& [name, material] : materials_) {
        material->cleanup();
    }
    materials_.clear();
    registeredMaterials_.clear();
    registeredRenderables_.clear();
    materialOrder_.clear();
    descriptorSets_.clear();
    initialized_ = false;
}
