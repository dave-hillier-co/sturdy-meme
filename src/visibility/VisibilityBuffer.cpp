#include "VisibilityBuffer.h"
#include "ShaderLoader.h"
#include "Mesh.h"
#include "shaders/bindings.h"

#include <SDL3/SDL_log.h>
#include <array>
#include <cstring>

// ============================================================================
// Factory methods
// ============================================================================

std::unique_ptr<VisibilityBuffer> VisibilityBuffer::create(const InitInfo& info) {
    auto system = std::make_unique<VisibilityBuffer>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<VisibilityBuffer> VisibilityBuffer::create(const InitContext& ctx, VkFormat depthFormat) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.extent = ctx.extent;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.depthFormat = depthFormat;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

VisibilityBuffer::~VisibilityBuffer() {
    cleanup();
}

// ============================================================================
// Initialization
// ============================================================================

bool VisibilityBuffer::initInternal(const InitInfo& info) {
    device_ = info.device;
    allocator_ = info.allocator;
    descriptorPool_ = info.descriptorPool;
    extent_ = info.extent;
    shaderPath_ = info.shaderPath;
    framesInFlight_ = info.framesInFlight;
    depthFormat_ = info.depthFormat;
    raiiDevice_ = info.raiiDevice;

    if (!createRenderTargets()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create render targets");
        return false;
    }

    if (!createRenderPass()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create render pass");
        return false;
    }

    if (!createFramebuffer()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create framebuffer");
        return false;
    }

    if (!createRasterPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster pipeline");
        return false;
    }

    if (!createDebugPipeline()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create debug pipeline");
        return false;
    }

    if (!createResolveBuffers()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create resolve buffers");
        return false;
    }

    SDL_Log("VisibilityBuffer: Initialized (%ux%u, %u frames)",
            extent_.width, extent_.height, framesInFlight_);
    return true;
}

void VisibilityBuffer::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    destroyResolveBuffers();
    destroyResolvePipeline();
    destroyDebugPipeline();
    destroyRasterPipeline();
    destroyFramebuffer();
    destroyRenderPass();
    destroyRenderTargets();
    destroyDescriptorSets();
}

// ============================================================================
// Render targets
// ============================================================================

bool VisibilityBuffer::createRenderTargets() {
    // V-buffer: R32_UINT for packed instance+triangle IDs
    bool ok = ImageBuilder(allocator_)
        .setExtent(extent_)
        .setFormat(VISBUF_FORMAT)
        .setUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT)
        .build(device_, visibilityImage_, visibilityView_);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create visibility image");
        return false;
    }

    // Depth buffer
    ok = ImageBuilder(allocator_)
        .setExtent(extent_)
        .setFormat(depthFormat_)
        .setUsage(VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        .build(device_, depthImage_, depthView_, VK_IMAGE_ASPECT_DEPTH_BIT);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create depth image");
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyRenderTargets() {
    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    depthImage_.reset();

    if (visibilityView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, visibilityView_, nullptr);
        visibilityView_ = VK_NULL_HANDLE;
    }
    visibilityImage_.reset();
}

// ============================================================================
// Render pass
// ============================================================================

