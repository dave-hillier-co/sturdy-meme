#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <functional>

struct WatershedMetricsConfig {
    float terrainSize = 16384.0f;
    float seaLevel = 0.0f;
    float riverFlowThreshold = 0.3f;
    std::string erosionCacheDir;
};

struct WatershedMetricsResult {
    std::vector<float> twiMap;             // Topographic Wetness Index
    std::vector<uint8_t> streamOrderMap;   // Strahler stream order (0 = not a stream)
    std::vector<uint32_t> basinLabels;     // Watershed basin ID for each cell
    uint32_t basinCount = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

class WatershedMetrics {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    // Compute TWI from slope and flow accumulation
    static void computeTWI(
        WatershedMetricsResult& result,
        const std::vector<float>& slopeMap,
        const std::vector<float>& flowAccumulation,
        uint32_t flowMapWidth,
        uint32_t flowMapHeight,
        uint32_t outputWidth,
        uint32_t outputHeight,
        float terrainSize,
        ProgressCallback callback = nullptr
    );

    // Compute Strahler stream order
    static void computeStreamOrder(
        WatershedMetricsResult& result,
        const std::vector<float>& flowAccumulation,
        const std::vector<int8_t>& flowDirection,
        const std::vector<float>& heightData,
        uint32_t flowMapWidth,
        uint32_t flowMapHeight,
        uint32_t heightmapWidth,
        uint32_t heightmapHeight,
        const WatershedMetricsConfig& config,
        ProgressCallback callback = nullptr
    );

    // Load or generate watershed basin labels
    static void loadOrGenerateBasins(
        WatershedMetricsResult& result,
        const std::vector<float>& heightData,
        const std::vector<int8_t>& flowDirection,
        uint32_t heightmapWidth,
        uint32_t heightmapHeight,
        uint32_t flowMapWidth,
        uint32_t flowMapHeight,
        const WatershedMetricsConfig& config,
        ProgressCallback callback = nullptr
    );

    // Sample functions
    static float sampleTWI(const WatershedMetricsResult& result, float x, float z, float terrainSize);
    static uint8_t sampleStreamOrder(const WatershedMetricsResult& result, float x, float z, float terrainSize);
    static uint32_t sampleBasinLabel(const WatershedMetricsResult& result, float x, float z, float terrainSize);
};
