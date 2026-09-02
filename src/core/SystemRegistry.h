#pragma once

#include <entt/entt.hpp>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <vector>
#include <functional>

/**
 * SystemRegistry - Type-indexed singleton storage for renderer subsystems
 *
 * Uses EnTT's registry ctx() as a type-keyed store for unique_ptr<T>.
 * Replaces hand-written accessors in RendererSystems with a generic pattern:
 *
 *   registry.add<ShadowSystem>(std::move(shadow));
 *   auto& shadow = registry.get<ShadowSystem>();
 *   if (auto* terrain = registry.find<TerrainSystem>()) { ... }
 *
 * For multiple instances of the same type, use tag types:
 *
 *   struct RocksTag {};
 *   struct DetritusTag {};
 *   registry.add<ScatterSystem, RocksTag>(std::move(rocks));
 *   registry.add<ScatterSystem, DetritusTag>(std::move(detritus));
 *
 * Destruction happens in reverse registration order via destroyAll().
 *
 * Thread safety: registration (add/emplace) and erasure take an exclusive
 * lock; lookups (get/find/has) take a shared lock. Async init registers
 * systems on the main thread while loader workers may still be looking up
 * already-registered ones. Returning T& after the lock is released is safe:
 * Holder owns the unique_ptr, so the T* is stable across the underlying
 * dense-map rehash, and a system is only erased by destroyAll().
 */
class SystemRegistry {
public:
    SystemRegistry() = default;
    ~SystemRegistry() { destroyAll(); }

    // Non-copyable, non-movable (owns GPU resources via subsystems)
    SystemRegistry(const SystemRegistry&) = delete;
    SystemRegistry& operator=(const SystemRegistry&) = delete;
    SystemRegistry(SystemRegistry&&) = delete;
    SystemRegistry& operator=(SystemRegistry&&) = delete;

    /**
     * Register a system by transferring ownership of a unique_ptr.
     * Returns a reference to the stored system.
     */
    template<typename T, typename Tag = void>
    T& add(std::unique_ptr<T> system) {
        T* raw = system.get();
        std::unique_lock<std::shared_mutex> lock(mutex_);
        registry_.ctx().emplace<Holder<T, Tag>>(std::move(system));
        destructors_.push_back([this]() {
            // Unlink under the exclusive lock, but run the system's destructor
            // outside it so a destructor that looks up other systems cannot
            // deadlock against the registry lock.
            std::unique_ptr<T> victim;
            {
                std::unique_lock<std::shared_mutex> eraseLock(mutex_);
                victim = std::move(registry_.ctx().get<Holder<T, Tag>>().ptr);
                registry_.ctx().erase<Holder<T, Tag>>();
            }
        });
        return *raw;
    }

    /**
     * Construct a system in-place and register it.
     * Returns a reference to the newly created system.
     */
    template<typename T, typename Tag = void, typename... Args>
    T& emplace(Args&&... args) {
        return add<T, Tag>(std::make_unique<T>(std::forward<Args>(args)...));
    }

    /**
     * Get a reference to a registered system.
     * Asserts if the system is not registered.
     */
    template<typename T, typename Tag = void>
    T& get() {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto& holder = registry_.ctx().get<Holder<T, Tag>>();
        return *holder.ptr;
    }

    template<typename T, typename Tag = void>
    const T& get() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto& holder = registry_.ctx().get<const Holder<T, Tag>>();
        return *holder.ptr;
    }

    /**
     * Get a pointer to a registered system, or nullptr if not registered.
     */
    template<typename T, typename Tag = void>
    T* find() {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto* holder = registry_.ctx().find<Holder<T, Tag>>();
        return holder ? holder->ptr.get() : nullptr;
    }

    template<typename T, typename Tag = void>
    const T* find() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        const auto* holder = registry_.ctx().find<const Holder<T, Tag>>();
        return holder ? holder->ptr.get() : nullptr;
    }

    /**
     * Check if a system is registered.
     */
    template<typename T, typename Tag = void>
    bool has() const {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        return registry_.ctx().find<const Holder<T, Tag>>() != nullptr;
    }

    /**
     * Destroy all registered systems in reverse registration order.
     * Safe to call multiple times.
     */
    void destroyAll() {
        std::vector<std::function<void()>> destructors;
        {
            std::unique_lock<std::shared_mutex> lock(mutex_);
            destructors.swap(destructors_);
        }
        for (auto it = destructors.rbegin(); it != destructors.rend(); ++it) {
            (*it)();
        }
    }

private:
    template<typename T, typename Tag>
    struct Holder {
        std::unique_ptr<T> ptr;
        explicit Holder(std::unique_ptr<T> p) : ptr(std::move(p)) {}
    };

    mutable std::shared_mutex mutex_;
    entt::registry registry_;
    std::vector<std::function<void()>> destructors_;
};
