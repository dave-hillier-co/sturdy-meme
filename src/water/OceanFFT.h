#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <optional>
#include "VmaBuffer.h"
#include "VmaImage.h"

/**
 * OceanFFT - FFT-based Ocean Simulation (Tessendorf Method)
 *
 * Implements a physically-based ocean surface simulation using FFT.
 * Based on "Simulating Ocean Water" (Tessendorf, 2001).
 *
 * Pipeline:
 * 1. Generate initial spectrum H0(k) using Phillips spectrum (once at init,
 *    and again whenever parameters change)
 * 2. Each frame:
 *    a. Time evolution: H(k,t) from H0(k) for height + choppy X/Z displacement
 *    b. Inverse FFT (per component) to get spatial displacement
 *    c. Generate displacement, normal, and foam maps
 *
 * Three cascades (large swells / medium waves / ripples) feed the water shader's
 * cascade bindings for multi-scale detail.
 *
 * All simulation images live permanently in VK_IMAGE_LAYOUT_GENERAL; consumers
 * must bind them with that layout (like the SSR result image).
 */

// Push constant structures - must match the ocean_*.comp shaders
struct OceanTimeEvolutionPushConstants {
    float time;
    int resolution;
    float oceanSize;
    float choppiness;
};

struct OceanFFTPushConstants {
    int stage;
    int direction;
    int resolution;
    int inverse;
};

struct OceanDisplacementPushConstants {
    int resolution;
    float oceanSize;
    float heightScale;
    float foamThreshold;
    float foamDecay;
    float normalStrength;
};

class OceanFFT {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit OceanFFT(ConstructToken) {}

    // Ocean simulation parameters
    struct OceanParams {
        int resolution = 256;           // FFT resolution (power of two)
        float oceanSize = 256.0f;       // Physical patch size in meters (cascade 0)
        float windSpeed = 8.0f;         // Wind speed in m/s
        glm::vec2 windDirection = {0.8f, 0.6f};  // Wind direction (normalized)
        float amplitude = 200.0f;       // Phillips spectrum amplitude (A constant)
                                        // Note: the IFFT is 1/N^2-normalized, so A is
                                        // large compared to textbook values.
        float gravity = 9.81f;          // Gravitational constant
        float smallWaveCutoff = 0.01f;  // Suppress waves smaller than this (meters)
        float alignment = 0.8f;         // Wind alignment (0=omni, 1=directional)
        float choppiness = 1.0f;        // Horizontal displacement scale (lambda)
        float heightScale = 1.0f;       // Height multiplier
        float foamThreshold = 0.0f;     // Jacobian threshold for foam
        float normalStrength = 1.0f;    // Normal map intensity
    };

    // Cascade configuration for multi-scale waves
    struct CascadeConfig {
        float oceanSize;     // Patch size for this cascade
        float heightScale;   // Height scale for this cascade
        float choppiness;    // Choppiness for this cascade
    };

    struct InitInfo {
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::CommandPool commandPool;
        vk::Queue computeQueue;
        std::string shaderPath;
        uint32_t framesInFlight;
        OceanParams params;
        bool useCascades = true;  // Enable multi-scale cascades
        const vk::raii::Device* raiiDevice = nullptr;
    };

