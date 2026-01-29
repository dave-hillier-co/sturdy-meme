#pragma once

// ============================================================================
// OptionalSystem.h - Type aliases for optional and required subsystems
// ============================================================================
//
// Provides semantic type aliases to make system ownership intent clear.
// std::unique_ptr already provides all the functionality needed for optional
// systems (bool conversion, arrow operator, move semantics).
//
// Usage:
//   OptionalSystem<TreeSystem> tree_;      // May be null - just unique_ptr
//   RequiredSystem<TerrainSystem> terrain_; // Must be valid - asserts on access
//
//   if (tree_) tree_->render(cmd);          // Check optional before use
//   terrain_->recordDraw(cmd);              // No check needed for required
//

#include <memory>
#include <cassert>

// ============================================================================
// OptionalSystem - Type alias for std::unique_ptr
// ============================================================================
// std::unique_ptr already has:
// - operator bool() for null checks
// - operator->() and operator*() for access
// - Move semantics (no copy)
// - get(), reset(), release()
//
// This alias just documents intent - the system may not exist.

template<typename T>
using OptionalSystem = std::unique_ptr<T>;

// ============================================================================
// RequiredSystem - Wrapper that asserts on null access
// ============================================================================
// Unlike OptionalSystem, this wrapper asserts if accessed when null.
// Use for systems that must always be initialized before use.

template<typename T>
class RequiredSystem {
public:
    RequiredSystem() = default;
    explicit RequiredSystem(std::unique_ptr<T> ptr) : ptr_(std::move(ptr)) {}

    // Move only
    RequiredSystem(RequiredSystem&&) = default;
    RequiredSystem& operator=(RequiredSystem&&) = default;
    RequiredSystem(const RequiredSystem&) = delete;
    RequiredSystem& operator=(const RequiredSystem&) = delete;

    // Bool conversion (for consistency, though you shouldn't need to check)
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Access with assert - these should never be null after init
    T* operator->() noexcept {
        assert(ptr_ && "RequiredSystem accessed before initialization");
        return ptr_.get();
    }
    const T* operator->() const noexcept {
        assert(ptr_ && "RequiredSystem accessed before initialization");
        return ptr_.get();
    }

    T& operator*() noexcept {
        assert(ptr_ && "RequiredSystem accessed before initialization");
        return *ptr_;
    }
    const T& operator*() const noexcept {
        assert(ptr_ && "RequiredSystem accessed before initialization");
        return *ptr_;
    }

    // Raw access for legacy APIs
    T* get() noexcept { return ptr_.get(); }
    const T* get() const noexcept { return ptr_.get(); }

    void reset(std::unique_ptr<T> ptr) { ptr_ = std::move(ptr); }

private:
    std::unique_ptr<T> ptr_;
};
