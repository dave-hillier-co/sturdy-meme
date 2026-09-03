#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <optional>
#include <atomic>
#include <cstdint>
#include <memory>

/**
 * GPU Profiler using Vulkan timestamp queries.
 *
 * Measures GPU execution time for individual render passes and compute dispatches.
 * Uses double-buffered query pools to avoid pipeline stalls.
 *
 * Usage:
 *   std::optional<GpuProfiler> profiler;
 *   profiler.emplace(GpuProfiler::ConstructToken{}, raiiDevice, physicalDevice, framesInFlight);
 *   if (!profiler->isInitialized()) { profiler.reset(); }
 *   profiler->beginFrame(cmd, frameIndex);
 *   profiler->beginZone(cmd, "ShadowPass");
 *   // ... shadow pass commands ...
 *   profiler->endZone(cmd, "ShadowPass");
 *   profiler->endFrame(cmd, frameIndex);
 *   // Results available next frame via getResults()
 */
class GpuProfiler {
public:
    struct TimingResult {
        std::string name;
        float gpuTimeMs;       // GPU time in milliseconds
        float percentOfFrame;  // Percentage of total frame GPU time
    };

    struct FrameStats {
        float totalGpuTimeMs;
        std::vector<TimingResult> zones;
    };

    // Passkey for controlled in-place construction (e.g. std::optional::emplace)
    struct ConstructToken { explicit ConstructToken() = default; };

    /**
     * Construct a GPU profiler in place. Check isInitialized() afterwards: false means
     * query pool creation failed (fatal for profiling; drop the instance).
     * Note: If timestamps are unsupported, the profiler is valid but disabled.
     */
    GpuProfiler(ConstructToken, const vk::raii::Device& raiiDevice, vk::PhysicalDevice physicalDevice,
                uint32_t framesInFlight, uint32_t maxZones = 64);

    // RAII query pools are released by the member destructors
    ~GpuProfiler() = default;

    // Non-copyable, non-movable (atomics + RAII query pools; constructed in place)
    GpuProfiler(GpuProfiler&&) = delete;
    GpuProfiler& operator=(GpuProfiler&&) = delete;
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;

    /**
     * True when construction succeeded (query pools created, or timestamps unsupported
     * and profiling disabled). False only on a fatal creation error.
     */
    bool isInitialized() const { return initialized_; }

    /**
     * Call at the start of frame command buffer recording.
     * Resets query pool for this frame and writes initial timestamp.
     */
    void beginFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

    /**
     * Call at the end of frame command buffer recording.
     * Writes final timestamp and triggers result collection from previous frame.
     */
    void endFrame(vk::CommandBuffer cmd, uint32_t frameIndex);

    /**
     * Begin a named profiling zone.
     * Writes a timestamp at the top of the GPU pipeline.
     */
    void beginZone(vk::CommandBuffer cmd, const char* zoneName);

    /**
     * End a named profiling zone.
     * Writes a timestamp at the bottom of the GPU pipeline.
     */
    void endZone(vk::CommandBuffer cmd, const char* zoneName);

    /**
     * Get profiling results from the previous frame.
     * Results are only valid after at least 2 frames have been rendered.
     */
    const FrameStats& getResults() const { return lastFrameStats; }

    /**
     * Get smoothed profiling results (averaged over multiple frames).
     * More stable for display, handles zones that appear/disappear.
     */
    const FrameStats& getSmoothedResults() const { return smoothedStats; }

    /**
     * Check if profiling is enabled.
     */
    bool isEnabled() const { return enabled; }
    void setEnabled(bool e) { enabled = e; }

    /**
     * Get the list of available zone names (for GUI display).
     */
    const std::vector<std::string>& getZoneNames() const { return zoneNames; }

private:
    bool initInternal(const vk::raii::Device& raiiDevice, vk::PhysicalDevice physicalDevice,
                      uint32_t framesInFlight, uint32_t maxZones);

    static constexpr uint32_t QUERIES_PER_ZONE = 2;  // Start + end timestamp

    // Lock-free zone recording slot
    struct ZoneSlot {
        std::atomic<uint32_t> startQueryIndex{UINT32_MAX};  // UINT32_MAX = unused
        std::atomic<uint32_t> endQueryIndex{UINT32_MAX};
        const char* name = nullptr;  // Set atomically with startQueryIndex
    };

    vk::Device device{};
    std::vector<vk::raii::QueryPool> queryPools_;  // One per frame in flight (RAII)
    bool initialized_ = false;

    float timestampPeriod = 0.0f;  // Nanoseconds per timestamp tick
    uint32_t maxZones = 0;
    uint32_t framesInFlight = 0;
    bool enabled = true;

    // Current frame state - lock-free zone tracking
    std::atomic<uint32_t> currentQueryIndex{0};
    std::atomic<uint32_t> currentZoneSlot{0};  // Next available slot in current frame's zoneSlots
    uint32_t currentFrameIndex = 0;

    // Per-frame zone slot storage (one array per frame in flight)
    std::vector<std::unique_ptr<ZoneSlot[]>> zoneSlots_;  // [frameIndex][slotIndex]

    // Per-frame data for result collection (indexed by frameIndex, sized to framesInFlight)
    std::vector<uint32_t> frameQueryCounts;
    std::vector<uint32_t> frameZoneCounts;  // Number of zones recorded per frame

    // Results from previous frame
    FrameStats lastFrameStats;
    FrameStats smoothedStats;
    std::unordered_map<std::string, float> smoothedZoneTimes;  // Per-zone smoothed times
    std::unordered_set<std::string> seenZonesScratch_;  // Reused each frame to avoid per-frame allocation
    std::vector<std::string> zoneNames;

    // Smoothing factor (0.0 = no smoothing, 1.0 = infinite smoothing)
    static constexpr float SMOOTHING_FACTOR = 0.9f;
    float smoothedFrameTimeMs = 0.0f;

    // Frame start/end query indices
    uint32_t frameStartQuery = 0;
    uint32_t frameEndQuery = 0;

    void collectResults(uint32_t frameIndex);
};
