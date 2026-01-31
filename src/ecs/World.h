#pragma once

#include <entt/entt.hpp>
#include <vector>
#include <functional>

namespace ecs {

// Entity handle - strongly typed wrapper around entt::entity
using Entity = entt::entity;
constexpr Entity NullEntity = entt::null;

// World wraps entt::registry, providing a clean interface for ECS operations.
// This allows future changes to the underlying ECS library without affecting client code.
class World {
public:
    World() = default;
    ~World() = default;

    // Non-copyable, moveable
    World(const World&) = delete;
    World& operator=(const World&) = delete;
    World(World&&) = default;
    World& operator=(World&&) = default;

    // Entity creation and destruction
    [[nodiscard]] Entity create() {
        return registry_.create();
    }

    void destroy(Entity entity) {
        registry_.destroy(entity);
    }

    [[nodiscard]] bool valid(Entity entity) const {
        return registry_.valid(entity);
    }

    // Component operations
    template<typename Component, typename... Args>
    decltype(auto) add(Entity entity, Args&&... args) {
        return registry_.emplace<Component>(entity, std::forward<Args>(args)...);
    }

    template<typename Component>
    void remove(Entity entity) {
        registry_.remove<Component>(entity);
    }

    template<typename Component>
    [[nodiscard]] bool has(Entity entity) const {
        return registry_.all_of<Component>(entity);
    }

    template<typename... Components>
    [[nodiscard]] bool hasAll(Entity entity) const {
        return registry_.all_of<Components...>(entity);
    }

    template<typename... Components>
    [[nodiscard]] bool hasAny(Entity entity) const {
        return registry_.any_of<Components...>(entity);
    }

    template<typename Component>
    [[nodiscard]] Component& get(Entity entity) {
        return registry_.get<Component>(entity);
    }

    template<typename Component>
    [[nodiscard]] const Component& get(Entity entity) const {
        return registry_.get<Component>(entity);
    }

    template<typename Component>
    [[nodiscard]] Component* tryGet(Entity entity) {
        return registry_.try_get<Component>(entity);
    }

    template<typename Component>
    [[nodiscard]] const Component* tryGet(Entity entity) const {
        return registry_.try_get<Component>(entity);
    }

    // Query interface - returns a view that can be iterated
    template<typename... Components>
    [[nodiscard]] auto view() {
        return registry_.view<Components...>();
    }

    template<typename... Components>
    [[nodiscard]] auto view() const {
        return registry_.view<Components...>();
    }

    // Group interface - for efficient iteration of related components
    template<typename... Owned, typename... Get, typename... Exclude>
    [[nodiscard]] auto group(entt::get_t<Get...> = {}, entt::exclude_t<Exclude...> = {}) {
        return registry_.group<Owned...>(entt::get<Get...>, entt::exclude<Exclude...>);
    }

    // Count entities with specific components
    template<typename... Components>
    [[nodiscard]] size_t count() const {
        size_t n = 0;
        for ([[maybe_unused]] auto entity : registry_.view<Components...>()) {
            ++n;
        }
        return n;
    }

    // Iteration helpers
    template<typename... Components, typename Func>
    void each(Func&& func) {
        registry_.view<Components...>().each(std::forward<Func>(func));
    }

    template<typename... Components, typename Func>
    void each(Func&& func) const {
        registry_.view<Components...>().each(std::forward<Func>(func));
    }

    // Access to underlying registry (for advanced use cases)
    [[nodiscard]] entt::registry& registry() { return registry_; }
    [[nodiscard]] const entt::registry& registry() const { return registry_; }

    // Clear all entities and components
    void clear() {
        registry_.clear();
    }

    // Number of active entities
    [[nodiscard]] size_t size() const {
        return registry_.storage<Entity>()->size();
    }

    [[nodiscard]] bool empty() const {
        return size() == 0;
    }

private:
    entt::registry registry_;
};

} // namespace ecs
