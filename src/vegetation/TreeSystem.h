#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <optional>

#include "TreeOptions.h"
#include "TreeGenerator.h"
#include "TreeCollision.h"
#include "Mesh.h"
#include "Texture.h"
#include "core/vulkan/VmaBuffer.h"
#include "scene/Transform.h"
#include "ecs/World.h"
#include "ecs/Components.h"

// Lightweight per-mesh render carrier for tree branches and leaves.
// Carries the tree-specific data (bark/leaf type, tint, autumn shift, instance
// indices) that the tree render/cull paths consume; the authoritative source is
// the ECS TreeData/BarkType/LeafType components, this is the per-frame upload POD.
struct TreeRenderable {
    Mesh* mesh = nullptr;
    glm::mat4 transform = glm::mat4(1.0f);
    float roughness = 0.5f;
    float metallic = 0.0f;
    float alphaTestThreshold = 0.0f;
    std::string barkType = "oak";
    std::string leafType = "oak";
    glm::vec3 leafTint = glm::vec3(1.0f);
    float autumnHueShift = 0.0f;
    int treeInstanceIndex = -1;
    int leafInstanceIndex = -1;
};

// GPU leaf instance data - matches shaders/tree_leaf_instance.glsl
// std430 layout: 32 bytes per instance
struct LeafInstanceGPU {
    glm::vec4 positionAndSize;  // xyz = world position, w = size
    glm::vec4 orientation;       // quaternion (x, y, z, w)
};
static_assert(sizeof(LeafInstanceGPU) == 32, "LeafInstanceGPU must be 32 bytes for std430 layout");

// Per-tree leaf instance offsets and counts for instanced drawing
struct LeafDrawInfo {
    uint32_t firstInstance;  // Starting instance in the SSBO
    uint32_t instanceCount;  // Number of leaf instances for this tree
};

// A single tree instance in the scene
struct TreeInstanceData {
    Transform transform;
    uint32_t meshIndex{0};  // Which tree mesh to use
    uint32_t archetypeIndex{0}; // Which impostor archetype to use (0=oak, 1=pine, 2=ash, 3=aspen)

    TreeInstanceData() = default;

    // Convenience: Create with Y-axis rotation only (radians)
    static TreeInstanceData withYRotation(const glm::vec3& pos, float yRotation, float s,
                                           uint32_t mesh, uint32_t archetype) {
        TreeInstanceData data;
        data.transform = Transform(pos, Transform::yRotation(yRotation), s);
        data.meshIndex = mesh;
        data.archetypeIndex = archetype;
        return data;
    }

    // Accessors for backward compatibility
    const glm::vec3& position() const { return transform.position; }
    const glm::quat& rotation() const { return transform.rotation; }
    float scale() const { return transform.scale.x; }

    // Get Y-axis rotation in radians
    float getYRotation() const {
        glm::vec3 euler = glm::eulerAngles(transform.rotation);
        return euler.y;
    }

    // Get transform matrix
    glm::mat4 getTransformMatrix() const { return transform.toMatrix(); }
};

