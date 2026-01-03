#pragma once

#include <cstdint>
#include <array>
#include <glm/glm.hpp>

/**
 * SkyProbeConfig - Configuration for cascaded sky visibility probes
 *
 * Implements Ghost of Tsushima-style sky probes:
 * - 4 cascades with increasing spacing (camera-relative)
 * - SH1 (L1 spherical harmonics) for directional visibility
 * - Bent normal + occlusion for compact storage option
 *
 * Memory usage per cascade:
 * - 64³ grid × 48 bytes (SH1 RGB) = 12.6MB per cascade
 * - Total 4 cascades = ~50MB
 */

struct SkyProbeCascade {
    float spacing;        // Probe spacing in meters
    uint32_t gridSize;    // Grid resolution (cubic)
    float range;          // Total range covered by this cascade
};

struct SkyProbeConfig {
    // Cascade configuration
    // Cascade 0: Fine detail near camera
    // Cascade 3: Coarse for distant ambient
    static constexpr uint32_t NUM_CASCADES = 4;

    std::array<SkyProbeCascade, NUM_CASCADES> cascades = {{
        { 4.0f,   64, 256.0f   },  // Cascade 0: 4m spacing, 256m range
        { 16.0f,  64, 1024.0f  },  // Cascade 1: 16m spacing, 1km range
        { 64.0f,  64, 4096.0f  },  // Cascade 2: 64m spacing, 4km range
        { 256.0f, 64, 16384.0f },  // Cascade 3: 256m spacing, 16km range
    }};

    // Probe data format
    enum class Format {
        SH1_RGB,        // L1 SH (4 coeffs × 3 channels = 12 floats = 48 bytes)
        BentNormal,     // Bent normal + visibility (4 floats = 16 bytes)
    };
    Format format = Format::SH1_RGB;

    // Update frequency
    bool runtimeBaking = false;      // Bake probes at runtime vs load from file
    uint32_t probesPerFrame = 64;    // Probes to update per frame (if runtime)

    // Blend settings
    float cascadeBlendRange = 0.1f;  // Fraction of cascade for blending

    // Quality presets
    enum class Quality {
        Low,      // 32³ grids, bent normal format (~8MB)
        Medium,   // 64³ grids, bent normal format (~32MB)
        High,     // 64³ grids, SH1 format (~50MB)
        Ultra,    // 96³ grids, SH1 format (~170MB)
    };

    static SkyProbeConfig fromQuality(Quality q) {
        SkyProbeConfig cfg;
        switch (q) {
            case Quality::Low:
                cfg.cascades = {{
                    { 8.0f,   32, 256.0f   },
                    { 32.0f,  32, 1024.0f  },
                    { 128.0f, 32, 4096.0f  },
                    { 512.0f, 32, 16384.0f },
                }};
                cfg.format = Format::BentNormal;
                break;
            case Quality::Medium:
                cfg.cascades = {{
                    { 4.0f,   64, 256.0f   },
                    { 16.0f,  64, 1024.0f  },
                    { 64.0f,  64, 4096.0f  },
                    { 256.0f, 64, 16384.0f },
                }};
                cfg.format = Format::BentNormal;
                break;
            case Quality::High:
                // Default settings
                break;
            case Quality::Ultra:
                cfg.cascades = {{
                    { 4.0f,   96, 384.0f   },
                    { 16.0f,  96, 1536.0f  },
                    { 64.0f,  96, 6144.0f  },
                    { 256.0f, 96, 24576.0f },
                }};
                cfg.format = Format::SH1_RGB;
                break;
        }
        return cfg;
    }

    // Calculate memory usage
    size_t estimateMemoryMB() const {
        size_t bytesPerProbe = (format == Format::SH1_RGB) ? 48 : 16;
        size_t totalProbes = 0;
        for (const auto& cascade : cascades) {
            totalProbes += cascade.gridSize * cascade.gridSize * cascade.gridSize;
        }
        return (totalProbes * bytesPerProbe) / (1024 * 1024);
    }

    // Get total probe count across all cascades
    uint32_t getTotalProbeCount() const {
        uint32_t total = 0;
        for (const auto& cascade : cascades) {
            total += cascade.gridSize * cascade.gridSize * cascade.gridSize;
        }
        return total;
    }
};

// GPU structures for shader binding
struct SkyProbeCascadeInfo {
    glm::vec4 origin;       // xyz = world origin of cascade, w = spacing
    glm::vec4 invSize;      // xyz = 1/(gridSize * spacing), w = blend start
    glm::vec4 params;       // x = gridSize, y = layer offset in 3D texture, z = range, w = unused
};

// Per-probe data (SH1 format)
struct SkyProbeSH1 {
    glm::vec4 sh0;          // L0 constant term (RGB + visibility)
    glm::vec3 sh1_x;        // L1 x coefficient (RGB)
    float padding1;
    glm::vec3 sh1_y;        // L1 y coefficient (RGB)
    float padding2;
    glm::vec3 sh1_z;        // L1 z coefficient (RGB)
    float padding3;
};

// Per-probe data (bent normal format - compact)
struct SkyProbeBentNormal {
    glm::vec4 bentNormalAndVisibility;  // xyz = bent normal, w = visibility [0,1]
};
