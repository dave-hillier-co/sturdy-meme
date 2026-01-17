#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <cstdint>
#include <limits>

/**
 * TransformHandle - Opaque handle to a transform in the hierarchy
 *
 * Uses generation counting to detect stale handles (similar to entity IDs in ECS).
 * A null handle has index == UINT32_MAX.
 */
struct TransformHandle {
    uint32_t index = std::numeric_limits<uint32_t>::max();
    uint32_t generation = 0;

    bool isValid() const { return index != std::numeric_limits<uint32_t>::max(); }
    bool operator==(const TransformHandle& other) const {
        return index == other.index && generation == other.generation;
    }
    bool operator!=(const TransformHandle& other) const { return !(*this == other); }

    static TransformHandle null() { return TransformHandle{}; }
};

/**
 * Transform - Local transform data (position, rotation, scale)
 *
 * This is the data you manipulate. The hierarchy manages world matrices.
 */
struct Transform {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity (w, x, y, z)
    glm::vec3 scale{1.0f};

    // Convenience constructors
    Transform() = default;
    explicit Transform(const glm::vec3& pos) : position(pos) {}
    Transform(const glm::vec3& pos, const glm::quat& rot) : position(pos), rotation(rot) {}
    Transform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& s)
        : position(pos), rotation(rot), scale(s) {}
    Transform(const glm::vec3& pos, const glm::quat& rot, float uniformScale)
        : position(pos), rotation(rot), scale(uniformScale) {}

    // Build local matrix (TRS order: translate * rotate * scale)
    glm::mat4 toMatrix() const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
        m = m * glm::mat4_cast(rotation);
        m = glm::scale(m, scale);
        return m;
    }

    // Direction vectors
    glm::vec3 forward() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }
    glm::vec3 right() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }
    glm::vec3 up() const {
        return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    }

    // Rotation helpers
    static glm::quat yRotation(float radians) {
        return glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f));
    }
    static glm::quat fromEuler(const glm::vec3& euler) {
        glm::quat qYaw = glm::angleAxis(euler.y, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(euler.x, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat qRoll = glm::angleAxis(euler.z, glm::vec3(0.0f, 0.0f, 1.0f));
        return qYaw * qPitch * qRoll;
    }

    // Look at a target (Y-up)
    void lookAt(const glm::vec3& target, const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f)) {
        glm::vec3 fwd = glm::normalize(target - position);
        glm::vec3 r = glm::normalize(glm::cross(worldUp, fwd));
        glm::vec3 u = glm::cross(fwd, r);
        rotation = glm::quat_cast(glm::mat3(r, u, fwd));
    }
};

/**
 * TransformHierarchy - Manages parent-child relationships and world matrices
 *
 * Key features:
 * - Handles instead of raw pointers (safe, no dangling references)
 * - Cached world matrices (updated lazily or in batch)
 * - Dirty propagation to children
 * - O(1) parent lookup, O(children) for child iteration
 *
 * Usage:
 *   TransformHierarchy hierarchy;
 *   auto root = hierarchy.create("root");
 *   auto child = hierarchy.create("child", root);  // child parented to root
 *
 *   hierarchy.setLocal(child, Transform(glm::vec3(1, 0, 0)));
 *   hierarchy.updateWorldMatrices();  // Batch update all dirty matrices
 *   glm::mat4 worldMat = hierarchy.getWorldMatrix(child);
 */
class TransformHierarchy {
public:
    // Create a new transform, optionally parented
    TransformHandle create(const std::string& name = "", TransformHandle parent = TransformHandle::null());

    // Destroy a transform and orphan its children (children become roots)
    void destroy(TransformHandle handle);

    // Check if handle is valid (not destroyed, correct generation)
    bool isValid(TransformHandle handle) const;

    // ========================================================================
    // Local transform access
    // ========================================================================

    // Get/set local transform (marks dirty)
    const Transform& getLocal(TransformHandle handle) const;
    void setLocal(TransformHandle handle, const Transform& transform);

    // Convenience: modify in place
    Transform& localMut(TransformHandle handle);
    void markDirty(TransformHandle handle);

    // ========================================================================
    // World matrix access
    // ========================================================================

    // Get world matrix (computed on demand if dirty)
    const glm::mat4& getWorldMatrix(TransformHandle handle);

    // Get world position (extracts from world matrix)
    glm::vec3 getWorldPosition(TransformHandle handle);

    // Batch update all dirty world matrices (call once per frame for efficiency)
    void updateWorldMatrices();

    // ========================================================================
    // Hierarchy management
    // ========================================================================

    // Set parent (null to make root)
    void setParent(TransformHandle handle, TransformHandle newParent);

    // Get parent handle (null if root)
    TransformHandle getParent(TransformHandle handle) const;

    // Get children
    const std::vector<TransformHandle>& getChildren(TransformHandle handle) const;

    // Get name
    const std::string& getName(TransformHandle handle) const;
    void setName(TransformHandle handle, const std::string& name);

    // Find by name (searches all transforms, returns first match)
    TransformHandle findByName(const std::string& name) const;

    // ========================================================================
    // Iteration
    // ========================================================================

    // Get all root transforms (no parent)
    const std::vector<TransformHandle>& getRoots() const { return roots_; }

    // Get total count of active transforms
    size_t count() const { return count_; }

private:
    struct Node {
        Transform local;
        glm::mat4 worldMatrix{1.0f};
        std::string name;
        TransformHandle parent;
        std::vector<TransformHandle> children;
        uint32_t generation = 0;
        bool dirty = true;
        bool active = false;
    };

    void propagateDirty(uint32_t index);
    void updateSingleNode(uint32_t index);  // On-demand: walks up only
    void updateWorldMatrixRecursive(uint32_t index);  // Batch: walks down from roots
    uint32_t allocateNode();

    std::vector<Node> nodes_;
    std::vector<uint32_t> freeList_;
    std::vector<TransformHandle> roots_;
    size_t count_ = 0;

    // Empty children vector for invalid handles
    static const std::vector<TransformHandle> emptyChildren_;
};
