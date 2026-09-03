#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>
#include <cmath>
#include <optional>
#include "UBOs.h"
#include "core/vulkan/VmaBuffer.h"
#include "core/vulkan/VmaImage.h"
#include "DescriptorManager.h"
#include "InitContext.h"
#include <vulkan/vulkan_raii.hpp>

// Atmosphere LUT system for physically-based sky rendering (Phase 4.1)
// Precomputes transmittance and multi-scatter LUTs for efficient atmospheric scattering

// Atmosphere parameters - layout must match GLSL std140 (see atmosphere_common.glsl)
struct AtmosphereParams {
    // Planet geometry (in kilometers to match sky.frag)
    float planetRadius = 6371.0f;          // Earth radius in km
    float atmosphereRadius = 6471.0f;      // Top of atmosphere in km
    float pad1 = 0.0f, pad2 = 0.0f;        // Padding to align vec3 to 16 bytes

    // Rayleigh scattering (air molecules) - per km coefficients
    glm::vec3 rayleighScatteringBase = glm::vec3(5.802e-3f, 13.558e-3f, 33.1e-3f);
    float rayleighScaleHeight = 8.0f;      // km

    // Mie scattering (aerosols/haze) - per km coefficients
    float mieScatteringBase = 3.996e-3f;
    float mieAbsorptionBase = 4.4e-3f;
    float mieScaleHeight = 1.2f;           // km
    float mieAnisotropy = 0.8f;            // Phase function asymmetry

    // Ozone absorption (affects blue channel at horizon) - per km
    glm::vec3 ozoneAbsorption = glm::vec3(0.65e-3f, 1.881e-3f, 0.085e-3f);
    float ozoneLayerCenter = 25.0f;        // km

    float ozoneLayerWidth = 15.0f;         // km
    float sunAngularRadius = 0.00935f / 2.0f;  // radians
    float pad3 = 0.0f, pad4 = 0.0f;        // Padding to align vec3 to 16 bytes

    glm::vec3 solarIrradiance = glm::vec3(1.474f, 1.8504f, 1.91198f);
    float pad5 = 0.0f;                     // Padding for struct alignment
};

// AtmosphereUniforms struct (manually defined since it contains nested AtmosphereParams)
struct AtmosphereUniforms {
    AtmosphereParams params;
    glm::vec4 toSunDirection;  // xyz = direction toward sun, w = unused
    glm::vec4 cameraPosition; // xyz = camera pos, w = camera altitude
    // Note: Individual floats instead of float[2] array to match GLSL std140
    // layout (arrays get 16-byte stride per element in std140, scalars don't)
    float atmoPadding0 = 0.0f;
    float atmoPadding1 = 0.0f;
};


// Cloud map uniform parameters (must match GLSL layout)
struct CloudMapUniforms {
    glm::vec4 windOffset;    // xyz = wind offset for animation, w = time
    float coverage;          // 0-1 cloud coverage amount
    float density;           // Base density multiplier
    float sharpness;         // Coverage threshold sharpness
    float detailScale;       // Scale for detail noise
};

