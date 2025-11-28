#pragma once

#include <string>
#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

class SystemBase {
public:
    struct InitInfo {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass renderPass;
        VkDescriptorPool descriptorPool;
        VkExtent2D extent;
        std::string shaderPath;
        uint32_t framesInFlight;
    };

    virtual ~SystemBase() = default;

    bool initBase(const InitInfo& info) {
        device = info.device;
        allocator = info.allocator;
        renderPass = info.renderPass;
        descriptorPool = info.descriptorPool;
        extent = info.extent;
        shaderPath = info.shaderPath;
        framesInFlight = info.framesInFlight;

        if (!createBuffers()) return false;

        if (usesComputePipeline()) {
            if (!createComputeDescriptorSetLayout()) return false;
            if (!createComputePipeline()) return false;
        }

        if (usesGraphicsPipeline()) {
            if (!createGraphicsDescriptorSetLayout()) return false;
            if (!createGraphicsPipeline()) return false;
        }

        if (!createExtraPipelines()) return false;
        if (!createDescriptorSets()) return false;

        return true;
    }

    void destroyBase(VkDevice deviceOverride = VK_NULL_HANDLE, VmaAllocator allocatorOverride = VK_NULL_HANDLE) {
        VkDevice dev = deviceOverride == VK_NULL_HANDLE ? device : deviceOverride;
        VmaAllocator alloc = allocatorOverride == VK_NULL_HANDLE ? allocator : allocatorOverride;

        if (usesGraphicsPipeline()) {
            destroyPipelineHandles(dev, graphicsPipeline);
        }

        if (usesComputePipeline()) {
            destroyPipelineHandles(dev, computePipeline);
        }

        destroyBuffers(alloc);
    }

protected:
    struct PipelineHandles {
        VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
        VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
        VkPipeline pipeline = VK_NULL_HANDLE;
    };

    virtual bool createBuffers() = 0;
    virtual bool createDescriptorSets() = 0;
    virtual void destroyBuffers(VmaAllocator allocator) = 0;

    virtual bool createComputeDescriptorSetLayout() { return true; }
    virtual bool createComputePipeline() { return true; }
    virtual bool createGraphicsDescriptorSetLayout() { return true; }
    virtual bool createGraphicsPipeline() { return true; }
    virtual bool createExtraPipelines() { return true; }

    virtual bool usesComputePipeline() const { return true; }
    virtual bool usesGraphicsPipeline() const { return true; }

    void destroyPipelineHandles(VkDevice dev, PipelineHandles& handles) {
        vkDestroyPipeline(dev, handles.pipeline, nullptr);
        vkDestroyPipelineLayout(dev, handles.pipelineLayout, nullptr);
        vkDestroyDescriptorSetLayout(dev, handles.descriptorSetLayout, nullptr);
        handles.pipeline = VK_NULL_HANDLE;
        handles.pipelineLayout = VK_NULL_HANDLE;
        handles.descriptorSetLayout = VK_NULL_HANDLE;
    }

    VkDevice device = VK_NULL_HANDLE;
    VmaAllocator allocator = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkExtent2D extent = {0, 0};
    std::string shaderPath;
    uint32_t framesInFlight = 0;

    PipelineHandles computePipeline;
    PipelineHandles graphicsPipeline;
};

