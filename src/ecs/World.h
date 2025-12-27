#pragma once

#include "Components.h"
#include <entt/entt.hpp>

// Thin wrapper around entt::registry providing convenient entity creation
// and typed queries. Keeps ECS usage simple and focused.
class World {
public:
    entt::registry& registry() { return registry_; }
    const entt::registry& registry() const { return registry_; }

    // Create player entity with all required components
    entt::entity createPlayer(const glm::vec3& position, float yaw = 0.0f) {
        auto entity = registry_.create();
        registry_.emplace<Transform>(entity, Transform{position, yaw});
        registry_.emplace<Velocity>(entity);
        registry_.emplace<PlayerTag>(entity);
        registry_.emplace<PlayerMovement>(entity);
        registry_.emplace<Grounded>(entity);
        return entity;
    }

    // Create a dynamic physics object
    entt::entity createDynamicObject(size_t sceneIndex, PhysicsBodyID bodyId) {
        auto entity = registry_.create();
        registry_.emplace<RenderableRef>(entity, RenderableRef{sceneIndex});
        registry_.emplace<PhysicsBody>(entity, PhysicsBody{bodyId});
        registry_.emplace<DynamicObject>(entity);
        return entity;
    }

    // Create emissive object with light
    entt::entity createEmissiveObject(size_t sceneIndex, PhysicsBodyID bodyId,
                                       const glm::vec3& color, float intensity) {
        auto entity = createDynamicObject(sceneIndex, bodyId);
        registry_.emplace<EmissiveLight>(entity, EmissiveLight{color, intensity});
        return entity;
    }

    // Find the player entity (returns null entity if not found)
    entt::entity findPlayer() const {
        auto view = registry_.view<PlayerTag>();
        return view.empty() ? entt::null : view.front();
    }

    // Get player transform (assumes player exists)
    Transform& getPlayerTransform() {
        return registry_.get<Transform>(findPlayer());
    }

    const Transform& getPlayerTransform() const {
        return registry_.get<Transform>(findPlayer());
    }

    // Get player movement component
    PlayerMovement& getPlayerMovement() {
        return registry_.get<PlayerMovement>(findPlayer());
    }

    // Check if player is grounded
    bool isPlayerGrounded() const {
        auto player = findPlayer();
        return player != entt::null && registry_.all_of<Grounded>(player);
    }

    void setPlayerGrounded(bool grounded) {
        auto player = findPlayer();
        if (player == entt::null) return;

        if (grounded && !registry_.all_of<Grounded>(player)) {
            registry_.emplace<Grounded>(player);
        } else if (!grounded && registry_.all_of<Grounded>(player)) {
            registry_.remove<Grounded>(player);
        }
    }

private:
    entt::registry registry_;
};
