#pragma once

#include "AssetHandle.h"
#include "../Texture.h"
#include "../Mesh.h"
#include "../ShaderLoader.h"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <optional>
#include <mutex>

/**
 * AssetRegistry - Centralized asset management with deduplication and caching
 *
 * The AssetRegistry provides:
 * - Path-based deduplication: Loading the same path twice returns the same handle
 * - Reference counting: Assets are kept alive while referenced
 * - Handle-based access: Lightweight handles instead of raw pointers
 * - Thread-safe loading: Mutex-protected for async loading compatibility
 *
 * Usage:
 *   AssetRegistry registry;
 *   registry.init(initContext);
 *
 *   // Load texture (second call returns cached handle)
 *   auto handle = registry.loadTexture("assets/textures/brick.png");
 *   auto handle2 = registry.loadTexture("assets/textures/brick.png"); // Same handle
 *
 *   // Access the texture
 *   if (const Texture* tex = registry.getTexture(handle)) {
 *       VkImageView view = tex->getImageView();
 *   }
 *
 *   // Release when done (decrements refcount)
 *   registry.releaseTexture(handle);
 */
class AssetRegistry {
public:
    // Configuration for texture loading
    struct TextureLoadConfig {
        bool useSRGB = true;
        bool generateMipmaps = true;
        bool enableAnisotropy = true;
    };

    // Configuration for mesh creation
    struct MeshConfig {
        enum class Type {
            Cube,
            Plane,
            Sphere,
            Cylinder,
            Capsule,
            Disc,
            Rock,
            Custom
        };

        Type type = Type::Cube;

        // Plane/Disc parameters
        float width = 1.0f;
        float depth = 1.0f;
        float radius = 1.0f;

        // Sphere/Cylinder/Capsule parameters
        float height = 1.0f;
        int stacks = 16;
        int slices = 32;
        int segments = 32;

        // Rock parameters
        int subdivisions = 3;
        uint32_t seed = 0;
        float roughness = 0.3f;
        float asymmetry = 0.2f;

        // Disc UV scale
        float uvScale = 1.0f;
    };

    AssetRegistry() = default;
    ~AssetRegistry();

    // Non-copyable, non-movable (owns GPU resources)
    AssetRegistry(const AssetRegistry&) = delete;
    AssetRegistry& operator=(const AssetRegistry&) = delete;
    AssetRegistry(AssetRegistry&&) = delete;
    AssetRegistry& operator=(AssetRegistry&&) = delete;

