#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <vector>
#include "Components.h"

// ============================================================================
// Transform Hierarchy System
// ============================================================================
// Unity-like transform hierarchy for ECS entities.
// - Entities with Hierarchy component have local transforms (relative to parent)
// - WorldTransform is computed by walking up the parent chain
// - Updates are done in depth-first order to ensure parents update before children

namespace TransformHierarchy {

// Forward declarations
inline void updateDepth(entt::registry& registry, entt::entity entity);

// ============================================================================
// Hierarchy Management Functions
// ============================================================================

// Set parent for an entity. Updates both parent's children list and child's parent reference.
// If the entity doesn't have a Hierarchy component, one will be added.
// Pass entt::null to remove parent (make root entity).
inline void setParent(entt::registry& registry, entt::entity child, entt::entity newParent) {
    if (!registry.valid(child)) return;

    // Ensure child has Hierarchy component
    if (!registry.all_of<Hierarchy>(child)) {
        registry.emplace<Hierarchy>(child);
    }

    auto& childHierarchy = registry.get<Hierarchy>(child);
    entt::entity oldParent = childHierarchy.parent;

    // No change needed
    if (oldParent == newParent) return;

    // Prevent circular references
    if (newParent != entt::null) {
        entt::entity ancestor = newParent;
        while (ancestor != entt::null && registry.valid(ancestor)) {
            if (ancestor == child) {
                // Circular reference detected - cannot set parent
                return;
            }
            if (registry.all_of<Hierarchy>(ancestor)) {
                ancestor = registry.get<Hierarchy>(ancestor).parent;
            } else {
                break;
            }
        }
    }

    // Remove from old parent's children list
    if (oldParent != entt::null && registry.valid(oldParent) &&
        registry.all_of<Hierarchy>(oldParent)) {
        auto& oldParentHierarchy = registry.get<Hierarchy>(oldParent);
        auto& children = oldParentHierarchy.children;
        children.erase(std::remove(children.begin(), children.end(), child), children.end());
    }

    // Update child's parent reference
    childHierarchy.parent = newParent;

    // Add to new parent's children list
    if (newParent != entt::null && registry.valid(newParent)) {
        // Ensure parent has Hierarchy component
        if (!registry.all_of<Hierarchy>(newParent)) {
            registry.emplace<Hierarchy>(newParent);
        }
        auto& newParentHierarchy = registry.get<Hierarchy>(newParent);
        newParentHierarchy.children.push_back(child);
    }

    // Update depth for child and all descendants
    updateDepth(registry, child);

    // Mark as dirty
    if (!registry.all_of<HierarchyDirty>(child)) {
        registry.emplace<HierarchyDirty>(child);
    }
}

// Remove entity from its parent (make it a root)
inline void removeFromParent(entt::registry& registry, entt::entity entity) {
    setParent(registry, entity, entt::null);
}

// Add a child to an entity (convenience wrapper for setParent)
inline void addChild(entt::registry& registry, entt::entity parent, entt::entity child) {
    setParent(registry, child, parent);
}

// Get the depth of an entity in the hierarchy (0 = root)
inline uint32_t getDepth(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) {
        return 0;
    }
    return registry.get<Hierarchy>(entity).depth;
}

// Update depth for an entity and all its descendants
inline void updateDepth(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) return;

    auto& hierarchy = registry.get<Hierarchy>(entity);

    // Calculate depth from parent
    if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent) &&
        registry.all_of<Hierarchy>(hierarchy.parent)) {
        hierarchy.depth = registry.get<Hierarchy>(hierarchy.parent).depth + 1;
    } else {
        hierarchy.depth = 0;
    }

    // Recursively update children
    for (entt::entity child : hierarchy.children) {
        updateDepth(registry, child);
    }
}

// Get all children of an entity
inline std::vector<entt::entity> getChildren(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) {
        return {};
    }
    return registry.get<Hierarchy>(entity).children;
}

// Get parent of an entity (returns entt::null if no parent)
inline entt::entity getParent(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Hierarchy>(entity)) {
        return entt::null;
    }
    return registry.get<Hierarchy>(entity).parent;
}

// Check if entity is descendant of another entity
inline bool isDescendantOf(entt::registry& registry, entt::entity entity, entt::entity potentialAncestor) {
    if (!registry.valid(entity) || !registry.valid(potentialAncestor)) return false;

    entt::entity current = entity;
    while (current != entt::null && registry.valid(current)) {
        if (!registry.all_of<Hierarchy>(current)) break;
        entt::entity parent = registry.get<Hierarchy>(current).parent;
        if (parent == potentialAncestor) return true;
        current = parent;
    }
    return false;
}

