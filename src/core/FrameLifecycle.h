#pragma once

// ============================================================================
// FrameLifecycle.h - Frame lifecycle phase management
// ============================================================================
//
// Defines clear phases for frame processing, ensuring consistent timing
// for buffer advancement, updates, and cleanup across all subsystems.
//
// Phase Order:
// 1. BeginFrame    - Advance double-buffers, reset per-frame state
// 2. Update        - Compute simulations (wind, weather, grass compute)
// 3. Record        - Record command buffers (already handled by FrameGraph)
// 4. Submit        - Submit commands to GPU (handled by Renderer)
// 5. EndFrame      - Post-submit cleanup, diagnostics, profiling
//
// Usage:
//   FrameLifecycle lifecycle;
//
//   // Register callbacks during initialization
//   lifecycle.onBeginFrame([&grass]() { grass.advanceBufferSet(); });
//   lifecycle.onUpdate([&wind](float dt) { wind.simulate(dt); });
//   lifecycle.onEndFrame([&profiler]() { profiler.endFrame(); });
//
//   // In render loop
//   lifecycle.beginFrame();
//   lifecycle.update(deltaTime);
//   // ... record commands via FrameGraph ...
//   lifecycle.endFrame();
//

#include <functional>
#include <vector>
#include <cstdint>

/**
 * Frame lifecycle phase enumeration
 */
enum class FramePhase {
    BeginFrame,  // Advance double-buffers, reset state
    Update,      // Compute simulations before rendering
    Record,      // Command buffer recording (FrameGraph handles this)
    Submit,      // GPU submission (Renderer handles this)
    EndFrame     // Post-submit cleanup and diagnostics
};

/**
 * FrameLifecycle - Manages per-frame phase callbacks
 *
 * Provides a central place for systems to register phase callbacks,
 * ensuring consistent ordering across all frame operations.
 */
class FrameLifecycle {
public:
    using BeginFrameCallback = std::function<void(uint32_t frameIndex)>;
    using UpdateCallback = std::function<void(float deltaTime, uint32_t frameIndex)>;
    using EndFrameCallback = std::function<void(uint32_t frameIndex)>;

    FrameLifecycle() = default;
    ~FrameLifecycle() = default;

    // Non-copyable (callbacks may capture references)
    FrameLifecycle(const FrameLifecycle&) = delete;
    FrameLifecycle& operator=(const FrameLifecycle&) = delete;

    // Move constructible
    FrameLifecycle(FrameLifecycle&&) = default;
    FrameLifecycle& operator=(FrameLifecycle&&) = default;

    // ========================================================================
    // Callback registration
    // ========================================================================

    /**
     * Register a BeginFrame callback.
     * Called at the start of each frame, before any updates or recording.
     * Use for: advancing double-buffers, resetting per-frame state
     */
    void onBeginFrame(BeginFrameCallback callback) {
        beginFrameCallbacks_.push_back(std::move(callback));
    }

    /**
     * Register an Update callback.
     * Called after BeginFrame, before command recording.
     * Use for: compute simulations, wind, weather, grass animation
     */
    void onUpdate(UpdateCallback callback) {
        updateCallbacks_.push_back(std::move(callback));
    }

    /**
     * Register an EndFrame callback.
     * Called after GPU submission and presentation.
     * Use for: cleanup, diagnostics, profiling end
     */
    void onEndFrame(EndFrameCallback callback) {
        endFrameCallbacks_.push_back(std::move(callback));
    }

    // ========================================================================
    // Phase execution
    // ========================================================================

    /**
     * Execute BeginFrame phase.
     * Advances buffer sets and resets per-frame state.
     */
    void beginFrame(uint32_t frameIndex) {
        currentFrameIndex_ = frameIndex;
        for (auto& callback : beginFrameCallbacks_) {
            callback(frameIndex);
        }
    }

    /**
     * Execute Update phase.
     * Runs compute simulations and pre-render updates.
     */
    void update(float deltaTime) {
        for (auto& callback : updateCallbacks_) {
            callback(deltaTime, currentFrameIndex_);
        }
    }

    /**
     * Execute EndFrame phase.
     * Handles post-submit cleanup and diagnostics.
     */
    void endFrame() {
        for (auto& callback : endFrameCallbacks_) {
            callback(currentFrameIndex_);
        }
    }

    // ========================================================================
    // State
    // ========================================================================

    uint32_t currentFrameIndex() const { return currentFrameIndex_; }

    /**
     * Clear all registered callbacks.
     * Use during shutdown or reinitialization.
     */
    void clear() {
        beginFrameCallbacks_.clear();
        updateCallbacks_.clear();
        endFrameCallbacks_.clear();
    }

private:
    std::vector<BeginFrameCallback> beginFrameCallbacks_;
    std::vector<UpdateCallback> updateCallbacks_;
    std::vector<EndFrameCallback> endFrameCallbacks_;

    uint32_t currentFrameIndex_ = 0;
};

// ============================================================================
// FrameLifecycleRegistrar - Helper for RAII callback registration
// ============================================================================
//
// Automatically unregisters callbacks when the registrar goes out of scope.
// Use when you need scoped callback lifetime.
//
// Note: Currently FrameLifecycle doesn't support individual callback removal,
// so this is a placeholder for future enhancement if needed.
