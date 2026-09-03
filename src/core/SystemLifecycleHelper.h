#pragma once

#include <functional>
#include <string>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include "DescriptorManager.h"

class SystemLifecycleHelper {
public:
    SystemLifecycleHelper() = default;

    // Releases the pipeline handles if the owning system never called
    // destroy(). The destroyBuffers hook is deliberately NOT run here: it
    // captures the owning system's `this` and would execute after that
    // system's destructor body, when its buffer members may already be gone.
    // Systems that own buffers through this helper keep calling destroy().
    ~SystemLifecycleHelper() {
        if (!initialized) return;
        if (graphicsEnabled) destroyPipelineHandles(initInfo.device, graphicsPipeline);
        if (computeEnabled) destroyPipelineHandles(initInfo.device, computePipeline);
    }

    // Owns raw pipeline handles: copying would double-destroy them. Moving
    // transfers them and leaves the source uninitialized (inert destructor).
    SystemLifecycleHelper(const SystemLifecycleHelper&) = delete;
    SystemLifecycleHelper& operator=(const SystemLifecycleHelper&) = delete;
    SystemLifecycleHelper(SystemLifecycleHelper&& other) noexcept { moveFrom(other); }
    SystemLifecycleHelper& operator=(SystemLifecycleHelper&& other) noexcept {
        if (this != &other) {
            // Explicit destroy() by the owner is the documented teardown; a
            // still-initialized target only releases its pipeline handles
            // here (see the destructor for why destroyBuffers is not run).
            if (initialized) {
                if (graphicsEnabled) destroyPipelineHandles(initInfo.device, graphicsPipeline);
                if (computeEnabled) destroyPipelineHandles(initInfo.device, computePipeline);
            }
            moveFrom(other);
        }
        return *this;
    }

    struct InitInfo {
        vk::Device device;
        VmaAllocator allocator;
        vk::RenderPass renderPass;
        DescriptorManager::Pool* descriptorPool;  // Auto-growing pool (preferred)
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
        const vk::raii::Device* raiiDevice = nullptr;  // For vk::raii::* resource creation
    };

    struct PipelineHandles {
        vk::DescriptorSetLayout descriptorSetLayout{};
        vk::PipelineLayout pipelineLayout{};
        vk::Pipeline pipeline{};
    };

    struct Hooks {
        std::function<bool()> createBuffers;
        std::function<bool()> createDescriptorSets;
        std::function<void(VmaAllocator)> destroyBuffers;

        std::function<bool()> createComputeDescriptorSetLayout = [] { return true; };
        std::function<bool()> createComputePipeline = [] { return true; };
        std::function<bool()> createGraphicsDescriptorSetLayout = [] { return true; };
        std::function<bool()> createGraphicsPipeline = [] { return true; };
        std::function<bool()> createExtraPipelines = [] { return true; };

        std::function<bool()> usesComputePipeline = [] { return true; };
        std::function<bool()> usesGraphicsPipeline = [] { return true; };
    };

    bool init(const InitInfo& info, const Hooks& hooks) {
        initInfo = info;
        callbacks = hooks;
        computeEnabled = callbacks.usesComputePipeline();
        graphicsEnabled = callbacks.usesGraphicsPipeline();

        if (!callbacks.createBuffers || !callbacks.createDescriptorSets || !callbacks.destroyBuffers) {
            return false;
        }

        if (!callbacks.createBuffers()) return false;

        if (computeEnabled) {
            if (!callbacks.createComputeDescriptorSetLayout()) return false;
            if (!callbacks.createComputePipeline()) return false;
        }

        if (graphicsEnabled) {
            if (!callbacks.createGraphicsDescriptorSetLayout()) return false;
            if (!callbacks.createGraphicsPipeline()) return false;
        }

        if (!callbacks.createExtraPipelines()) return false;
        if (!callbacks.createDescriptorSets()) return false;

        initialized = true;
        return true;
    }

    void destroy(vk::Device deviceOverride = {}, VmaAllocator allocatorOverride = nullptr) {
        if (!initialized) return;

        vk::Device dev = deviceOverride ? deviceOverride : initInfo.device;
        VmaAllocator alloc = allocatorOverride ? allocatorOverride : initInfo.allocator;

        if (graphicsEnabled) {
            destroyPipelineHandles(dev, graphicsPipeline);
        }

        if (computeEnabled) {
            destroyPipelineHandles(dev, computePipeline);
        }

        callbacks.destroyBuffers(alloc);
        initialized = false;
    }

    vk::Device getDevice() const { return initInfo.device; }
    VmaAllocator getAllocator() const { return initInfo.allocator; }
    vk::RenderPass getRenderPass() const { return initInfo.renderPass; }
    DescriptorManager::Pool* getDescriptorPool() const { return initInfo.descriptorPool; }
    const VkExtent2D& getExtent() const { return initInfo.extent; }
    void setExtent(VkExtent2D newExtent) { initInfo.extent = newExtent; }
    const std::string& getShaderPath() const { return initInfo.shaderPath; }
    uint32_t getFramesInFlight() const { return initInfo.framesInFlight; }
    const vk::raii::Device* getRaiiDevice() const { return initInfo.raiiDevice; }

    PipelineHandles& getComputePipeline() { return computePipeline; }
    PipelineHandles& getGraphicsPipeline() { return graphicsPipeline; }

private:
    void moveFrom(SystemLifecycleHelper& other) noexcept {
        initInfo = std::move(other.initInfo);
        callbacks = std::move(other.callbacks);
        computePipeline = other.computePipeline;
        graphicsPipeline = other.graphicsPipeline;
        computeEnabled = other.computeEnabled;
        graphicsEnabled = other.graphicsEnabled;
        initialized = other.initialized;
        other.computePipeline = {};
        other.graphicsPipeline = {};
        other.initialized = false;
    }

    // vkDestroy* accept null handles, so this is safe on an empty set.
    void destroyPipelineHandles(vk::Device dev, PipelineHandles& handles) {
        dev.destroyPipeline(handles.pipeline);
        dev.destroyPipelineLayout(handles.pipelineLayout);
        dev.destroyDescriptorSetLayout(handles.descriptorSetLayout);
        handles.pipeline = vk::Pipeline{};
        handles.pipelineLayout = vk::PipelineLayout{};
        handles.descriptorSetLayout = vk::DescriptorSetLayout{};
    }

    InitInfo initInfo{};
    Hooks callbacks{};
    PipelineHandles computePipeline{};
    PipelineHandles graphicsPipeline{};
    bool computeEnabled = true;
    bool graphicsEnabled = true;
    bool initialized = false;
};

