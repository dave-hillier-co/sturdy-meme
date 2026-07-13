#pragma once

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <array>
#include <cstdint>

#include "FrameData.h"

struct QueueSubmitDiagnostics;

/**
 * RenderResources - Snapshot of shared rendering resources
 *
 * Populated once per frame from various subsystems. Stages access
 * resources through this struct rather than querying systems directly,
 * eliminating delimiter violations.
 */
struct RenderResources {
    // HDR render target (from PostProcessSystem)
    vk::RenderPass hdrRenderPass = VK_NULL_HANDLE;
    vk::Framebuffer hdrFramebuffer = VK_NULL_HANDLE;
    VkExtent2D hdrExtent = {0, 0};
    vk::ImageView hdrColorView = VK_NULL_HANDLE;
    vk::ImageView hdrDepthView = VK_NULL_HANDLE;

    // Shadow resources (from ShadowSystem)
    vk::RenderPass shadowRenderPass = VK_NULL_HANDLE;
    vk::ImageView shadowMapView = VK_NULL_HANDLE;
    vk::Sampler shadowSampler = VK_NULL_HANDLE;
    std::array<glm::mat4, 4> cascadeMatrices = {};
    glm::vec4 cascadeSplitDepths = glm::vec4(0.0f);
    vk::Pipeline shadowPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout shadowPipelineLayout = VK_NULL_HANDLE;

    // Bloom output (from BloomSystem)
    vk::ImageView bloomOutput = VK_NULL_HANDLE;
    vk::Sampler bloomSampler = VK_NULL_HANDLE;

    // Swapchain target (for final output)
    vk::RenderPass swapchainRenderPass = VK_NULL_HANDLE;
    vk::Framebuffer swapchainFramebuffer = VK_NULL_HANDLE;
    VkExtent2D swapchainExtent = {0, 0};

    // Main scene pipeline (from Renderer)
    vk::Pipeline graphicsPipeline = VK_NULL_HANDLE;
    vk::PipelineLayout pipelineLayout = VK_NULL_HANDLE;
    vk::DescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
};

/**
 * RenderContext - Execution context for render stages (legacy)
 *
 * Passed to stage execute() methods. Contains everything a stage
 * needs to record commands without querying external state.
 *
 * Note: New code should prefer FrameContext from FrameContext.h
 * which provides a more flexible interface with optional resources.
 */
struct RenderContext {
    vk::CommandBuffer cmd = VK_NULL_HANDLE;
    uint32_t frameIndex = 0;
    const FrameData& frame;
    const RenderResources& resources;
    QueueSubmitDiagnostics* diagnostics = nullptr;  // Optional command counting

    // Constructor to ensure references are always valid
    RenderContext(vk::CommandBuffer cmdBuffer, uint32_t frameIdx,
                  const FrameData& frameData, const RenderResources& res,
                  QueueSubmitDiagnostics* diag = nullptr)
        : cmd(cmdBuffer)
        , frameIndex(frameIdx)
        , frame(frameData)
        , resources(res)
        , diagnostics(diag) {}
};