// Bake contract:
//   ECS is the config source of truth. Each tree entity carries Transform +
//   TreeConfig{meshIndex,archetypeIndex} + TreeData(tint/autumn) + Bark/LeafType
//   + a sparse TreeSelected tag. treeInstances_,
//   branchRenderables_/leafRenderables_, leafDrawInfoPerTree_ and the leaf SSBO
//   are ONE-WAY derived bakes rebuilt on change (add/remove/regenerate/edit),
//   never per frame and never written back to the ECS. All of these share one
//   canonical insertion order (treeEntities_), so every parallel index
//   (treeInstanceIndex, lodStates_[i], cull inputData[i], meshIndex, GPU slot)
//   is equal by construction. meshIndex is 1:1 with the tree index.
class TreeSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit TreeSystem(ConstructToken) {}

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        vk::CommandPool commandPool;
        vk::Queue graphicsQueue;
        vk::PhysicalDevice physicalDevice;
        std::string resourcePath;
        std::function<float(float, float)> getTerrainHeight;  // Terrain height query
        float terrainSize;
    };

    /**
     * Factory: Create and initialize TreeSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<TreeSystem> create(const InitInfo& info);

    // Destroys this system's ECS entities; GPU resources (meshes, textures, the
    // leaf instance SSBO) are RAII members released by member destruction.
    ~TreeSystem();

    // Non-copyable, non-movable
    TreeSystem(const TreeSystem&) = delete;
    TreeSystem& operator=(const TreeSystem&) = delete;
    TreeSystem(TreeSystem&&) = delete;
    TreeSystem& operator=(TreeSystem&&) = delete;

    // ECS integration
    void setECSWorld(ecs::World* world) { world_ = world; }
    ecs::World* getECSWorld() const { return world_; }

    // Get scene objects for rendering (integrated with existing pipeline)
    const std::vector<TreeRenderable>& getBranchRenderables() const { return branchRenderables_; }
    std::vector<TreeRenderable>& getBranchRenderables() { return branchRenderables_; }

    const std::vector<TreeRenderable>& getLeafRenderables() const { return leafRenderables_; }
    std::vector<TreeRenderable>& getLeafRenderables() { return leafRenderables_; }

    // Tree management
    uint32_t addTree(const glm::vec3& position, float rotation, float scale, const TreeOptions& options);

    /**
     * Add a tree from pre-generated staged data (for threaded loading)
     * This uploads pre-generated mesh data to GPU without regenerating
     *
     * @param position World position
     * @param rotation Y-axis rotation in radians
     * @param scale Uniform scale factor
     * @param options Tree options for texture selection
     * @param branchVertexData Raw vertex data (Vertex structs)
     * @param branchVertexCount Number of vertices
     * @param branchIndices Index buffer data
     * @param leafInstanceData Raw leaf instance data (LeafInstanceGPU structs)
     * @param leafInstanceCount Number of leaf instances
     * @param archetypeIndex Impostor archetype index
     * @return Tree index, or UINT32_MAX on failure
     */
    uint32_t addTreeFromStagedData(
        const glm::vec3& position, float rotation, float scale,
        const TreeOptions& options,
        const std::vector<uint8_t>& branchVertexData,
        uint32_t branchVertexCount,
        const std::vector<uint32_t>& branchIndices,
        const std::vector<uint8_t>& leafInstanceData,
        uint32_t leafInstanceCount,
        uint32_t archetypeIndex);

    /**
     * Batch upload leaf instance buffer after adding multiple trees
     * Call this after adding all trees to avoid re-uploading for each tree
     */
    bool finalizeLeafInstanceBuffer();

    /**
     * Rendering gate for incremental forest generation: while trees stream in
     * over multiple frames, the shared leaf instance buffer and culling data
     * are not yet finalized, so render/culling paths must skip trees until
     * setRenderReady(true) after finalization.
     */
    void setRenderReady(bool ready) { renderReady_ = ready; }
    bool isRenderReady() const { return renderReady_; }

    void removeTree(uint32_t index);
    void selectTree(int index);
    int getSelectedTreeIndex() const { return selectedTreeIndex_; }

    // Update selected tree's options (triggers mesh regeneration)
    void updateSelectedTreeOptions(const TreeOptions& options);
    const TreeOptions* getSelectedTreeOptions() const;

    // Preset management
    void loadPreset(const std::string& name);
    void setPreset(const TreeOptions& preset);

    // Statistics
    size_t getTreeCount() const { return treeInstances_.size(); }
    size_t getMeshCount() const { return branchMeshes_.size(); }

    // Get tree instances for physics/other systems
    const std::vector<TreeInstanceData>& getTreeInstances() const { return treeInstances_; }

    // Get ECS entity for a tree by index
    ecs::Entity getTreeEntity(uint32_t index) const {
        if (index < treeEntities_.size()) return treeEntities_[index];
        return ecs::NullEntity;
    }
    const std::vector<ecs::Entity>& getTreeEntities() const { return treeEntities_; }

    // Generate collision capsule data for a tree instance
    // Returns capsules in world space (tree position + rotation + scale applied)
    std::vector<PhysicsWorld::CapsuleData> getTreeCollisionCapsules(
        uint32_t treeIndex,
        const TreeCollision::Config& config = TreeCollision::Config{}) const;

    // Get raw mesh data for a tree (for external collision generation)
    const TreeMeshData* getTreeMeshData(uint32_t meshIndex) const;

    // Access textures for GUI display
    const TreeOptions& getDefaultOptions() const { return defaultOptions_; }
    TreeOptions& getDefaultOptions() { return defaultOptions_; }

    // Access textures for descriptor set binding (uses default texture if type not found)
    Texture* getBarkTexture(const std::string& type) const;
    Texture* getBarkNormalMap(const std::string& type) const;
    Texture* getLeafTexture(const std::string& type) const;

    // Get all texture type names (for iteration in renderer)
    std::vector<std::string> getBarkTextureTypes() const;
    std::vector<std::string> getLeafTextureTypes() const;

    // Regenerate tree at index with new options
    void regenerateTree(uint32_t index);

    // Leaf instancing accessors (for TreeRenderer)
    vk::Buffer getLeafInstanceBuffer() const { return leafInstanceBuffer_.get(); }
    vk::DeviceSize getLeafInstanceBufferSize() const { return leafInstanceBufferSize_; }
    const Mesh& getSharedLeafQuadMesh() const { return sharedLeafQuadMesh_; }
    const std::vector<LeafDrawInfo>& getLeafDrawInfo() const { return leafDrawInfoPerTree_; }

    // Accessors for impostor generation
    const Mesh& getBranchMesh(uint32_t meshIndex) const { return branchMeshes_[meshIndex]; }
    const std::vector<LeafInstanceGPU>& getLeafInstances(uint32_t meshIndex) const { return leafInstancesPerTree_[meshIndex]; }
    const TreeOptions& getTreeOptions(uint32_t meshIndex) const { return treeOptions_[meshIndex]; }

    // Get full tree bounds (branches + leaves) for accurate imposter sizing
    const AABB& getFullTreeBounds(uint32_t meshIndex) const { return fullTreeBounds_[meshIndex]; }