// Get root ancestor of an entity (returns self if already root)
inline entt::entity getRoot(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity)) return entt::null;

    entt::entity current = entity;
    while (registry.valid(current) && registry.all_of<Hierarchy>(current)) {
        entt::entity parent = registry.get<Hierarchy>(current).parent;
        if (parent == entt::null || !registry.valid(parent)) break;
        current = parent;
    }
    return current;
}

// ============================================================================
// World Transform Computation
// ============================================================================

// Compute world matrix for a single entity (walks up the parent chain)
inline glm::mat4 computeWorldMatrix(entt::registry& registry, entt::entity entity) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) {
        return glm::mat4(1.0f);
    }

    const Transform& localTransform = registry.get<Transform>(entity);
    glm::mat4 localMatrix = localTransform.getMatrix();

    // If no hierarchy or no parent, local = world
    if (!registry.all_of<Hierarchy>(entity)) {
        return localMatrix;
    }

    const Hierarchy& hierarchy = registry.get<Hierarchy>(entity);
    if (hierarchy.parent == entt::null || !registry.valid(hierarchy.parent)) {
        return localMatrix;
    }

    // Get parent's world matrix (use cached if available, otherwise compute)
    glm::mat4 parentWorld;
    if (registry.all_of<WorldTransform>(hierarchy.parent)) {
        parentWorld = registry.get<WorldTransform>(hierarchy.parent).matrix;
    } else {
        parentWorld = computeWorldMatrix(registry, hierarchy.parent);
    }

    // world = parentWorld * local
    return parentWorld * localMatrix;
}

// Update world transform for a single entity (recursive, updates children too)
inline void updateWorldTransformRecursive(entt::registry& registry, entt::entity entity,
                                           const glm::mat4& parentWorld) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;

    const Transform& localTransform = registry.get<Transform>(entity);
    glm::mat4 worldMatrix = parentWorld * localTransform.getMatrix();

    // Ensure entity has WorldTransform component
    if (!registry.all_of<WorldTransform>(entity)) {
        registry.emplace<WorldTransform>(entity, WorldTransform{worldMatrix});
    } else {
        registry.get<WorldTransform>(entity).matrix = worldMatrix;
    }

    // Remove dirty flag if present
    if (registry.all_of<HierarchyDirty>(entity)) {
        registry.remove<HierarchyDirty>(entity);
    }

    // Update children
    if (registry.all_of<Hierarchy>(entity)) {
        const Hierarchy& hierarchy = registry.get<Hierarchy>(entity);
        for (entt::entity child : hierarchy.children) {
            updateWorldTransformRecursive(registry, child, worldMatrix);
        }
    }
}

// ============================================================================
// Transform Hierarchy System
// ============================================================================

// Main system function - updates all world transforms in correct order
// Call this each frame before rendering
inline void transformHierarchySystem(entt::registry& registry) {
    // Strategy: Find all root entities (no parent or parent is null)
    // and recursively update their children

    // First, update all entities that have Hierarchy component
    auto hierarchyView = registry.view<Transform, Hierarchy>();

    // Find roots (entities with no parent)
    std::vector<entt::entity> roots;
    for (auto entity : hierarchyView) {
        const auto& hierarchy = hierarchyView.get<Hierarchy>(entity);
        if (hierarchy.parent == entt::null || !registry.valid(hierarchy.parent)) {
            roots.push_back(entity);
        }
    }

    // Update from each root down
    for (entt::entity root : roots) {
        const Transform& localTransform = registry.get<Transform>(root);
        glm::mat4 rootWorld = localTransform.getMatrix();

        // Update root's world transform
        if (!registry.all_of<WorldTransform>(root)) {
            registry.emplace<WorldTransform>(root, WorldTransform{rootWorld});
        } else {
            registry.get<WorldTransform>(root).matrix = rootWorld;
        }

        // Remove dirty flag
        if (registry.all_of<HierarchyDirty>(root)) {
            registry.remove<HierarchyDirty>(root);
        }

        // Recursively update children
        const Hierarchy& rootHierarchy = registry.get<Hierarchy>(root);
        for (entt::entity child : rootHierarchy.children) {
            updateWorldTransformRecursive(registry, child, rootWorld);
        }
    }

    // Also update entities with Transform + WorldTransform but no Hierarchy
    // (simple entities that just need their local = world)
    auto simpleView = registry.view<Transform, WorldTransform>(entt::exclude<Hierarchy>);
    for (auto entity : simpleView) {
        const Transform& transform = simpleView.get<Transform>(entity);
        auto& worldTransform = simpleView.get<WorldTransform>(entity);
        worldTransform.matrix = transform.getMatrix();
    }
}

