#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

/**
 * SceneObjectInstance - Unified transform representation for scene objects
 *
 * Uses quaternion for rotation to support both Y-axis only rotation (rocks)
 * and full 3D rotation (detritus, future objects). Provides helper methods
 * for common rotation patterns.
 */
struct SceneObjectInstance {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};  // Identity quaternion (w, x, y, z)
    float scale{1.0f};
    uint32_t meshVariation{0};

    SceneObjectInstance() = default;

    SceneObjectInstance(const glm::vec3& pos, const glm::quat& rot, float s, uint32_t mesh)
        : position(pos), rotation(rot), scale(s), meshVariation(mesh) {}

    // Convenience: Create with Y-axis rotation only (radians)
    static SceneObjectInstance withYRotation(const glm::vec3& pos, float yRotation, float scale, uint32_t mesh) {
        return SceneObjectInstance(pos, glm::angleAxis(yRotation, glm::vec3(0.0f, 1.0f, 0.0f)), scale, mesh);
    }

    // Convenience: Create with Euler angles (pitch, yaw, roll in radians)
    static SceneObjectInstance withEulerAngles(const glm::vec3& pos, const glm::vec3& eulerAngles, float scale, uint32_t mesh) {
        // Convert Euler angles to quaternion (Y-X-Z order: yaw, pitch, roll)
        glm::quat qYaw = glm::angleAxis(eulerAngles.y, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat qPitch = glm::angleAxis(eulerAngles.x, glm::vec3(1.0f, 0.0f, 0.0f));
        glm::quat qRoll = glm::angleAxis(eulerAngles.z, glm::vec3(0.0f, 0.0f, 1.0f));
        return SceneObjectInstance(pos, qYaw * qPitch * qRoll, scale, mesh);
    }

    // Get the Y-axis rotation in radians (for backward compatibility)
    float getYRotation() const {
        // Extract yaw from quaternion
        glm::vec3 euler = glm::eulerAngles(rotation);
        return euler.y;
    }

    // Get Euler angles (pitch, yaw, roll) in radians
    glm::vec3 getEulerAngles() const {
        return glm::eulerAngles(rotation);
    }

    // Build 4x4 transform matrix (translate * rotate * scale)
    glm::mat4 getTransformMatrix() const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::scale(transform, glm::vec3(scale));
        return transform;
    }

    // Build transform with additional tilt (useful for rocks settling on terrain)
    glm::mat4 getTransformMatrixWithTilt(float tiltX, float tiltZ) const {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        transform = transform * glm::mat4_cast(rotation);
        transform = glm::rotate(transform, tiltX, glm::vec3(1.0f, 0.0f, 0.0f));
        transform = glm::rotate(transform, tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
        transform = glm::scale(transform, glm::vec3(scale));
        return transform;
    }
};