    /**
     * Initialize the registry with Vulkan context.
     * Must be called before any asset loading.
     */
    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              VmaAllocator allocator, VkCommandPool commandPool,
              VkQueue queue);

    /**
     * Cleanup all loaded assets.
     * Call before destroying Vulkan context.
     */
    void cleanup();

    // ========================================================================
    // Texture Management
    // ========================================================================

    /**
     * Load a texture from file with deduplication.
     * If the texture is already loaded, returns the existing handle.
     */
    asset::TextureHandle loadTexture(const std::string& path,
                                     const TextureLoadConfig& config = {});

    /**
     * Create a solid color texture (not path-cached).
     */
    asset::TextureHandle createSolidColorTexture(uint8_t r, uint8_t g,
                                                  uint8_t b, uint8_t a,
                                                  const std::string& name = "");

    /**
     * Register an externally-created texture.
     * The registry takes ownership of the texture.
     */
    asset::TextureHandle registerTexture(std::unique_ptr<Texture> texture,
                                          const std::string& name = "");

    /**
     * Get texture by handle. Returns nullptr if handle is invalid.
     */
    const Texture* getTexture(asset::TextureHandle handle) const;

    /**
     * Get texture handle by path/name. Returns invalid handle if not found.
     */
    asset::TextureHandle getTextureHandle(const std::string& path) const;

    /**
     * Add a reference to a texture (increment refcount).
     */
    void addTextureRef(asset::TextureHandle handle);

    /**
     * Release a texture reference (decrement refcount).
     * When refcount reaches 0, texture may be garbage collected.
     */
    void releaseTexture(asset::TextureHandle handle);

    // ========================================================================
    // Mesh Management
    // ========================================================================

    /**
     * Create a procedural mesh.
     */
    asset::MeshHandle createMesh(const MeshConfig& config,
                                  const std::string& name = "");

    /**
     * Create a mesh from custom geometry.
     */
    asset::MeshHandle createCustomMesh(const std::vector<Vertex>& vertices,
                                        const std::vector<uint32_t>& indices,
                                        const std::string& name = "");

    /**
     * Register an externally-created mesh.
     * The registry takes ownership of the mesh.
     */
    asset::MeshHandle registerMesh(std::unique_ptr<Mesh> mesh,
                                    const std::string& name = "");

    /**
     * Get mesh by handle. Returns nullptr if handle is invalid.
     */
    const Mesh* getMesh(asset::MeshHandle handle) const;

    /**
     * Get mutable mesh by handle (for dynamic mesh updates).
     */
    Mesh* getMeshMutable(asset::MeshHandle handle);

    /**
     * Get mesh handle by name. Returns invalid handle if not found.
     */
    asset::MeshHandle getMeshHandle(const std::string& name) const;

    /**
     * Add a reference to a mesh.
     */
    void addMeshRef(asset::MeshHandle handle);

    /**
     * Release a mesh reference.
     */
    void releaseMesh(asset::MeshHandle handle);

    // ========================================================================
    // Shader Management
    // ========================================================================

    /**
     * Load a shader module from file with caching.
     */
    asset::ShaderHandle loadShader(const std::string& path);

    /**
     * Get shader module by handle.
     */
    vk::ShaderModule getShader(asset::ShaderHandle handle) const;

    /**
     * Get shader handle by path. Returns invalid handle if not found.
     */
    asset::ShaderHandle getShaderHandle(const std::string& path) const;

    /**
     * Release a shader reference.
     */
    void releaseShader(asset::ShaderHandle handle);

    // ========================================================================
    // Statistics and Debugging
    // ========================================================================

    struct Stats {
        size_t textureCount = 0;
        size_t meshCount = 0;
        size_t shaderCount = 0;
        size_t textureCacheHits = 0;
        size_t shaderCacheHits = 0;
    };

    Stats getStats() const;

    /**
     * Garbage collect unreferenced assets.
     * Assets with refcount 0 will be destroyed.
     */
    void garbageCollect();

private:
    // Storage entry with reference counting
    template<typename T>
    struct AssetEntry {
        std::unique_ptr<T> asset;
        std::string name;
        uint32_t refCount = 1;
        uint32_t generation = 0;
    };

    // Shader entry (special case: vk::ShaderModule not unique_ptr)
    struct ShaderEntry {
        vk::ShaderModule module;
        std::string path;
        uint32_t refCount = 1;
        uint32_t generation = 0;
    };

    // Vulkan context
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;

    // Asset storage
    std::vector<AssetEntry<Texture>> textures_;
    std::vector<AssetEntry<Mesh>> meshes_;
    std::vector<ShaderEntry> shaders_;

    // Path/name to handle lookup for deduplication
    std::unordered_map<std::string, asset::TextureHandle> texturePathCache_;
    std::unordered_map<std::string, asset::MeshHandle> meshNameCache_;
    std::unordered_map<std::string, asset::ShaderHandle> shaderPathCache_;

    // Free list for reusing slots (indices of entries with refCount 0)
    std::vector<uint32_t> textureFreelist_;
    std::vector<uint32_t> meshFreelist_;
    std::vector<uint32_t> shaderFreelist_;

    // Statistics
    mutable size_t textureCacheHits_ = 0;
    mutable size_t shaderCacheHits_ = 0;

    // Thread safety
    mutable std::mutex mutex_;

    // Generation counter for ABA problem prevention
    uint32_t nextGeneration_ = 1;

    // Helper to allocate a slot from storage
    template<typename T>
    uint32_t allocateSlot(std::vector<AssetEntry<T>>& storage,
                          std::vector<uint32_t>& freelist);
    uint32_t allocateShaderSlot();

    // Validate handle
    template<typename T>
    bool isValidHandle(const asset::Handle<T>& handle,
                       const std::vector<AssetEntry<typename std::conditional<
                           std::is_same_v<T, asset::ShaderTag>, void,
                           std::conditional_t<std::is_same_v<T, asset::TextureTag>, Texture,
                           std::conditional_t<std::is_same_v<T, asset::MeshTag>, Mesh, void>>>::type>>& storage) const;

    bool isValidTextureHandle(asset::TextureHandle handle) const;
    bool isValidMeshHandle(asset::MeshHandle handle) const;
    bool isValidShaderHandle(asset::ShaderHandle handle) const;
};
