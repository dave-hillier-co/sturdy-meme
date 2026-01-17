#pragma once

#include "Transform.h"

/**
 * SceneObjectInstance - Static scene object with transform + mesh variation
 *
 * Uses Transform for position/rotation/scale, adds meshVariation for
 * selecting which mesh variant to render (e.g., different rock shapes).
 */
struct SceneObjectInstance {
    Transform transform;
    uint32_t meshVariation{0};

    SceneObjectInstance() = default;

    SceneObjectInstance(const glm::vec3& pos, const glm::quat& rot, float s, uint32_t mesh)
        : transform(pos, rot, s), meshVariation(mesh) {}

    // Convenience: Create with Y-axis rotation only (radians)
    static SceneObjectInstance withYRotation(const glm::vec3& pos, float yRotation, float scale, uint32_t mesh) {
        return SceneObjectInstance(pos, Transform::yRotation(yRotation), scale, mesh);
    }

    // Convenience: Create with Euler angles (pitch, yaw, roll in radians)
    static SceneObjectInstance withEulerAngles(const glm::vec3& pos, const glm::vec3& eulerAngles, float scale, uint32_t mesh) {
        return SceneObjectInstance(pos, Transform::fromEuler(eulerAngles), scale, mesh);
    }

    // Accessors for backward compatibility
    const glm::vec3& position() const { return transform.position; }
    const glm::quat& rotation() const { return transform.rotation; }
    float scale() const { return transform.scale.x; }  // Uniform scale

    // Mutators
    void setPosition(const glm::vec3& pos) { transform.position = pos; }
    void setRotation(const glm::quat& rot) { transform.rotation = rot; }
    void setScale(float s) { transform.scale = glm::vec3(s); }

    // Build 4x4 transform matrix
    glm::mat4 getTransformMatrix() const {
        return transform.toMatrix();
    }

    // Build transform with additional tilt (useful for rocks settling on terrain)
    glm::mat4 getTransformMatrixWithTilt(float tiltX, float tiltZ) const {
        glm::mat4 m = glm::translate(glm::mat4(1.0f), transform.position);
        m = m * glm::mat4_cast(transform.rotation);
        m = glm::rotate(m, tiltX, glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::rotate(m, tiltZ, glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::scale(m, transform.scale);
        return m;
    }
};
