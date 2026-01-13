#include "AssetRegistry.h"
#include <SDL_log.h>
#include <algorithm>

AssetRegistry::~AssetRegistry() {
    cleanup();
}

void AssetRegistry::init(VkDevice device, VkPhysicalDevice physicalDevice,
                         VmaAllocator allocator, VkCommandPool commandPool,
                         VkQueue queue) {
    std::lock_guard<std::mutex> lock(mutex_);

    device_ = device;
    physicalDevice_ = physicalDevice;
    allocator_ = allocator;
    commandPool_ = commandPool;
    queue_ = queue;

    SDL_Log("AssetRegistry initialized");
}

void AssetRegistry::cleanup() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Destroy shaders first (they only need device)
    for (auto& entry : shaders_) {
        if (entry.module) {
            vkDestroyShaderModule(device_, entry.module, nullptr);
            entry.module = VK_NULL_HANDLE;
        }
    }
    shaders_.clear();
    shaderPathCache_.clear();
    shaderFreelist_.clear();

    // Textures and meshes clean up via their destructors
    textures_.clear();
    texturePathCache_.clear();
    textureFreelist_.clear();

    meshes_.clear();
    meshNameCache_.clear();
    meshFreelist_.clear();

    SDL_Log("AssetRegistry cleaned up");
}

// ============================================================================
// Texture Management
// ============================================================================

asset::TextureHandle AssetRegistry::loadTexture(const std::string& path,
                                                const TextureLoadConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache first
    auto it = texturePathCache_.find(path);
    if (it != texturePathCache_.end()) {
        // Verify the handle is still valid
        if (isValidTextureHandle(it->second)) {
            textures_[it->second.index].refCount++;
            textureCacheHits_++;
            return it->second;
        } else {
            // Stale cache entry, remove it
            texturePathCache_.erase(it);
        }
    }

    // Load the texture
    std::unique_ptr<Texture> texture;
    if (config.generateMipmaps) {
        texture = Texture::loadFromFileWithMipmaps(
            path, allocator_, device_, commandPool_, queue_,
            physicalDevice_, config.useSRGB, config.enableAnisotropy);
    } else {
        texture = Texture::loadFromFile(
            path, allocator_, device_, commandPool_, queue_,
            physicalDevice_, config.useSRGB);
    }

    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to load texture: %s", path.c_str());
        return asset::INVALID_TEXTURE_HANDLE;
    }

    // Allocate slot
    uint32_t index = allocateSlot(textures_, textureFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Texture> entry;
    entry.asset = std::move(texture);
    entry.name = path;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < textures_.size()) {
        textures_[index] = std::move(entry);
    } else {
        textures_.push_back(std::move(entry));
    }

    asset::TextureHandle handle(index, gen);
    texturePathCache_[path] = handle;

    SDL_Log("AssetRegistry: Loaded texture '%s' (handle: %u)", path.c_str(), index);
    return handle;
}

asset::TextureHandle AssetRegistry::createSolidColorTexture(
    uint8_t r, uint8_t g, uint8_t b, uint8_t a, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto texture = Texture::createSolidColor(
        r, g, b, a, allocator_, device_, commandPool_, queue_);

    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to create solid color texture");
        return asset::INVALID_TEXTURE_HANDLE;
    }

    uint32_t index = allocateSlot(textures_, textureFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Texture> entry;
    entry.asset = std::move(texture);
    entry.name = name.empty() ?
        ("solid_" + std::to_string(r) + "_" + std::to_string(g) + "_" +
         std::to_string(b) + "_" + std::to_string(a)) : name;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < textures_.size()) {
        textures_[index] = std::move(entry);
    } else {
        textures_.push_back(std::move(entry));
    }

    asset::TextureHandle handle(index, gen);
    if (!name.empty()) {
        texturePathCache_[name] = handle;
    }

    return handle;
}