// Ensure an entity has WorldTransform component (for rendering)
// Returns the world matrix
inline const glm::mat4& ensureWorldTransform(entt::registry& registry, entt::entity entity) {
    if (!registry.all_of<WorldTransform>(entity)) {
        glm::mat4 world = computeWorldMatrix(registry, entity);
        registry.emplace<WorldTransform>(entity, WorldTransform{world});
    }
    return registry.get<WorldTransform>(entity).matrix;
}

// ============================================================================
// Transform Modification Helpers
// ============================================================================

// Set local position (relative to parent)
inline void setLocalPosition(entt::registry& registry, entt::entity entity, const glm::vec3& pos) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;
    registry.get<Transform>(entity).position = pos;
    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

// Set local rotation (relative to parent)
inline void setLocalRotation(entt::registry& registry, entt::entity entity, const glm::quat& rot) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;
    registry.get<Transform>(entity).rotation = rot;
    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

// Set local scale (relative to parent)
inline void setLocalScale(entt::registry& registry, entt::entity entity, const glm::vec3& scale) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;
    registry.get<Transform>(entity).scale = scale;
    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

// Set world position (calculates required local position to achieve world pos)
inline void setWorldPosition(entt::registry& registry, entt::entity entity, const glm::vec3& worldPos) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;

    auto& transform = registry.get<Transform>(entity);

    // If no hierarchy or no parent, world = local
    if (!registry.all_of<Hierarchy>(entity)) {
        transform.position = worldPos;
        return;
    }

    const Hierarchy& hierarchy = registry.get<Hierarchy>(entity);
    if (hierarchy.parent == entt::null || !registry.valid(hierarchy.parent)) {
        transform.position = worldPos;
        return;
    }

    // Get parent's world matrix and its inverse
    glm::mat4 parentWorld = computeWorldMatrix(registry, hierarchy.parent);
    glm::mat4 parentWorldInv = glm::inverse(parentWorld);

    // Transform world position to local space
    glm::vec4 localPos = parentWorldInv * glm::vec4(worldPos, 1.0f);
    transform.position = glm::vec3(localPos);

    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

// Translate in local space
inline void translateLocal(entt::registry& registry, entt::entity entity, const glm::vec3& delta) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;

    auto& transform = registry.get<Transform>(entity);
    glm::vec3 localDelta = glm::vec3(glm::mat4_cast(transform.rotation) * glm::vec4(delta, 0.0f));
    transform.position += localDelta;

    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

// Translate in world space
inline void translateWorld(entt::registry& registry, entt::entity entity, const glm::vec3& delta) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;

    // Get current world position, add delta, set new world position
    glm::mat4 worldMatrix = computeWorldMatrix(registry, entity);
    glm::vec3 currentWorldPos = glm::vec3(worldMatrix[3]);
    setWorldPosition(registry, entity, currentWorldPos + delta);
}

// Look at target in world space
inline void lookAt(entt::registry& registry, entt::entity entity, const glm::vec3& target,
                   const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
    if (!registry.valid(entity) || !registry.all_of<Transform>(entity)) return;

    glm::mat4 worldMatrix = computeWorldMatrix(registry, entity);
    glm::vec3 worldPos = glm::vec3(worldMatrix[3]);

    glm::vec3 forward = glm::normalize(target - worldPos);
    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    glm::vec3 correctedUp = glm::cross(forward, right);

    glm::mat3 rotMat(right, correctedUp, forward);
    glm::quat worldRot = glm::quat_cast(rotMat);

    auto& transform = registry.get<Transform>(entity);

    // Convert world rotation to local rotation if has parent
    if (registry.all_of<Hierarchy>(entity)) {
        const Hierarchy& hierarchy = registry.get<Hierarchy>(entity);
        if (hierarchy.parent != entt::null && registry.valid(hierarchy.parent)) {
            glm::mat4 parentWorld = computeWorldMatrix(registry, hierarchy.parent);
            glm::quat parentRot = glm::quat_cast(glm::mat3(parentWorld));
            transform.rotation = glm::inverse(parentRot) * worldRot;

            if (!registry.all_of<HierarchyDirty>(entity)) {
                registry.emplace<HierarchyDirty>(entity);
            }
            return;
        }
    }

    transform.rotation = worldRot;
    if (!registry.all_of<HierarchyDirty>(entity)) {
        registry.emplace<HierarchyDirty>(entity);
    }
}

} // namespace TransformHierarchy