bool VisibilityBuffer::createRenderPass() {
    // Attachment 0: Visibility buffer (R32_UINT)
    VkAttachmentDescription visAttachment{};
    visAttachment.format = VISBUF_FORMAT;
    visAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    visAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    visAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    visAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    visAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    visAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    visAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    // Attachment 1: Depth
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat_;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthRef{};
    depthRef.attachment = 1;
    depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    // Dependencies for proper synchronization
    std::array<VkSubpassDependency, 2> dependencies{};

    // External -> Subpass 0
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // Subpass 0 -> External
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                                   VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                    VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    std::array<VkAttachmentDescription, 2> attachments = {visAttachment, depthAttachment};

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
    renderPassInfo.pDependencies = dependencies.data();

    if (vkCreateRenderPass(device_, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyRenderPass() {
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Framebuffer
// ============================================================================

bool VisibilityBuffer::createFramebuffer() {
    std::array<VkImageView, 2> attachments = {visibilityView_, depthView_};

    VkFramebufferCreateInfo fbInfo{};
    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbInfo.pAttachments = attachments.data();
    fbInfo.width = extent_.width;
    fbInfo.height = extent_.height;
    fbInfo.layers = 1;

    if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) != VK_SUCCESS) {
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyFramebuffer() {
    if (framebuffer_ != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device_, framebuffer_, nullptr);
        framebuffer_ = VK_NULL_HANDLE;
    }
}

// ============================================================================
// Raster pipeline (V-buffer write)
// ============================================================================

bool VisibilityBuffer::createRasterPipeline() {
    // Create descriptor set layout for raster pass
    // Binding 0: Main UBO (from main rendering set)
    // Binding 1: Diffuse texture (for alpha testing)
    std::array<vk::DescriptorSetLayoutBinding, 2> layoutBindings;
    layoutBindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_UBO)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eVertex);

    layoutBindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_DIFFUSE_TEX)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(layoutBindings);

    try {
        rasterDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster desc set layout: %s", e.what());
        return false;
    }

    // Push constants for per-object data
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(VisBufPushConstants);

    vk::DescriptorSetLayout vkRasterLayout(**rasterDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(vkRasterLayout)
        .setPushConstantRanges(pushRange);

    vk::Device vkDevice(device_);
    rasterPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load shaders
    auto vertModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to load raster shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages[0] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertModule)
        .setPName("main");
    shaderStages[1] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragModule)
        .setPName("main");

    // Vertex input - same as standard Vertex
    auto bindingDesc = Vertex::getBindingDescription();
    auto attrDescs = Vertex::getAttributeDescriptions();

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptionCount(1)
        .setPVertexBindingDescriptions(reinterpret_cast<const vk::VertexInputBindingDescription*>(&bindingDesc))
        .setVertexAttributeDescriptionCount(static_cast<uint32_t>(attrDescs.size()))
        .setPVertexAttributeDescriptions(reinterpret_cast<const vk::VertexInputAttributeDescription*>(attrDescs.data()));

    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    // Dynamic viewport/scissor
    auto viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {extent_.width, extent_.height}};

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eBack)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setLineWidth(1.0f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_TRUE)
        .setDepthCompareOp(vk::CompareOp::eLess);

    // No blending for R32_UINT (integer format)
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR);  // Only R channel for uint

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    // Dynamic state for viewport/scissor
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(shaderStages.data())
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(rasterPipelineLayout_)
        .setRenderPass(renderPass_)
        .setSubpass(0);

    auto result = vkDevice.createGraphicsPipeline(nullptr, pipelineInfo);
    if (result.result != vk::Result::eSuccess) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create raster pipeline");
        vkDevice.destroyShaderModule(*vertModule);
        vkDevice.destroyShaderModule(*fragModule);
        return false;
    }
    rasterPipeline_ = static_cast<VkPipeline>(result.value);

    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    SDL_Log("VisibilityBuffer: Raster pipeline created");
    return true;
}

void VisibilityBuffer::destroyRasterPipeline() {
    if (rasterPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, rasterPipeline_, nullptr);
        rasterPipeline_ = VK_NULL_HANDLE;
    }
    if (rasterPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, rasterPipelineLayout_, nullptr);
        rasterPipelineLayout_ = VK_NULL_HANDLE;
    }
    rasterDescSetLayout_.reset();
}

// ============================================================================
// Debug visualization pipeline
// ============================================================================