asset::TextureHandle AssetRegistry::registerTexture(
    std::unique_ptr<Texture> texture, const std::string& name) {
    if (!texture) {
        return asset::INVALID_TEXTURE_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t index = allocateSlot(textures_, textureFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Texture> entry;
    entry.asset = std::move(texture);
    entry.name = name;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < textures_.size()) {
        textures_[index] = std::move(entry);
    } else {
        textures_.push_back(std::move(entry));
    }

    asset::TextureHandle handle(index, gen);
    if (!name.empty()) {
        texturePathCache_[name] = handle;
    }

    return handle;
}

const Texture* AssetRegistry::getTexture(asset::TextureHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidTextureHandle(handle)) {
        return nullptr;
    }

    return textures_[handle.index].asset.get();
}

asset::TextureHandle AssetRegistry::getTextureHandle(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = texturePathCache_.find(path);
    if (it != texturePathCache_.end() && isValidTextureHandle(it->second)) {
        return it->second;
    }
    return asset::INVALID_TEXTURE_HANDLE;
}

void AssetRegistry::addTextureRef(asset::TextureHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (isValidTextureHandle(handle)) {
        textures_[handle.index].refCount++;
    }
}

void AssetRegistry::releaseTexture(asset::TextureHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidTextureHandle(handle)) {
        return;
    }

    auto& entry = textures_[handle.index];
    if (entry.refCount > 0) {
        entry.refCount--;
    }
}

// ============================================================================
// Mesh Management
// ============================================================================

asset::MeshHandle AssetRegistry::createMesh(const MeshConfig& config,
                                             const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache if name provided
    if (!name.empty()) {
        auto it = meshNameCache_.find(name);
        if (it != meshNameCache_.end() && isValidMeshHandle(it->second)) {
            meshes_[it->second.index].refCount++;
            return it->second;
        }
    }

    auto mesh = std::make_unique<Mesh>();

    switch (config.type) {
        case MeshConfig::Type::Cube:
            mesh->createCube();
            break;
        case MeshConfig::Type::Plane:
            mesh->createPlane(config.width, config.depth);
            break;
        case MeshConfig::Type::Sphere:
            mesh->createSphere(config.radius, config.stacks, config.slices);
            break;
        case MeshConfig::Type::Cylinder:
            mesh->createCylinder(config.radius, config.height, config.segments);
            break;
        case MeshConfig::Type::Capsule:
            mesh->createCapsule(config.radius, config.height, config.stacks, config.slices);
            break;
        case MeshConfig::Type::Disc:
            mesh->createDisc(config.radius, config.segments, config.uvScale);
            break;
        case MeshConfig::Type::Rock:
            mesh->createRock(config.radius, config.subdivisions, config.seed,
                            config.roughness, config.asymmetry);
            break;
        case MeshConfig::Type::Custom:
            // Empty mesh for custom - caller should use createCustomMesh instead
            break;
    }

    if (!mesh->upload(allocator_, device_, commandPool_, queue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to upload mesh");
        return asset::INVALID_MESH_HANDLE;
    }

    uint32_t index = allocateSlot(meshes_, meshFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Mesh> entry;
    entry.asset = std::move(mesh);
    entry.name = name;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < meshes_.size()) {
        meshes_[index] = std::move(entry);
    } else {
        meshes_.push_back(std::move(entry));
    }

    asset::MeshHandle handle(index, gen);
    if (!name.empty()) {
        meshNameCache_[name] = handle;
    }

    SDL_Log("AssetRegistry: Created mesh '%s' (handle: %u)",
            name.empty() ? "unnamed" : name.c_str(), index);
    return handle;
}

asset::MeshHandle AssetRegistry::createCustomMesh(
    const std::vector<Vertex>& vertices,
    const std::vector<uint32_t>& indices,
    const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto mesh = std::make_unique<Mesh>();
    mesh->setCustomGeometry(vertices, indices);

    if (!mesh->upload(allocator_, device_, commandPool_, queue_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to upload custom mesh");
        return asset::INVALID_MESH_HANDLE;
    }

    uint32_t index = allocateSlot(meshes_, meshFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Mesh> entry;
    entry.asset = std::move(mesh);
    entry.name = name;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < meshes_.size()) {
        meshes_[index] = std::move(entry);
    } else {
        meshes_.push_back(std::move(entry));
    }

    asset::MeshHandle handle(index, gen);
    if (!name.empty()) {
        meshNameCache_[name] = handle;
    }

    return handle;
}

asset::MeshHandle AssetRegistry::registerMesh(std::unique_ptr<Mesh> mesh,
                                               const std::string& name) {
    if (!mesh) {
        return asset::INVALID_MESH_HANDLE;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    uint32_t index = allocateSlot(meshes_, meshFreelist_);
    uint32_t gen = nextGeneration_++;

    AssetEntry<Mesh> entry;
    entry.asset = std::move(mesh);
    entry.name = name;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < meshes_.size()) {
        meshes_[index] = std::move(entry);
    } else {
        meshes_.push_back(std::move(entry));
    }

    asset::MeshHandle handle(index, gen);
    if (!name.empty()) {
        meshNameCache_[name] = handle;
    }

    return handle;
}

const Mesh* AssetRegistry::getMesh(asset::MeshHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidMeshHandle(handle)) {
        return nullptr;
    }

    return meshes_[handle.index].asset.get();
}

Mesh* AssetRegistry::getMeshMutable(asset::MeshHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidMeshHandle(handle)) {
        return nullptr;
    }

    return meshes_[handle.index].asset.get();
}

