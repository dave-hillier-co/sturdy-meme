#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <memory>
#include <string>

class Mesh;
class TreeSystem;
struct TreeOptions;

/**
 * Manages combined mesh buffers per tree archetype for GPU-driven rendering.
 *
 * Each archetype (oak, pine, ash, aspen) has:
 * - Combined vertex buffer
 * - Combined index buffer
 * - Per-archetype indirect draw command
 *
 * This enables rendering thousands of trees per archetype with a single
 * vkCmdDrawIndexedIndirect call, using instance data from GPU forest compute.
 */
class TreeArchetypeMeshes {
public:
    static constexpr uint32_t MAX_ARCHETYPES = 4;

    struct InitInfo {
        VkDevice device = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VmaAllocator allocator = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkQueue graphicsQueue = VK_NULL_HANDLE;
        std::string resourcePath;
    };

    // Per-archetype mesh data
    struct ArchetypeMesh {
        std::string name;              // e.g., "oak", "pine"
        VkBuffer vertexBuffer = VK_NULL_HANDLE;
        VmaAllocation vertexAllocation = VK_NULL_HANDLE;
        VkBuffer indexBuffer = VK_NULL_HANDLE;
        VmaAllocation indexAllocation = VK_NULL_HANDLE;
        uint32_t indexCount = 0;
        uint32_t vertexCount = 0;
        bool valid = false;
    };

    static std::unique_ptr<TreeArchetypeMeshes> create(const InitInfo& info);
    ~TreeArchetypeMeshes();

    // Non-copyable
    TreeArchetypeMeshes(const TreeArchetypeMeshes&) = delete;
    TreeArchetypeMeshes& operator=(const TreeArchetypeMeshes&) = delete;

    // Build archetype meshes from TreeSystem presets
    bool buildFromPresets(const TreeSystem& treeSystem);

    // Build archetype mesh from a specific tree options preset
    bool buildArchetype(uint32_t archetypeIndex, const std::string& name, const TreeOptions& options);

    // Get archetype mesh data
    const ArchetypeMesh& getArchetype(uint32_t index) const;
    uint32_t getArchetypeCount() const { return archetypeCount_; }
    bool isReady() const { return initialized_; }

    // Render all instances of an archetype using indirect draw
    void renderArchetypeIndirect(
        VkCommandBuffer cmd,
        uint32_t archetypeIndex,
        VkBuffer instanceBuffer,
        VkDeviceSize instanceOffset,
        VkBuffer indirectBuffer,
        VkDeviceSize indirectOffset
    );

private:
    TreeArchetypeMeshes() = default;
    bool init(const InitInfo& info);
    void cleanup();
    void destroyArchetype(ArchetypeMesh& archetype);

    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::string resourcePath_;

    std::array<ArchetypeMesh, MAX_ARCHETYPES> archetypes_;
    uint32_t archetypeCount_ = 0;
    bool initialized_ = false;
};
