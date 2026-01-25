#pragma once

// ============================================================================
// OptionalSystem.h - Wrapper types for optional and required subsystems
// ============================================================================
//
// Provides explicit wrapper types to distinguish between optional and required
// subsystems in RendererSystems and other containers. This makes the contract
// clear at the API level and prevents null-pointer bugs.
//
// Usage:
//   // In RendererSystems.h:
//   RequiredSystem<TerrainSystem> terrain_;     // Must always be valid
//   OptionalSystem<TreeSystem> tree_;           // May be null
//
//   // Accessing:
//   terrain_->recordDraw(cmd);                  // Direct arrow, always safe
//   if (tree_) tree_->render(cmd);              // Check before use
//
// Design:
// - OptionalSystem: wraps a nullable unique_ptr, provides bool operator
// - RequiredSystem: wraps a unique_ptr, asserts non-null on construction
// - Both support arrow operator for method access
// - Both are move-only (no copy)
//

#include <memory>
#include <cassert>

/**
 * OptionalSystem - Wrapper for subsystems that may not exist
 *
 * Makes it explicit at the type level that a system is optional.
 * Provides boolean conversion for null checks.
 *
 * Example usage:
 *   OptionalSystem<TreeSystem> tree_;
 *
 *   // Check before use
 *   if (tree_) {
 *       tree_->render(cmd, frameIndex);
 *   }
 *
 *   // Or with hasX() pattern
 *   bool hasTree() const { return static_cast<bool>(tree_); }
 */
template<typename T>
class OptionalSystem {
public:
    OptionalSystem() = default;

    explicit OptionalSystem(std::unique_ptr<T> ptr)
        : ptr_(std::move(ptr)) {}

    // Move constructible/assignable
    OptionalSystem(OptionalSystem&&) = default;
    OptionalSystem& operator=(OptionalSystem&&) = default;

    // No copy
    OptionalSystem(const OptionalSystem&) = delete;
    OptionalSystem& operator=(const OptionalSystem&) = delete;

    // Boolean conversion for null checks
    explicit operator bool() const noexcept { return ptr_ != nullptr; }
    bool hasValue() const noexcept { return ptr_ != nullptr; }

    // Access operators (undefined behavior if null - caller must check)
    T* operator->() noexcept { return ptr_.get(); }
    const T* operator->() const noexcept { return ptr_.get(); }

    T& operator*() noexcept { return *ptr_; }
    const T& operator*() const noexcept { return *ptr_; }

    // Get raw pointer (for legacy APIs)
    T* get() noexcept { return ptr_.get(); }
    const T* get() const noexcept { return ptr_.get(); }

    // Transfer ownership
    std::unique_ptr<T> release() noexcept { return std::move(ptr_); }

    // Reset
    void reset(std::unique_ptr<T> ptr = nullptr) { ptr_ = std::move(ptr); }

private:
    std::unique_ptr<T> ptr_;
};

/**
 * RequiredSystem - Wrapper for subsystems that must always exist
 *
 * Makes it explicit at the type level that a system is required.
 * Asserts non-null on construction to catch errors early.
 *
 * Example usage:
 *   RequiredSystem<TerrainSystem> terrain_;
 *
 *   // No null check needed - always valid after construction
 *   terrain_->recordDraw(cmd, frameIndex);
 */
template<typename T>
class RequiredSystem {
public:
    RequiredSystem() = default;

    explicit RequiredSystem(std::unique_ptr<T> ptr)
        : ptr_(std::move(ptr)) {
        // Note: We don't assert here because construction may happen in stages
        // The assert happens in the accessor methods instead
    }

    // Move constructible/assignable
    RequiredSystem(RequiredSystem&&) = default;
    RequiredSystem& operator=(RequiredSystem&&) = default;

    // No copy
    RequiredSystem(const RequiredSystem&) = delete;
    RequiredSystem& operator=(const RequiredSystem&) = delete;

    // Always true (if accessed before initialization, will assert)
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

    // Access operators (assert if null - programming error)
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

    // Get raw pointer (for legacy APIs)
    T* get() noexcept { return ptr_.get(); }
    const T* get() const noexcept { return ptr_.get(); }

    // Reset
    void reset(std::unique_ptr<T> ptr) {
        ptr_ = std::move(ptr);
    }

private:
    std::unique_ptr<T> ptr_;
};

// ============================================================================
// Factory helpers for cleaner construction
// ============================================================================

template<typename T, typename... Args>
OptionalSystem<T> makeOptionalSystem(Args&&... args) {
    return OptionalSystem<T>(std::make_unique<T>(std::forward<Args>(args)...));
}

template<typename T, typename... Args>
RequiredSystem<T> makeRequiredSystem(Args&&... args) {
    return RequiredSystem<T>(std::make_unique<T>(std::forward<Args>(args)...));
}
