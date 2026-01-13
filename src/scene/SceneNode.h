#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <string>
#include <memory>
#include <functional>

/**
 * SceneNode - Hierarchical transform node for scene graph
 *
 * Represents a single entity in the scene with position, rotation, and scale.
 * Can have children forming a transform hierarchy (e.g., player -> weapon).
 * Uses composition - does not own or manage rendering/physics directly.
 *
 * Common uses:
 * - Player character with attachments (cape, weapon)
 * - Camera with offset
 * - Light sources
 * - Individual scene props
 * - Animated skeleton bones
 */
class SceneNode {
public:
    SceneNode() = default;
    explicit SceneNode(const std::string& name) : name_(name) {}

    // ========================================================================
    // Transform accessors (local space)
    // ========================================================================

    void setPosition(const glm::vec3& pos) { position_ = pos; dirty_ = true; }
    void setRotation(const glm::quat& rot) { rotation_ = rot; dirty_ = true; }
    void setScale(float s) { scale_ = glm::vec3(s); dirty_ = true; }
    void setScale(const glm::vec3& s) { scale_ = s; dirty_ = true; }

    const glm::vec3& getPosition() const { return position_; }
    const glm::quat& getRotation() const { return rotation_; }
    const glm::vec3& getScale() const { return scale_; }

    // Convenience setters
    void setYRotation(float yRadians) {
        rotation_ = glm::angleAxis(yRadians, glm::vec3(0.0f, 1.0f, 0.0f));
        dirty_ = true;
    }

    void setEulerAngles(const glm::vec3& euler) {
        glm::quat qYaw = glm::angleAxis(euler.y, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(euler.x, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat qRoll = glm::angleAxis(euler.z, glm::vec3(0.0f, 0.0f, 1.0f));
        rotation_ = qYaw * qPitch * qRoll;
        dirty_ = true;
    }

    // ========================================================================
    // Transform matrix computation
    // ========================================================================

    // Get local transform matrix (position * rotation * scale)
    glm::mat4 getLocalMatrix() const {
        if (dirty_) {
            updateLocalMatrix();
        }
        return localMatrix_;
    }

    // Get world transform matrix (parent chain * local)
    glm::mat4 getWorldMatrix() const {
        if (parent_) {
            return parent_->getWorldMatrix() * getLocalMatrix();
        }
        return getLocalMatrix();
    }

    // Get world position (extracts from world matrix)
    glm::vec3 getWorldPosition() const {
        glm::mat4 world = getWorldMatrix();
        return glm::vec3(world[3]);
    }

    // ========================================================================
    // Hierarchy management
    // ========================================================================

    void setParent(SceneNode* parent) {
        if (parent_ == parent) return;

        // Remove from old parent
        if (parent_) {
            auto& siblings = parent_->children_;
            siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        }

        parent_ = parent;

        // Add to new parent
        if (parent_) {
            parent_->children_.push_back(this);
        }
    }

    SceneNode* getParent() const { return parent_; }
    const std::vector<SceneNode*>& getChildren() const { return children_; }

    // ========================================================================
    // Identification
    // ========================================================================

    void setName(const std::string& name) { name_ = name; }
    const std::string& getName() const { return name_; }

    // Find child by name (non-recursive)
    SceneNode* findChild(const std::string& name) {
        for (auto* child : children_) {
            if (child->name_ == name) return child;
        }
        return nullptr;
    }

    // ========================================================================
    // Movement helpers
    // ========================================================================

    void translate(const glm::vec3& delta) {
        position_ += delta;
        dirty_ = true;
    }

    void translateLocal(const glm::vec3& delta) {
        // Move in local space (relative to current rotation)
        position_ += glm::vec3(glm::mat4_cast(rotation_) * glm::vec4(delta, 0.0f));
        dirty_ = true;
    }

    void rotate(const glm::quat& delta) {
        rotation_ = delta * rotation_;
        dirty_ = true;
    }

    void rotateY(float radians) {
        rotate(glm::angleAxis(radians, glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    // Look at a target position (Y-up)
    void lookAt(const glm::vec3& target, const glm::vec3& up = glm::vec3(0.0f, 1.0f, 0.0f)) {
        glm::vec3 forward = glm::normalize(target - position_);
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        glm::vec3 correctedUp = glm::cross(forward, right);

        glm::mat3 rotMat(right, correctedUp, forward);
        rotation_ = glm::quat_cast(rotMat);
        dirty_ = true;
    }

    // ========================================================================
    // Forward/Right/Up vectors
    // ========================================================================

    glm::vec3 getForward() const {
        return glm::vec3(glm::mat4_cast(rotation_) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f));
    }

    glm::vec3 getRight() const {
        return glm::vec3(glm::mat4_cast(rotation_) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
    }

    glm::vec3 getUp() const {
        return glm::vec3(glm::mat4_cast(rotation_) * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
    }

private:
    void updateLocalMatrix() const {
        localMatrix_ = glm::translate(glm::mat4(1.0f), position_);
        localMatrix_ = localMatrix_ * glm::mat4_cast(rotation_);
        localMatrix_ = glm::scale(localMatrix_, scale_);
        dirty_ = false;
    }

    std::string name_;
    glm::vec3 position_{0.0f};
    glm::quat rotation_{1.0f, 0.0f, 0.0f, 0.0f};  // Identity
    glm::vec3 scale_{1.0f};

    mutable glm::mat4 localMatrix_{1.0f};
    mutable bool dirty_ = true;

    SceneNode* parent_ = nullptr;
    std::vector<SceneNode*> children_;
};

/**
 * ScopedSceneNode - RAII wrapper for SceneNode ownership
 *
 * Use when you need to own a SceneNode and ensure proper cleanup.
 */
class ScopedSceneNode {
public:
    ScopedSceneNode() : node_(std::make_unique<SceneNode>()) {}
    explicit ScopedSceneNode(const std::string& name) : node_(std::make_unique<SceneNode>(name)) {}

    SceneNode* operator->() { return node_.get(); }
    const SceneNode* operator->() const { return node_.get(); }
    SceneNode& operator*() { return *node_; }
    const SceneNode& operator*() const { return *node_; }
    SceneNode* get() { return node_.get(); }
    const SceneNode* get() const { return node_.get(); }

private:
    std::unique_ptr<SceneNode> node_;
};
