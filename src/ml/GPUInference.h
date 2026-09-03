#pragma once

#include "Tensor.h"
#include "MLPNetwork.h"
#include "calm/LowLevelController.h"
#include "VmaBuffer.h"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <vector>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace ml {

// Layer metadata for the GPU compute shader.
// Packed as 5 uint32 values per layer.
struct GPULayerMeta {
    uint32_t weightOffset;  // Float offset into weight buffer
    uint32_t biasOffset;    // Float offset into weight buffer
    uint32_t inFeatures;
    uint32_t outFeatures;
    uint32_t activation;    // 0=None, 1=ReLU, 2=Tanh
};

// Push constants matching the shader layout
struct InferencePushConstants {
    uint32_t numLayers;
    uint32_t styleLayerCount;
    uint32_t mainLayerCount;
    uint32_t styleDim;
};

// GPU batch inference for style-conditioned LLC policies.
//
// Evaluates the same LLC network for many NPCs simultaneously using a
// Vulkan compute shader. All NPCs must share the same LLC architecture
// (same archetype). For mixed archetypes, create one GPUInference per type.
//
// Usage:
//   1. create() and upload weights (once at load time)
//   2. Each frame: upload batched latent+obs, dispatch, read back actions
//   3. Apply actions to skeletons on CPU
//
// The compute shader processes one NPC per invocation, performing the full
// style-conditioned MLP forward pass (style MLP -> concat -> main MLP -> muHead).
class GPUInference {
public:
    struct Config {
        uint32_t maxNPCs = 256;         // Maximum NPCs in a single batch
        uint32_t latentDim = 64;
        uint32_t obsDim = 102;
        uint32_t actionDim = 37;
        uint32_t maxHiddenSize = 1024;  // Largest hidden layer in the network
        std::string shaderPath;         // Path to calm_inference.comp.spv
    };

    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit GPUInference(ConstructToken) {}

    /**
     * Factory: create GPU resources (pipeline, descriptor set, buffers).
     * The raii device and allocator must outlive the returned object.
     * Returns nullptr on failure.
     */
    static std::unique_ptr<GPUInference> create(const vk::raii::Device& device,
                                                VmaAllocator allocator,
                                                const Config& config);

    // Members release in reverse declaration order (pipeline, layout, pool,
    // set layout, buffers).
    ~GPUInference() = default;

    // Non-copyable, non-movable
    GPUInference(const GPUInference&) = delete;
    GPUInference& operator=(const GPUInference&) = delete;
    GPUInference(GPUInference&&) = delete;
    GPUInference& operator=(GPUInference&&) = delete;

    // Upload LLC network weights to the GPU.
    bool uploadWeights(const calm::LowLevelController& llc);

    // Upload batched input data (latent codes + observations) for this frame.
    void uploadInputs(const std::vector<float>& latents,
                      const std::vector<float>& observations,
                      uint32_t npcCount);

    // Record the compute dispatch into a command buffer.
    void recordDispatch(vk::CommandBuffer cmd, uint32_t npcCount);

    // Read back the computed actions from the GPU.
    void readBackActions(std::vector<float>& actions, uint32_t npcCount);

    // Get the config
    const Config& config() const { return config_; }

private:
    bool initInternal(const vk::raii::Device& device, VmaAllocator allocator, const Config& config);

    Config config_;

    vk::Device device_{};
    VmaAllocator allocator_ = nullptr;

    // GPU buffers (VMA-allocated, persistently mapped; RAII via VmaBuffer)
    struct GPUBuffer {
        VmaBuffer buffer;
        size_t size = 0;
    };

    GPUBuffer weightBuffer_;
    GPUBuffer layerMetaBuffer_;
    GPUBuffer latentBuffer_;
    GPUBuffer obsBuffer_;
    GPUBuffer actionBuffer_;

    // Vulkan objects (RAII, declared in dependency order)
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::optional<vk::raii::DescriptorPool> descriptorPool_;
    vk::DescriptorSet descriptorSet_{};  // Pool-owned
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;
    std::optional<vk::raii::Pipeline> pipeline_;

    InferencePushConstants pushConstants_{};

    // Helpers
    bool createBuffer(GPUBuffer& buf, size_t size,
                      vk::BufferUsageFlags usage, VmaMemoryUsage memUsage);
    void uploadToBuffer(GPUBuffer& buf, const void* data, size_t size);
    void readFromBuffer(const GPUBuffer& buf, void* data, size_t size);

    bool createDescriptorSet();
    void updateDescriptorSet();

    bool packWeights(const calm::LowLevelController& llc,
                     std::vector<float>& packedWeights,
                     std::vector<GPULayerMeta>& layerMetas);
};

} // namespace ml