private:
    bool initInternal(const InitInfo& info);
    bool loadTextures(const InitInfo& info);
    bool generateTreeMesh(const TreeOptions& options, Mesh& branchMesh, std::vector<LeafInstanceGPU>& leafInstances,
                          TreeMeshData* meshDataOut = nullptr);
    bool createSharedLeafQuadMesh();
    bool uploadLeafInstanceBuffer();
    void createSceneObjects();
    void rebuildSceneObjects();
    // Re-derive treeInstances_ (a one-way read-model) from the ECS source of
    // truth, ordered by treeEntities_ insertion order. Called at the start of
    // every bake; never writes back to the ECS.
    void rebuildTreeInstancesFromECS();

    // ECS entity creation/destruction for trees
    ecs::Entity createTreeEntity(const TreeInstanceData& instance, const TreeOptions& opts, const AABB& bounds);
    void destroyTreeEntity(uint32_t index);
    void refreshMeshRefs();  // Update MeshRef pointers after branchMeshes_ reallocation

    // ECS world reference (not owned)
    ecs::World* world_ = nullptr;

    // ECS entities for each tree instance (parallel to treeInstances_)
    std::vector<ecs::Entity> treeEntities_;

    // Stored for RAII cleanup and reload
    VmaAllocator storedAllocator_ = VK_NULL_HANDLE;
    vk::Device storedDevice_ = VK_NULL_HANDLE;
    vk::CommandPool storedCommandPool_ = VK_NULL_HANDLE;
    vk::Queue storedQueue_ = VK_NULL_HANDLE;
    vk::PhysicalDevice storedPhysicalDevice_ = VK_NULL_HANDLE;
    std::string storedResourcePath_;

    // Tree generator
    TreeGenerator generator_;

    // Tree options per mesh
    std::vector<TreeOptions> treeOptions_;
    TreeOptions defaultOptions_;

    // Tree meshes (branches only - leaves use instanced quad)
    std::vector<Mesh> branchMeshes_;

    // Shared leaf quad mesh (4 vertices, 6 indices) used for all leaf instances
    Mesh sharedLeafQuadMesh_;

    // Leaf instance data per tree (CPU-side, uploaded to GPU SSBO)
    // Each tree's leaves are stored contiguously: tree 0 leaves, tree 1 leaves, etc.
    std::vector<std::vector<LeafInstanceGPU>> leafInstancesPerTree_;

    // All leaf instances combined for GPU upload (flattened from leafInstancesPerTree_)
    std::vector<LeafInstanceGPU> allLeafInstances_;

    // Per-tree leaf instance offsets and counts for instanced drawing
    std::vector<LeafDrawInfo> leafDrawInfoPerTree_;

    // Leaf instance SSBO (storage buffer for GPU, persistently mapped at creation)
    VmaBuffer leafInstanceBuffer_;
    vk::DeviceSize leafInstanceBufferSize_ = 0;

    // Raw mesh data (stored for collision generation)
    std::vector<TreeMeshData> treeMeshData_;

    // Full tree bounds (branches + leaves) per mesh - for accurate imposter sizing
    std::vector<AABB> fullTreeBounds_;

    // Textures indexed by type name (e.g., "oak", "pine", "ash")
    std::unordered_map<std::string, std::unique_ptr<Texture>> barkTextures_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> barkNormalMaps_;
    std::unordered_map<std::string, std::unique_ptr<Texture>> leafTextures_;

    // ONE-WAY DERIVED read-model of per-tree transform + meshIndex/archetype.
    // The ECS (Transform + TreeConfig on each tree entity) is the source of
    // truth; this cache is rebuilt from it on every bake (rebuildSceneObjects ->
    // rebuildTreeInstancesFromECS), ordered by treeEntities_ insertion order.
    // Never written by a render/cull/physics consumer. meshIndex is 1:1 with the
    // tree index; indices here are ephemeral per bake.
    std::vector<TreeInstanceData> treeInstances_;
    int selectedTreeIndex_ = -1;
    bool renderReady_ = true;  // Cleared while a forest streams in incrementally

    // ONE-WAY DERIVED render bake, rebuilt on add/remove/regenerate/edit (never
    // per draw). These may be SPARSER than treeInstances_ (a tree with an empty
    // branch mesh or no leaves is skipped), so vector position is NOT the tree
    // index. Consumers key off the carried indices: renderable.treeInstanceIndex
    // == tree index i (LOD/lodStates_ lookup); renderable.leafInstanceIndex ==
    // meshIndex (leafDrawInfoPerTree_ lookup), and meshIndex is 1:1 with i.
    std::vector<TreeRenderable> branchRenderables_;
    std::vector<TreeRenderable> leafRenderables_;
};
