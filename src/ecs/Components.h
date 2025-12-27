#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <entt/entt.hpp>
#include "PhysicsSystem.h"

// Core transform component - position and rotation for entities
struct Transform {
    glm::vec3 position{0.0f};
    float yaw{0.0f};  // Horizontal rotation in degrees

    glm::vec3 getForward() const {
        float rad = glm::radians(yaw);
        return glm::vec3(sin(rad), 0.0f, cos(rad));
    }

    glm::vec3 getRight() const {
        float rad = glm::radians(yaw + 90.0f);
        return glm::vec3(sin(rad), 0.0f, cos(rad));
    }
};

// Velocity for physics-driven entities
struct Velocity {
    glm::vec3 linear{0.0f};
};

// Physics body reference - links entity to Jolt physics body
struct PhysicsBody {
    PhysicsBodyID id = INVALID_BODY_ID;
};

// Renderable reference - links entity to scene object index (during migration)
// Eventually this could hold mesh/texture references directly
struct RenderableRef {
    size_t sceneIndex = 0;
};

// Tag: marks entity as the player
struct PlayerTag {};

// Tag: marks entity as grounded (on floor/terrain)
struct Grounded {};

// Player-specific components
struct PlayerMovement {
    static constexpr float CAPSULE_HEIGHT = 1.8f;
    static constexpr float CAPSULE_RADIUS = 0.3f;
    bool orientationLocked = false;
    float lockedYaw = 0.0f;

    glm::vec3 getFocusPoint(const glm::vec3& position) const {
        return position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.85f, 0.0f);
    }

    glm::mat4 getModelMatrix(const Transform& transform) const {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, transform.position + glm::vec3(0.0f, CAPSULE_HEIGHT * 0.5f, 0.0f));
        float effectiveYaw = orientationLocked ? lockedYaw : transform.yaw;
        model = glm::rotate(model, glm::radians(effectiveYaw), glm::vec3(0.0f, 1.0f, 0.0f));
        return model;
    }
};

// Dynamic scene object that has physics simulation
struct DynamicObject {};

// Emissive light source that follows an entity
struct EmissiveLight {
    glm::vec3 color{1.0f};
    float intensity{1.0f};
};