class AtmosphereLUTSystem {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit AtmosphereLUTSystem(ConstructToken) {}

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;
    };

    // LUT dimensions (from Phase 4.1)
    static constexpr uint32_t TRANSMITTANCE_WIDTH = 256;
    static constexpr uint32_t TRANSMITTANCE_HEIGHT = 64;
    static constexpr uint32_t MULTISCATTER_SIZE = 32;
    static constexpr uint32_t SKYVIEW_WIDTH = 192;
    static constexpr uint32_t SKYVIEW_HEIGHT = 108;
    // Irradiance LUT dimensions (Phase 4.1.9)
    // Indexed by: altitude (Y) and sun zenith cosine (X)
    static constexpr uint32_t IRRADIANCE_WIDTH = 64;   // cos(sun zenith)
    static constexpr uint32_t IRRADIANCE_HEIGHT = 16;  // altitude

    // Cloud Map LUT dimensions (Paraboloid projection)
    // Stores procedural cloud density mapped to hemisphere directions
    static constexpr uint32_t CLOUDMAP_SIZE = 256;     // Square texture for paraboloid map

    /**
     * Factory: Create and initialize AtmosphereLUTSystem.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<AtmosphereLUTSystem> create(const InitInfo& info);
    static std::unique_ptr<AtmosphereLUTSystem> create(const InitContext& ctx);

    ~AtmosphereLUTSystem() = default;

    // Non-copyable, non-movable
    AtmosphereLUTSystem(const AtmosphereLUTSystem&) = delete;
    AtmosphereLUTSystem& operator=(const AtmosphereLUTSystem&) = delete;
    AtmosphereLUTSystem(AtmosphereLUTSystem&&) = delete;
    AtmosphereLUTSystem& operator=(AtmosphereLUTSystem&&) = delete;

    // Compute LUTs (called at startup and when atmosphere parameters change)
    void computeTransmittanceLUT(vk::CommandBuffer cmd);
    void computeMultiScatterLUT(vk::CommandBuffer cmd);
    void computeIrradianceLUT(vk::CommandBuffer cmd);
    void computeSkyViewLUT(vk::CommandBuffer cmd, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);
    void computeCloudMapLUT(vk::CommandBuffer cmd, const glm::vec3& windOffset, float time);

    // Update sky-view LUT per frame (uses SHADER_READ_ONLY_OPTIMAL as old layout since LUT was already computed)
    // frameIndex is required for proper double-buffering of uniform buffers and descriptor sets
    void updateSkyViewLUT(vk::CommandBuffer cmd, uint32_t frameIndex, const glm::vec3& sunDir, const glm::vec3& cameraPos, float cameraAltitude);
    void updateCloudMapLUT(vk::CommandBuffer cmd, uint32_t frameIndex, const glm::vec3& windOffset, float time);

    // Get LUT views for sampling in shaders
    vk::ImageView getTransmittanceLUTView() const { return transmittanceLUTView ? static_cast<vk::ImageView>(**transmittanceLUTView) : vk::ImageView{}; }
    vk::ImageView getMultiScatterLUTView() const { return multiScatterLUTView ? static_cast<vk::ImageView>(**multiScatterLUTView) : vk::ImageView{}; }
    vk::ImageView getSkyViewLUTView() const { return skyViewLUTView ? static_cast<vk::ImageView>(**skyViewLUTView) : vk::ImageView{}; }
    vk::ImageView getRayleighIrradianceLUTView() const { return rayleighIrradianceLUTView ? static_cast<vk::ImageView>(**rayleighIrradianceLUTView) : vk::ImageView{}; }
    vk::ImageView getMieIrradianceLUTView() const { return mieIrradianceLUTView ? static_cast<vk::ImageView>(**mieIrradianceLUTView) : vk::ImageView{}; }
    vk::ImageView getCloudMapLUTView() const { return cloudMapLUTView ? static_cast<vk::ImageView>(**cloudMapLUTView) : vk::ImageView{}; }
    vk::Sampler getLUTSampler() const { return lutSampler_ ? **lutSampler_ : vk::Sampler{}; }

    // Export LUTs as PNG files (for debugging/visualization)
    bool exportLUTsAsPNG(const std::string& outputDir);

    // Atmosphere parameters
    void setAtmosphereParams(const AtmosphereParams& params) {
        atmosphereParams = params;
        paramsDirty = true;  // Mark for static LUT recomputation
        skyViewNeedsUpdate = true;  // Sky-view depends on atmosphere params
    }
    const AtmosphereParams& getAtmosphereParams() const { return atmosphereParams; }

    // Cloud map parameters (used by updateCloudMapLUT)
    void setCloudCoverage(float coverage) {
        float newCoverage = glm::clamp(coverage, 0.0f, 1.0f);
        if (std::abs(newCoverage - cloudCoverage) > 0.001f) {
            cloudCoverage = newCoverage;
            cloudMapNeedsUpdate = true;
        }
    }
    float getCloudCoverage() const { return cloudCoverage; }
    void setCloudDensity(float density) {
        float newDensity = glm::clamp(density, 0.0f, 2.0f);
        if (std::abs(newDensity - cloudDensity) > 0.001f) {
            cloudDensity = newDensity;
            cloudMapNeedsUpdate = true;
        }
    }
    float getCloudDensity() const { return cloudDensity; }

    // Check if LUTs need recomputation due to parameter changes
    bool needsRecompute() const { return paramsDirty; }

    // Recompute static LUTs (transmittance, multi-scatter, irradiance) when params change
    // Call this from the render loop when needsRecompute() returns true
    void recomputeStaticLUTs(vk::CommandBuffer cmd);

private:
    bool createTransmittanceLUT();
    bool createMultiScatterLUT();
    bool createSkyViewLUT();
    bool createIrradianceLUTs();
    bool createCloudMapLUT();
    bool createLUTSampler();
    bool createDescriptorSetLayouts();
    bool createDescriptorSets();
    bool createUniformBuffer();
    bool createComputePipelines();

    // Transition irradiance LUTs for compute write
    void barrierIrradianceLUTsForCompute(vk::CommandBuffer cmd);

    // Transition irradiance LUTs for fragment shader sampling
    void barrierIrradianceLUTsForSampling(vk::CommandBuffer cmd);

    // Helper to export a 2D image to PNG
    bool exportImageToPNG(vk::Image image, VkFormat format, uint32_t width, uint32_t height, const std::string& filename);

    vk::Device device{};
    VmaAllocator allocator = nullptr;
    DescriptorManager::Pool* descriptorPool = nullptr;
    std::string shaderPath;
    uint32_t framesInFlight = 0;
    const vk::raii::Device* raiiDevice_ = nullptr;

    // Transmittance LUT (256×64, RGBA16F)
    ManagedImage transmittanceLUT;
    std::optional<vk::raii::ImageView> transmittanceLUTView;

    // Multi-scatter LUT (32×32, RG16F)
    ManagedImage multiScatterLUT;
    std::optional<vk::raii::ImageView> multiScatterLUTView;

    // Sky-View LUT (192×108, RGBA16F)
    ManagedImage skyViewLUT;
    std::optional<vk::raii::ImageView> skyViewLUTView;

    // Rayleigh Irradiance LUT (64×16, RGBA16F) - Phase 4.1.9
    // Stores scattered Rayleigh light *before* phase function multiplication
    ManagedImage rayleighIrradianceLUT;
    std::optional<vk::raii::ImageView> rayleighIrradianceLUTView;

    // Mie Irradiance LUT (64×16, RGBA16F) - Phase 4.1.9
    // Stores scattered Mie light *before* phase function multiplication
    ManagedImage mieIrradianceLUT;
    std::optional<vk::raii::ImageView> mieIrradianceLUTView;

    // Cloud Map LUT (256×256, RGBA16F) - Paraboloid projection
    // R = base density, G = detail noise, B = coverage mask, A = height gradient
    ManagedImage cloudMapLUT;
    std::optional<vk::raii::ImageView> cloudMapLUTView;

    // LUT sampler (bilinear filtering, clamp to edge)
    std::optional<vk::raii::Sampler> lutSampler_;

    // Uniform buffer for one-time LUT computation (at startup); persistently mapped
    ManagedBuffer staticUniformBuffer_;
    void* staticUniformMapped_ = nullptr;

    // Per-frame uniform buffers for per-frame updates (double-buffered); persistently mapped
    std::vector<ManagedBuffer> skyViewUniformBuffers_;
    std::vector<void*> skyViewUniformMapped_;
    std::vector<ManagedBuffer> cloudMapUniformBuffers_;
    std::vector<void*> cloudMapUniformMapped_;

    // Compute pipelines (RAII; declared after the resources they reference so
    // reverse-order destruction releases pipelines before layouts and buffers)
    std::optional<vk::raii::DescriptorSetLayout> transmittanceDescriptorSetLayout;
    std::optional<vk::raii::DescriptorSetLayout> multiScatterDescriptorSetLayout;
    std::optional<vk::raii::DescriptorSetLayout> skyViewDescriptorSetLayout;
    std::optional<vk::raii::DescriptorSetLayout> irradianceDescriptorSetLayout;
    std::optional<vk::raii::DescriptorSetLayout> cloudMapDescriptorSetLayout;

    std::optional<vk::raii::PipelineLayout> transmittancePipelineLayout;
    std::optional<vk::raii::PipelineLayout> multiScatterPipelineLayout;
    std::optional<vk::raii::PipelineLayout> skyViewPipelineLayout;
    std::optional<vk::raii::PipelineLayout> irradiancePipelineLayout;
    std::optional<vk::raii::PipelineLayout> cloudMapPipelineLayout;

    std::optional<vk::raii::Pipeline> transmittancePipeline;
    std::optional<vk::raii::Pipeline> multiScatterPipeline;
    std::optional<vk::raii::Pipeline> skyViewPipeline;
    std::optional<vk::raii::Pipeline> irradiancePipeline;
    std::optional<vk::raii::Pipeline> cloudMapPipeline;

    // Descriptor sets are pool-owned (DescriptorManager::Pool) and released with the pool.
    // Single descriptor sets for one-time LUT computation (at startup)
    vk::DescriptorSet transmittanceDescriptorSet{};
    vk::DescriptorSet multiScatterDescriptorSet{};
    vk::DescriptorSet irradianceDescriptorSet{};

    // Per-frame descriptor sets for per-frame LUT updates (double-buffered)
    std::vector<vk::DescriptorSet> skyViewDescriptorSets;
    std::vector<vk::DescriptorSet> cloudMapDescriptorSets;

    // Atmosphere parameters
    AtmosphereParams atmosphereParams;

    // Cloud map parameters
    float cloudCoverage = 0.5f;  // 0-1 cloud coverage
    float cloudDensity = 0.3f;   // Base density multiplier

    // Dirty flag for LUT recomputation
    bool paramsDirty = false;

    // Change detection for sky-view LUT (only recompute when inputs change significantly)
    glm::vec3 lastSkyViewSunDir = glm::vec3(0.0f);
    glm::vec3 lastSkyViewCameraPos = glm::vec3(0.0f);
    float lastSkyViewCameraAltitude = -1.0f;
    bool skyViewNeedsUpdate = true;  // Force initial compute

    // Change detection for cloud map LUT
    glm::vec3 lastCloudWindOffset = glm::vec3(0.0f);
    float lastCloudTime = -1.0f;
    float lastCloudCoverage = -1.0f;
    float lastCloudDensity = -1.0f;
    bool cloudMapNeedsUpdate = true;  // Force initial compute

    // Thresholds for detecting significant changes
    static constexpr float SUN_DIR_THRESHOLD = 0.0001f;      // Sun direction change (dot product)
    static constexpr float CAMERA_POS_THRESHOLD = 10.0f;     // Camera position change in km
    static constexpr float ALTITUDE_THRESHOLD = 0.1f;        // Altitude change in km
    static constexpr float WIND_OFFSET_THRESHOLD = 0.001f;   // Wind offset change
    static constexpr float CLOUD_PARAM_THRESHOLD = 0.001f;   // Cloud coverage/density change

    bool initInternal(const InitInfo& info);
};
