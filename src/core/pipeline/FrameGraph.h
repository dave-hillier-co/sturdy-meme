#pragma once

#include <vulkan/vulkan.hpp>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

#include "core/FrameContext.h"

class TaskScheduler;
class TaskGroup;
class ThreadedCommandPool;
struct QueueSubmitDiagnostics;

/**
 * FrameGraph - Dependency-driven render pass scheduling.
 *
 * Implements the frame graph concept from the video:
 * "each node in the graph will have its own dependencies and priorities
 * allowing for other tasks to branch forward from each node until the frame is complete"
 *
 * The frame graph:
 * 1. Defines render passes as nodes with dependencies
 * 2. Compiles to find parallelization opportunities
 * 3. Executes passes in dependency order, running independent passes in parallel
 *
 * Example graph:
 *   ComputeStage ──┬──> ShadowPass ──> HDRPass ──> PostProcess
 *                  └──> FroxelStage ─┘
 *
 * Usage:
 *   FrameGraph graph;
 *   auto compute = graph.addPass("Compute", [](FrameContext& ctx) { ... });
 *   auto shadow = graph.addPass("Shadow", [](FrameContext& ctx) { ... });
 *   auto hdr = graph.addPass("HDR", [](FrameContext& ctx) { ... });
 *
 *   graph.addDependency(shadow, compute);  // Shadow depends on Compute
 *   graph.addDependency(hdr, shadow);      // HDR depends on Shadow
 *
 *   graph.compile();
 *   graph.execute(context);
 */
class FrameGraph {
public:
    using PassId = uint32_t;
    static constexpr PassId INVALID_PASS = UINT32_MAX;

    // Use the unified FrameContext from FrameContext.h
    // Legacy alias for backward compatibility
    using RenderContext = FrameContext;

    using PassFunction = std::function<void(FrameContext&)>;

    /**
     * Secondary recording function for parallel command buffer recording.
     * Called for each secondary buffer slot with a thread-allocated command buffer.
     * @param ctx The frame context (cmd field is the secondary buffer)
     * @param slotIndex Which slot is being recorded (0 to numSlots-1)
     */
    using SecondaryRecordFunction = std::function<void(FrameContext& ctx, uint32_t slotIndex)>;

    /**
     * Pass configuration for parallel recording.
     */
    struct PassConfig {
        std::string name;
        PassFunction execute;

        // If true, this pass can record using secondary command buffers
        // and be parallelized with other secondary-capable passes at the same level
        bool canUseSecondary = false;

        // If true, this pass must run on the main thread
        bool mainThreadOnly = true;

        // Priority within the same dependency level (higher = earlier)
        int32_t priority = 0;

        // Number of secondary buffers to allocate (for parallel recording)
        // Only used when canUseSecondary = true
        uint32_t secondarySlots = 0;

        // Function to record secondary command buffers in parallel
        // Called once per slot, potentially from different threads
        // Only used when canUseSecondary = true and secondarySlots > 0
        SecondaryRecordFunction secondaryRecord;
    };

    FrameGraph() = default;
    ~FrameGraph() = default;

    /**
     * Add a render pass to the graph.
     * @param name Debug name for the pass
     * @param execute Function to execute for this pass
     * @return Pass ID for adding dependencies
     */
    PassId addPass(const std::string& name, PassFunction execute);

    /**
     * Add a pass with full configuration.
     */
    PassId addPass(const PassConfig& config);

    /**
     * Add a dependency: 'from' must complete before 'to' can start.
     * @param from Pass that must complete first
     * @param to Pass that depends on 'from'
     */
    void addDependency(PassId from, PassId to);

    /**
     * Remove a pass from the graph.
     */
    void removePass(PassId id);

    /**
     * Enable or disable a pass.
     * Disabled passes are skipped during execution.
     */
    void setPassEnabled(PassId id, bool enabled);

    /**
     * Check if a pass is enabled.
     */
    bool isPassEnabled(PassId id) const;

    /**
     * Compile the graph for execution.
     * Performs topological sort and identifies parallelization opportunities.
     * Must be called after adding all passes and dependencies.
     */
    bool compile();

    /**
     * Execute all enabled passes in dependency order.
     * @param context Frame context for the current frame
     * @param scheduler Optional task scheduler for parallel execution
     */
    void execute(FrameContext& context, TaskScheduler* scheduler = nullptr);

    /**
     * Get pass by name.
     */
    PassId getPass(const std::string& name) const;

    /**
     * Get pass count.
     */
    size_t getPassCount() const { return passes_.size(); }

    /**
     * Get the number of execution levels (for debugging).
     */
    size_t getLevelCount() const { return executionLevels_.size(); }

    /**
     * Clear all passes and dependencies.
     */
    void clear();

    /**
     * Check if the graph has been compiled.
     */
    bool isCompiled() const { return compiled_; }

    /**
     * Get debug string representation of the graph.
     */
    std::string debugString() const;

private:
    struct Pass {
        PassId id = INVALID_PASS;
        PassConfig config;
        std::vector<PassId> dependencies;
        std::vector<PassId> dependents;
        bool enabled = true;
    };

    // Topological sort helper
    bool topologicalSort(std::vector<std::vector<PassId>>& levels);

    // Execute a pass using secondary command buffers for parallel recording
    void executeWithSecondaryBuffers(FrameContext& context, const Pass& pass, TaskScheduler* scheduler);

    std::vector<Pass> passes_;
    std::unordered_map<std::string, PassId> nameToId_;

    // Compiled execution order: levels[level][passIndex]
    // Passes in the same level can potentially run in parallel
    std::vector<std::vector<PassId>> executionLevels_;

    PassId nextPassId_ = 0;
    bool compiled_ = false;
};