bool VisibilityBuffer::createDebugPipeline() {
    if (!raiiDevice_) return false;

    // Create nearest sampler for integer texture sampling
    auto samplerInfo = vk::SamplerCreateInfo{}
        .setMagFilter(vk::Filter::eNearest)
        .setMinFilter(vk::Filter::eNearest)
        .setAddressModeU(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeV(vk::SamplerAddressMode::eClampToEdge)
        .setAddressModeW(vk::SamplerAddressMode::eClampToEdge);

    try {
        nearestSampler_.emplace(*raiiDevice_, samplerInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create sampler: %s", e.what());
        return false;
    }

    // Descriptor set layout: visibility buffer + depth buffer
    std::array<vk::DescriptorSetLayoutBinding, 2> bindings;
    bindings[0] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_DEBUG_INPUT)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    bindings[1] = vk::DescriptorSetLayoutBinding{}
        .setBinding(BINDING_VISBUF_DEBUG_DEPTH_INPUT)
        .setDescriptorType(vk::DescriptorType::eCombinedImageSampler)
        .setDescriptorCount(1)
        .setStageFlags(vk::ShaderStageFlagBits::eFragment);

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}.setBindings(bindings);

    try {
        debugDescSetLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create debug desc set layout: %s", e.what());
        return false;
    }

    // Allocate debug descriptor set
    VkDescriptorSetLayout rawDebugLayout = **debugDescSetLayout_;
    auto debugSets = descriptorPool_->allocate(rawDebugLayout, 1);
    if (debugSets.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to allocate debug descriptor set");
        return false;
    }
    debugDescSet_ = debugSets[0];

    // Update debug descriptor set with V-buffer and depth views
    VkSampler rawSampler = **nearestSampler_;

    VkDescriptorImageInfo visInfo{};
    visInfo.sampler = rawSampler;
    visInfo.imageView = visibilityView_;
    visInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDescriptorImageInfo depthInfo{};
    depthInfo.sampler = rawSampler;
    depthInfo.imageView = depthView_;
    depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

    std::array<VkWriteDescriptorSet, 2> writes{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = debugDescSet_;
    writes[0].dstBinding = BINDING_VISBUF_DEBUG_INPUT;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &visInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = debugDescSet_;
    writes[1].dstBinding = BINDING_VISBUF_DEBUG_DEPTH_INPUT;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &depthInfo;

    vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Push constants
    vk::PushConstantRange pushRange{};
    pushRange.stageFlags = vk::ShaderStageFlagBits::eFragment;
    pushRange.offset = 0;
    pushRange.size = sizeof(VisBufDebugPushConstants);

    vk::DescriptorSetLayout vkDebugLayout(**debugDescSetLayout_);
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(vkDebugLayout)
        .setPushConstantRanges(pushRange);

    vk::Device vkDevice(device_);
    debugPipelineLayout_ = static_cast<VkPipelineLayout>(vkDevice.createPipelineLayout(pipelineLayoutInfo));

    // Load debug shaders
    auto vertModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_debug.vert.spv");
    auto fragModule = ShaderLoader::loadShaderModule(vkDevice, shaderPath_ + "/visbuf_debug.frag.spv");

    if (!vertModule || !fragModule) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to load debug shaders");
        return false;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages;
    shaderStages[0] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(*vertModule)
        .setPName("main");
    shaderStages[1] = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(*fragModule)
        .setPName("main");

    // No vertex input (fullscreen triangle)
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{};
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList);

    auto viewport = vk::Viewport{0.0f, 0.0f, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0.0f, 1.0f};
    auto scissor = vk::Rect2D{{0, 0}, {extent_.width, extent_.height}};

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setPViewports(&viewport)
        .setScissorCount(1)
        .setPScissors(&scissor);

    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setPolygonMode(vk::PolygonMode::eFill)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setLineWidth(1.0f);

    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_FALSE)
        .setDepthWriteEnable(VK_FALSE);

    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Debug pipeline renders to the swapchain render pass, not the V-buffer render pass.
    // We'll need the swapchain render pass to be passed in. For now, skip the
    // graphics pipeline creation and create it lazily when the render pass is known.
    // Store the shader modules for later creation.

    // Actually, the debug visualization will be rendered in the post-process pass's
    // output render pass. We don't have that here, so we'll create the pipeline lazily.
    // For now, just clean up.
    vkDevice.destroyShaderModule(*vertModule);
    vkDevice.destroyShaderModule(*fragModule);

    SDL_Log("VisibilityBuffer: Debug descriptor set created (pipeline deferred)");
    return true;
}

void VisibilityBuffer::destroyDebugPipeline() {
    nearestSampler_.reset();
    if (debugPipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, debugPipeline_, nullptr);
        debugPipeline_ = VK_NULL_HANDLE;
    }
    if (debugPipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, debugPipelineLayout_, nullptr);
        debugPipelineLayout_ = VK_NULL_HANDLE;
    }
    debugDescSetLayout_.reset();
}

// ============================================================================
// Resolve pipeline (compute)
// ============================================================================

bool VisibilityBuffer::createResolvePipeline() {
    // Will be created when needed (Phase 2)
    return true;
}

void VisibilityBuffer::destroyResolvePipeline() {
    if (resolvePipeline_ != VK_NULL_HANDLE) {
        vkDestroyPipeline(device_, resolvePipeline_, nullptr);
        resolvePipeline_ = VK_NULL_HANDLE;
    }
    if (resolvePipelineLayout_ != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device_, resolvePipelineLayout_, nullptr);
        resolvePipelineLayout_ = VK_NULL_HANDLE;
    }
    resolveDescSetLayout_.reset();
}

VkPipeline VisibilityBuffer::getResolvePipeline() const {
    return resolvePipeline_;
}

VkPipelineLayout VisibilityBuffer::getResolvePipelineLayout() const {
    return resolvePipelineLayout_;
}

// ============================================================================
// Descriptor sets
// ============================================================================

bool VisibilityBuffer::createDescriptorSets() {
    return true;
}

void VisibilityBuffer::destroyDescriptorSets() {
    resolveDescSets_.clear();
}

// ============================================================================
// Resolve buffers
// ============================================================================

bool VisibilityBuffer::createResolveBuffers() {
    VkDeviceSize uniformSize = sizeof(VisBufResolveUniforms);
    bool ok = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator_)
        .setFrameCount(framesInFlight_)
        .setSize(uniformSize)
        .setUsage(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(resolveUniformBuffers_);

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "VisibilityBuffer: Failed to create resolve uniform buffers");
        return false;
    }

    return true;
}

void VisibilityBuffer::destroyResolveBuffers() {
    BufferUtils::destroyBuffers(allocator_, resolveUniformBuffers_);
}

