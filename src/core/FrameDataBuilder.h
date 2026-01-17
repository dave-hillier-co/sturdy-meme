#pragma once

// ============================================================================
// FrameDataBuilder.h - Utility for building per-frame data structures
// ============================================================================
//
// Provides static methods for building FrameData and RenderResources from
// camera state and subsystems. This is pure data building with no side effects.
//
// Usage:
//   FrameData frame = FrameDataBuilder::buildFrameData(
//       camera, systems, frameIndex, deltaTime, time);
//   RenderResources resources = FrameDataBuilder::buildRenderResources(
//       systems, swapchainImageIndex, framebuffers, renderPass,
//       graphicsPipeline, pipelineLayout, descriptorSetLayout);
//

#include "FrameData.h"
#include "RenderContext.h"
#include <vulkan/vulkan.h>
#include <vector>

// Forward declarations
class Camera;
class RendererSystems;

namespace vk::raii {
    class Framebuffer;
}

class FrameDataBuilder {
public:
    // Build FrameData from camera and subsystems
    // This gathers all per-frame state needed by rendering passes
    static FrameData buildFrameData(
        const Camera& camera,
        const RendererSystems& systems,
        uint32_t frameIndex,
        float deltaTime,
        float time);

    // Build RenderResources from subsystems and Renderer-owned resources
    // This creates a snapshot of GPU resources needed by rendering passes
    static RenderResources buildRenderResources(
        const RendererSystems& systems,
        uint32_t swapchainImageIndex,
        const std::vector<vk::raii::Framebuffer>& framebuffers,
        VkRenderPass swapchainRenderPass,
        VkExtent2D swapchainExtent,
        VkPipeline graphicsPipeline,
        VkPipelineLayout pipelineLayout,
        VkDescriptorSetLayout descriptorSetLayout);
};