    /**
     * Factory: Create and initialize OceanFFT.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<OceanFFT> create(const InitInfo& info);

    ~OceanFFT();

    // Non-copyable, non-movable
    OceanFFT(const OceanFFT&) = delete;
    OceanFFT& operator=(const OceanFFT&) = delete;
    OceanFFT(OceanFFT&&) = delete;
    OceanFFT& operator=(OceanFFT&&) = delete;

    // Record the full simulation for this frame (spectrum regen if dirty,
    // time evolution, IFFT, displacement/normal/foam generation).
    // Call from the compute pass, before any water rendering.
    void recordCompute(vk::CommandBuffer cmd, float time);

    // Get output textures for the water shader.
    // Images are in VK_IMAGE_LAYOUT_GENERAL - bind descriptors accordingly.
    vk::ImageView getDisplacementView(int cascade = 0) const;
    vk::ImageView getNormalView(int cascade = 0) const;
    vk::ImageView getFoamView(int cascade = 0) const;
    vk::Sampler getSampler() const { return sampler_ ? **sampler_ : VK_NULL_HANDLE; }

    // Parameter access
    void setParams(const OceanParams& newParams);
    const OceanParams& getParams() const { return params; }

    // Individual parameter setters (mark the spectrum dirty when needed)
    void setWindSpeed(float speed);
    void setWindDirection(const glm::vec2& dir);
    void setAmplitude(float amp);
    void setChoppiness(float chop);
    void setHeightScale(float scale);
    void setFoamThreshold(float threshold);

    // Getters for UI
    float getWindSpeed() const { return params.windSpeed; }
    glm::vec2 getWindDirection() const { return params.windDirection; }
    float getAmplitude() const { return params.amplitude; }
    float getChoppiness() const { return params.choppiness; }
    float getHeightScale() const { return params.heightScale; }
    float getFoamThreshold() const { return params.foamThreshold; }
    int getResolution() const { return params.resolution; }
    float getOceanSize() const { return params.oceanSize; }
    int getCascadeCount() const { return cascadeCount; }

    bool isEnabled() const { return enabled; }
    void setEnabled(bool enable) { enabled = enable; }

private:
    bool initInternal(const InitInfo& info);
    void cleanup();

    // Per-cascade data
    struct Cascade {
        // Spectrum textures (generated once / on param change)
        ManagedImage h0Spectrum;
        std::optional<vk::raii::ImageView> h0SpectrumView;

        ManagedImage omegaSpectrum;
        std::optional<vk::raii::ImageView> omegaSpectrumView;

        // Time-evolved spectra -> become spatial displacement after IFFT.
        // Each component ping-pongs with its own scratch image; 2*log2(N) passes
        // is even, so the final IFFT result always lands back in the hkt image.
        ManagedImage hkt[3];      // 0 = Dy, 1 = Dx, 2 = Dz
        std::optional<vk::raii::ImageView> hktView[3];
        ManagedImage scratch[3];
        std::optional<vk::raii::ImageView> scratchView[3];

        // Output textures
        ManagedImage displacementMap;
        std::optional<vk::raii::ImageView> displacementMapView;

        ManagedImage normalMap;
        std::optional<vk::raii::ImageView> normalMapView;

        ManagedImage foamMap;
        std::optional<vk::raii::ImageView> foamMapView;

        CascadeConfig config;
    };

    static constexpr int kComponents = 3;  // Dy, Dx, Dz

    bool createComputePipelines();
    bool createCascade(Cascade& cascade);
    bool createImage(ManagedImage& image, std::optional<vk::raii::ImageView>& view,
                     VkFormat format, VkImageUsageFlags usage);
    bool createDescriptorSets();
    bool transitionImagesToGeneral();

    void recordSpectrumGeneration(vk::CommandBuffer cmd, int cascadeIndex);
    void recordTimeEvolution(vk::CommandBuffer cmd, int cascadeIndex, float time);
    void recordFFT(vk::CommandBuffer cmd, int cascadeIndex, int component);
    void recordDisplacementGeneration(vk::CommandBuffer cmd, int cascadeIndex);

    // Device resources
    vk::Device device = VK_NULL_HANDLE;
    vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    vk::CommandPool commandPool = VK_NULL_HANDLE;
    vk::Queue computeQueue = VK_NULL_HANDLE;
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    // Parameters
    OceanParams params;
    bool enabled = true;
    bool spectrumDirty = true;

    // Cascades for multi-scale simulation
    static constexpr int MAX_CASCADES = 3;
    std::vector<Cascade> cascades;
    int cascadeCount = 1;

    // Compute pipelines
    std::optional<vk::raii::Pipeline> spectrumPipeline_;
    std::optional<vk::raii::PipelineLayout> spectrumPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> spectrumDescLayout_;

    std::optional<vk::raii::Pipeline> timeEvolutionPipeline_;
    std::optional<vk::raii::PipelineLayout> timeEvolutionPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> timeEvolutionDescLayout_;

    std::optional<vk::raii::Pipeline> fftPipeline_;
    std::optional<vk::raii::PipelineLayout> fftPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> fftDescLayout_;

    std::optional<vk::raii::Pipeline> displacementPipeline_;
    std::optional<vk::raii::PipelineLayout> displacementPipelineLayout_;
    std::optional<vk::raii::DescriptorSetLayout> displacementDescLayout_;

    // Descriptor pool and sets (all written once at init)
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    std::vector<vk::DescriptorSet> spectrumDescSets;       // [cascade]
    std::vector<vk::DescriptorSet> timeEvolutionDescSets;  // [cascade]
    std::vector<vk::DescriptorSet> displacementDescSets;   // [cascade]
    // FFT sets: per cascade, per component, two directions of the ping-pong
    // (hkt -> scratch and scratch -> hkt).
    std::vector<vk::DescriptorSet> fftDescSets;            // [cascade][component][2]
    vk::DescriptorSet fftSet(int cascade, int component, int pingpong) const {
        return fftDescSets[(cascade * kComponents + component) * 2 + pingpong];
    }

    // Spectrum parameters UBO - must match ocean_spectrum.comp layout
    struct SpectrumUBO {
        int resolution;
        float oceanSize;
        float windSpeed;
        float padding1;
        glm::vec2 windDirection;
        float amplitude;
        float gravity;
        float smallWaveCutoff;
        float alignment;
        uint32_t seed;
        float padding2;
        float padding3;
        float padding4;
    };
    std::vector<ManagedBuffer> spectrumUBOs;
    std::vector<void*> spectrumUBOMapped;

    // Sampler for output textures
    std::optional<vk::raii::Sampler> sampler_;

    // RAII device pointer
    const vk::raii::Device* raiiDevice_ = nullptr;
};