// ============================================================================
// Resize
// ============================================================================

void VisibilityBuffer::resize(VkExtent2D newExtent) {
    if (newExtent.width == extent_.width && newExtent.height == extent_.height) {
        return;
    }

    vkDeviceWaitIdle(device_);

    extent_ = newExtent;

    // Recreate size-dependent resources
    destroyFramebuffer();
    destroyRenderTargets();

    createRenderTargets();
    createFramebuffer();

    // Update debug descriptor set with new image views
    if (debugDescSet_ != VK_NULL_HANDLE && nearestSampler_) {
        VkSampler rawSampler = **nearestSampler_;

        VkDescriptorImageInfo visInfo{};
        visInfo.sampler = rawSampler;
        visInfo.imageView = visibilityView_;
        visInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkDescriptorImageInfo depthInfo{};
        depthInfo.sampler = rawSampler;
        depthInfo.imageView = depthView_;
        depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        std::array<VkWriteDescriptorSet, 2> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = debugDescSet_;
        writes[0].dstBinding = BINDING_VISBUF_DEBUG_INPUT;
        writes[0].descriptorCount = 1;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[0].pImageInfo = &visInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = debugDescSet_;
        writes[1].dstBinding = BINDING_VISBUF_DEBUG_DEPTH_INPUT;
        writes[1].descriptorCount = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].pImageInfo = &depthInfo;

        vkUpdateDescriptorSets(device_, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    SDL_Log("VisibilityBuffer: Resized to %ux%u", extent_.width, extent_.height);
}

// ============================================================================
// Command recording helpers
// ============================================================================

void VisibilityBuffer::transitionToShaderRead(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.image = visibilityImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VisibilityBuffer::transitionToColorAttachment(VkCommandBuffer cmd) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barrier.image = visibilityImage_.get();
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void VisibilityBuffer::recordClear(VkCommandBuffer cmd) {
    // V-buffer clear happens in the render pass (loadOp = CLEAR)
    // This method is for explicit clears if needed outside the render pass
    VkClearColorValue clearColor{};
    clearColor.uint32[0] = 0;  // 0 = no geometry

    VkImageSubresourceRange range{};
    range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    range.levelCount = 1;
    range.layerCount = 1;

    vkCmdClearColorImage(cmd, visibilityImage_.get(),
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          &clearColor, 1, &range);
}

void VisibilityBuffer::updateResolveUniforms(uint32_t frameIndex,
                                              const glm::mat4& view, const glm::mat4& proj,
                                              const glm::vec3& cameraPos,
                                              const glm::vec3& sunDir, float sunIntensity) {
    VisBufResolveUniforms uniforms{};
    uniforms.viewMatrix = view;
    uniforms.projMatrix = proj;
    uniforms.invViewProj = glm::inverse(proj * view);
    uniforms.cameraPosition = glm::vec4(cameraPos, 1.0f);
    uniforms.screenParams = glm::vec4(
        static_cast<float>(extent_.width),
        static_cast<float>(extent_.height),
        1.0f / static_cast<float>(extent_.width),
        1.0f / static_cast<float>(extent_.height)
    );
    uniforms.lightDirection = glm::vec4(sunDir, sunIntensity);

    void* mapped = resolveUniformBuffers_.mappedPointers[frameIndex];
    if (mapped) {
        memcpy(mapped, &uniforms, sizeof(uniforms));
        vmaFlushAllocation(allocator_, resolveUniformBuffers_.allocations[frameIndex],
                           0, sizeof(uniforms));
    }
}

void VisibilityBuffer::recordResolvePass(VkCommandBuffer cmd, uint32_t frameIndex,
                                          VkImageView hdrOutputView) {
    // Phase 2 implementation - for now this is a no-op
    (void)cmd;
    (void)frameIndex;
    (void)hdrOutputView;
}

void VisibilityBuffer::recordDebugVisualization(VkCommandBuffer cmd, uint32_t debugMode) {
    if (debugDescSet_ == VK_NULL_HANDLE || debugPipelineLayout_ == VK_NULL_HANDLE) {
        return;
    }

    vk::CommandBuffer vkCmd(cmd);

    // Bind debug descriptor set
    vkCmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                              vk::PipelineLayout(debugPipelineLayout_),
                              0, vk::DescriptorSet(debugDescSet_), {});

    // Push debug mode
    VisBufDebugPushConstants push{};
    push.mode = debugMode;
    vkCmd.pushConstants<VisBufDebugPushConstants>(
        vk::PipelineLayout(debugPipelineLayout_),
        vk::ShaderStageFlagBits::eFragment,
        0, push);

    // Draw fullscreen triangle (3 vertices, no vertex buffer needed)
    vkCmd.draw(3, 1, 0, 0);
}
