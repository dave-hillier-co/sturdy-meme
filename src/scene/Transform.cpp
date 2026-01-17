#include "Transform.h"
#include <algorithm>

const std::vector<TransformHandle> TransformHierarchy::emptyChildren_;

TransformHandle TransformHierarchy::create(const std::string& name, TransformHandle parent) {
    uint32_t index = allocateNode();

    Node& node = nodes_[index];
    node.local = Transform{};
    node.worldMatrix = glm::mat4(1.0f);
    node.name = name;
    node.parent = TransformHandle::null();
    node.children.clear();
    node.dirty = true;
    node.active = true;

    TransformHandle handle;
    handle.index = index;
    handle.generation = node.generation;

    // Set parent (handles root tracking)
    if (parent.isValid()) {
        setParent(handle, parent);
    } else {
        roots_.push_back(handle);
    }

    count_++;
    return handle;
}

void TransformHierarchy::destroy(TransformHandle handle) {
    if (!isValid(handle)) return;

    Node& node = nodes_[handle.index];

    // Orphan children (make them roots)
    for (auto& child : node.children) {
        if (isValid(child)) {
            nodes_[child.index].parent = TransformHandle::null();
            roots_.push_back(child);
        }
    }
    node.children.clear();

    // Remove from parent's children list
    if (node.parent.isValid() && isValid(node.parent)) {
        auto& siblings = nodes_[node.parent.index].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
    }

    // Remove from roots if necessary
    roots_.erase(std::remove(roots_.begin(), roots_.end(), handle), roots_.end());

    // Mark inactive and increment generation
    node.active = false;
    node.generation++;
    freeList_.push_back(handle.index);
    count_--;
}

bool TransformHierarchy::isValid(TransformHandle handle) const {
    if (!handle.isValid()) return false;
    if (handle.index >= nodes_.size()) return false;
    const Node& node = nodes_[handle.index];
    return node.active && node.generation == handle.generation;
}

const Transform& TransformHierarchy::getLocal(TransformHandle handle) const {
    static const Transform identity;
    if (!isValid(handle)) return identity;
    return nodes_[handle.index].local;
}

void TransformHierarchy::setLocal(TransformHandle handle, const Transform& transform) {
    if (!isValid(handle)) return;
    nodes_[handle.index].local = transform;
    markDirty(handle);
}

Transform& TransformHierarchy::localMut(TransformHandle handle) {
    static Transform dummy;
    if (!isValid(handle)) return dummy;
    return nodes_[handle.index].local;
}

void TransformHierarchy::markDirty(TransformHandle handle) {
    if (!isValid(handle)) return;
    propagateDirty(handle.index);
}

const glm::mat4& TransformHierarchy::getWorldMatrix(TransformHandle handle) {
    static const glm::mat4 identity(1.0f);
    if (!isValid(handle)) return identity;

    Node& node = nodes_[handle.index];
    if (node.dirty) {
        updateSingleNode(handle.index);
    }
    return node.worldMatrix;
}

void TransformHierarchy::updateSingleNode(uint32_t index) {
    Node& node = nodes_[index];
    if (!node.dirty) return;

    // Walk up to update parent first
    if (node.parent.isValid() && isValid(node.parent)) {
        updateSingleNode(node.parent.index);
        node.worldMatrix = nodes_[node.parent.index].worldMatrix * node.local.toMatrix();
    } else {
        node.worldMatrix = node.local.toMatrix();
    }
    node.dirty = false;
}

glm::vec3 TransformHierarchy::getWorldPosition(TransformHandle handle) {
    const glm::mat4& world = getWorldMatrix(handle);
    return glm::vec3(world[3]);
}

void TransformHierarchy::updateWorldMatrices() {
    // Update all roots, which recursively updates children
    for (auto& root : roots_) {
        if (isValid(root)) {
            updateWorldMatrixRecursive(root.index);
        }
    }
}

void TransformHierarchy::setParent(TransformHandle handle, TransformHandle newParent) {
    if (!isValid(handle)) return;
    if (handle == newParent) return;  // Can't parent to self

    Node& node = nodes_[handle.index];

    // Skip if same parent
    if (node.parent == newParent) return;

    // Remove from old parent
    if (node.parent.isValid() && isValid(node.parent)) {
        auto& siblings = nodes_[node.parent.index].children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), handle), siblings.end());
    } else {
        // Was a root, remove from roots
        roots_.erase(std::remove(roots_.begin(), roots_.end(), handle), roots_.end());
    }

    // Set new parent
    node.parent = newParent;

    // Add to new parent or roots
    if (newParent.isValid() && isValid(newParent)) {
        nodes_[newParent.index].children.push_back(handle);
    } else {
        roots_.push_back(handle);
    }

    // Mark dirty (world transform changed)
    propagateDirty(handle.index);
}

TransformHandle TransformHierarchy::getParent(TransformHandle handle) const {
    if (!isValid(handle)) return TransformHandle::null();
    return nodes_[handle.index].parent;
}

const std::vector<TransformHandle>& TransformHierarchy::getChildren(TransformHandle handle) const {
    if (!isValid(handle)) return emptyChildren_;
    return nodes_[handle.index].children;
}

const std::string& TransformHierarchy::getName(TransformHandle handle) const {
    static const std::string empty;
    if (!isValid(handle)) return empty;
    return nodes_[handle.index].name;
}

void TransformHierarchy::setName(TransformHandle handle, const std::string& name) {
    if (!isValid(handle)) return;
    nodes_[handle.index].name = name;
}

TransformHandle TransformHierarchy::findByName(const std::string& name) const {
    for (size_t i = 0; i < nodes_.size(); ++i) {
        if (nodes_[i].active && nodes_[i].name == name) {
            TransformHandle h;
            h.index = static_cast<uint32_t>(i);
            h.generation = nodes_[i].generation;
            return h;
        }
    }
    return TransformHandle::null();
}

void TransformHierarchy::propagateDirty(uint32_t index) {
    Node& node = nodes_[index];
    if (node.dirty) return;  // Already dirty, children must be too

    node.dirty = true;

    // Propagate to all children
    for (auto& child : node.children) {
        if (isValid(child)) {
            propagateDirty(child.index);
        }
    }
}

void TransformHierarchy::updateWorldMatrixRecursive(uint32_t index) {
    // Used by batch updateWorldMatrices() - assumes we're called top-down from roots
    Node& node = nodes_[index];

    if (node.dirty) {
        if (node.parent.isValid() && isValid(node.parent)) {
            // Parent should already be up to date (we go top-down)
            node.worldMatrix = nodes_[node.parent.index].worldMatrix * node.local.toMatrix();
        } else {
            node.worldMatrix = node.local.toMatrix();
        }
        node.dirty = false;
    }

    // Recurse to children
    for (auto& child : node.children) {
        if (isValid(child)) {
            updateWorldMatrixRecursive(child.index);
        }
    }
}

uint32_t TransformHierarchy::allocateNode() {
    if (!freeList_.empty()) {
        uint32_t index = freeList_.back();
        freeList_.pop_back();
        return index;
    }

    uint32_t index = static_cast<uint32_t>(nodes_.size());
    nodes_.emplace_back();
    return index;
}