asset::MeshHandle AssetRegistry::getMeshHandle(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = meshNameCache_.find(name);
    if (it != meshNameCache_.end() && isValidMeshHandle(it->second)) {
        return it->second;
    }
    return asset::INVALID_MESH_HANDLE;
}

void AssetRegistry::addMeshRef(asset::MeshHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (isValidMeshHandle(handle)) {
        meshes_[handle.index].refCount++;
    }
}

void AssetRegistry::releaseMesh(asset::MeshHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidMeshHandle(handle)) {
        return;
    }

    auto& entry = meshes_[handle.index];
    if (entry.refCount > 0) {
        entry.refCount--;
    }
}

// ============================================================================
// Shader Management
// ============================================================================

asset::ShaderHandle AssetRegistry::loadShader(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check cache first
    auto it = shaderPathCache_.find(path);
    if (it != shaderPathCache_.end()) {
        if (isValidShaderHandle(it->second)) {
            shaders_[it->second.index].refCount++;
            shaderCacheHits_++;
            return it->second;
        } else {
            shaderPathCache_.erase(it);
        }
    }

    // Load shader
    auto module = ShaderLoader::loadShaderModule(device_, path);
    if (!module) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "AssetRegistry: Failed to load shader: %s", path.c_str());
        return asset::INVALID_SHADER_HANDLE;
    }

    uint32_t index = allocateShaderSlot();
    uint32_t gen = nextGeneration_++;

    ShaderEntry entry;
    entry.module = *module;
    entry.path = path;
    entry.refCount = 1;
    entry.generation = gen;

    if (index < shaders_.size()) {
        shaders_[index] = std::move(entry);
    } else {
        shaders_.push_back(std::move(entry));
    }

    asset::ShaderHandle handle(index, gen);
    shaderPathCache_[path] = handle;

    SDL_Log("AssetRegistry: Loaded shader '%s' (handle: %u)", path.c_str(), index);
    return handle;
}

vk::ShaderModule AssetRegistry::getShader(asset::ShaderHandle handle) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidShaderHandle(handle)) {
        return VK_NULL_HANDLE;
    }

    return shaders_[handle.index].module;
}

asset::ShaderHandle AssetRegistry::getShaderHandle(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = shaderPathCache_.find(path);
    if (it != shaderPathCache_.end() && isValidShaderHandle(it->second)) {
        return it->second;
    }
    return asset::INVALID_SHADER_HANDLE;
}

void AssetRegistry::releaseShader(asset::ShaderHandle handle) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!isValidShaderHandle(handle)) {
        return;
    }

    auto& entry = shaders_[handle.index];
    if (entry.refCount > 0) {
        entry.refCount--;
    }
}

// ============================================================================
// Statistics and Garbage Collection
// ============================================================================

AssetRegistry::Stats AssetRegistry::getStats() const {
    std::lock_guard<std::mutex> lock(mutex_);

    Stats stats;
    stats.textureCount = textures_.size() - textureFreelist_.size();
    stats.meshCount = meshes_.size() - meshFreelist_.size();
    stats.shaderCount = shaders_.size() - shaderFreelist_.size();
    stats.textureCacheHits = textureCacheHits_;
    stats.shaderCacheHits = shaderCacheHits_;
    return stats;
}

void AssetRegistry::garbageCollect() {
    std::lock_guard<std::mutex> lock(mutex_);

    // Collect unreferenced textures
    for (size_t i = 0; i < textures_.size(); ++i) {
        auto& entry = textures_[i];
        if (entry.asset && entry.refCount == 0) {
            SDL_Log("AssetRegistry: GC texture '%s'", entry.name.c_str());

            // Remove from path cache
            auto it = texturePathCache_.find(entry.name);
            if (it != texturePathCache_.end() && it->second.index == i) {
                texturePathCache_.erase(it);
            }

            entry.asset.reset();
            entry.name.clear();
            entry.generation++; // Invalidate any stale handles
            textureFreelist_.push_back(static_cast<uint32_t>(i));
        }
    }

    // Collect unreferenced meshes
    for (size_t i = 0; i < meshes_.size(); ++i) {
        auto& entry = meshes_[i];
        if (entry.asset && entry.refCount == 0) {
            SDL_Log("AssetRegistry: GC mesh '%s'", entry.name.c_str());

            auto it = meshNameCache_.find(entry.name);
            if (it != meshNameCache_.end() && it->second.index == i) {
                meshNameCache_.erase(it);
            }

            entry.asset.reset();
            entry.name.clear();
            entry.generation++;
            meshFreelist_.push_back(static_cast<uint32_t>(i));
        }
    }

    // Collect unreferenced shaders
    for (size_t i = 0; i < shaders_.size(); ++i) {
        auto& entry = shaders_[i];
        if (entry.module && entry.refCount == 0) {
            SDL_Log("AssetRegistry: GC shader '%s'", entry.path.c_str());

            auto it = shaderPathCache_.find(entry.path);
            if (it != shaderPathCache_.end() && it->second.index == i) {
                shaderPathCache_.erase(it);
            }

            vkDestroyShaderModule(device_, entry.module, nullptr);
            entry.module = VK_NULL_HANDLE;
            entry.path.clear();
            entry.generation++;
            shaderFreelist_.push_back(static_cast<uint32_t>(i));
        }
    }
}

// ============================================================================
// Private Helpers
// ============================================================================

template<typename T>
uint32_t AssetRegistry::allocateSlot(std::vector<AssetEntry<T>>& storage,
                                      std::vector<uint32_t>& freelist) {
    if (!freelist.empty()) {
        uint32_t index = freelist.back();
        freelist.pop_back();
        return index;
    }
    return static_cast<uint32_t>(storage.size());
}

uint32_t AssetRegistry::allocateShaderSlot() {
    if (!shaderFreelist_.empty()) {
        uint32_t index = shaderFreelist_.back();
        shaderFreelist_.pop_back();
        return index;
    }
    return static_cast<uint32_t>(shaders_.size());
}

bool AssetRegistry::isValidTextureHandle(asset::TextureHandle handle) const {
    if (!handle.isValid() || handle.index >= textures_.size()) {
        return false;
    }
    const auto& entry = textures_[handle.index];
    return entry.asset != nullptr && entry.generation == handle.generation;
}

bool AssetRegistry::isValidMeshHandle(asset::MeshHandle handle) const {
    if (!handle.isValid() || handle.index >= meshes_.size()) {
        return false;
    }
    const auto& entry = meshes_[handle.index];
    return entry.asset != nullptr && entry.generation == handle.generation;
}

bool AssetRegistry::isValidShaderHandle(asset::ShaderHandle handle) const {
    if (!handle.isValid() || handle.index >= shaders_.size()) {
        return false;
    }
    const auto& entry = shaders_[handle.index];
    return entry.module != VK_NULL_HANDLE && entry.generation == handle.generation;
}

// Explicit template instantiations
template uint32_t AssetRegistry::allocateSlot<Texture>(
    std::vector<AssetEntry<Texture>>&, std::vector<uint32_t>&);
template uint32_t AssetRegistry::allocateSlot<Mesh>(
    std::vector<AssetEntry<Mesh>>&, std::vector<uint32_t>&);
