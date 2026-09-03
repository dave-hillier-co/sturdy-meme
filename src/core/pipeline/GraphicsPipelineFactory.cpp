#include "GraphicsPipelineFactory.h"
#include "ShaderLoader.h"
#include <SDL3/SDL.h>

GraphicsPipelineFactory::GraphicsPipelineFactory(vk::Device device) : device(device) {
    initDefaultColorBlendAttachment();
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setPipelineCache(vk::PipelineCache cache) {
    pipelineCacheHandle = cache;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::reset() {
    shaderModules.clear();

    vertShaderPath.clear();
    fragShaderPath.clear();
    tescShaderPath.clear();
    teseShaderPath.clear();
    renderPass = vk::RenderPass{};
    subpass = 0;
    pipelineLayout = vk::PipelineLayout{};
    pipelineCacheHandle = vk::PipelineCache{};
    extent = {0, 0};
    dynamicViewport = false;
    vertexBindings.clear();
    vertexAttributes.clear();
    topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    cullMode = VK_CULL_MODE_BACK_BIT;
    frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    polygonMode = VK_POLYGON_MODE_FILL;
    lineWidth = 1.0f;
    depthClampEnable = false;
    depthBiasEnable = false;
    depthBiasConstant = 0.0f;
    depthBiasSlope = 0.0f;
    sampleCount = VK_SAMPLE_COUNT_1_BIT;
    alphaToCoverageEnable = false;
    alphaToOneEnable = false;
    depthTestEnable = true;
    depthWriteEnable = true;
    depthCompareOp = VK_COMPARE_OP_LESS;
    depthBoundsTestEnable = false;
    minDepthBounds = 0.0f;
    maxDepthBounds = 1.0f;
    stencilTestEnable = false;
    hasColorAttachments = true;
    colorAttachmentCount = 1;
    initDefaultColorBlendAttachment();

    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::applyPreset(Preset preset) {
    switch (preset) {
        case Preset::Default:
            // Standard 3D rendering - use current defaults
            depthTestEnable = true;
            depthWriteEnable = true;
            cullMode = VK_CULL_MODE_BACK_BIT;
            break;

        case Preset::FullscreenQuad:
            // Post-processing / fullscreen effects
            vertexBindings.clear();
            vertexAttributes.clear();
            depthTestEnable = false;
            depthWriteEnable = false;
            cullMode = VK_CULL_MODE_NONE;
            setBlendMode(BlendMode::None);
            break;

        case Preset::Shadow:
            // Depth-only shadow rendering
            depthTestEnable = true;
            depthWriteEnable = true;
            depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            cullMode = VK_CULL_MODE_BACK_BIT;
            depthBiasEnable = true;
            depthBiasConstant = 1.25f;
            depthBiasSlope = 1.75f;
            hasColorAttachments = false;
            break;

        case Preset::Particle:
            // Particle rendering with alpha blending
            depthTestEnable = true;
            depthWriteEnable = false;
            cullMode = VK_CULL_MODE_NONE;
            setBlendMode(BlendMode::Alpha);
            break;
    }
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setShaders(const std::string& vertPath, const std::string& fragPath) {
    vertShaderPath = vertPath;
    fragShaderPath = fragPath;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setVertexShader(const std::string& path) {
    vertShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setFragmentShader(const std::string& path) {
    fragShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setTessellationShaders(const std::string& tescPath, const std::string& tesePath) {
    tescShaderPath = tescPath;
    teseShaderPath = tesePath;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setTessellationControlShader(const std::string& path) {
    tescShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setTessellationEvaluationShader(const std::string& path) {
    teseShaderPath = path;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setRenderPass(vk::RenderPass pass, uint32_t sub) {
    renderPass = pass;
    subpass = sub;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setPipelineLayout(vk::PipelineLayout layout) {
    pipelineLayout = layout;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setExtent(VkExtent2D ext) {
    extent = ext;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDynamicViewport(bool dynamic) {
    dynamicViewport = dynamic;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setVertexInput(
    const std::vector<VkVertexInputBindingDescription>& bindings,
    const std::vector<VkVertexInputAttributeDescription>& attributes) {
    vertexBindings = bindings;
    vertexAttributes = attributes;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setNoVertexInput() {
    vertexBindings.clear();
    vertexAttributes.clear();
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setTopology(VkPrimitiveTopology topo) {
    topology = topo;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setCullMode(VkCullModeFlags mode) {
    cullMode = mode;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setFrontFace(VkFrontFace face) {
    frontFace = face;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setPolygonMode(VkPolygonMode mode) {
    polygonMode = mode;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthBias(float constantFactor, float slopeFactor) {
    depthBiasEnable = true;
    depthBiasConstant = constantFactor;
    depthBiasSlope = slopeFactor;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthClamp(bool enable) {
    depthClampEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setLineWidth(float width) {
    lineWidth = width;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setSampleCount(VkSampleCountFlagBits samples) {
    sampleCount = samples;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setAlphaToCoverage(bool enable) {
    alphaToCoverageEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setAlphaToOne(bool enable) {
    alphaToOneEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthTest(bool enable) {
    depthTestEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthWrite(bool enable) {
    depthWriteEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthCompareOp(VkCompareOp op) {
    depthCompareOp = op;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setDepthBoundsTest(bool enable, float minBounds, float maxBounds) {
    depthBoundsTestEnable = enable;
    minDepthBounds = minBounds;
    maxDepthBounds = maxBounds;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setStencilTest(bool enable) {
    stencilTestEnable = enable;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setBlendMode(BlendMode mode) {
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

    switch (mode) {
        case BlendMode::None:
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;

        case BlendMode::Alpha:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;

        case BlendMode::Premultiplied:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorBlendAttachment(
    const VkPipelineColorBlendAttachmentState& attachment) {
    colorBlendAttachment = attachment;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorWriteMask(VkColorComponentFlags mask) {
    colorBlendAttachment.colorWriteMask = mask;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setNoColorAttachments() {
    hasColorAttachments = false;
    return *this;
}

GraphicsPipelineFactory& GraphicsPipelineFactory::setColorAttachmentCount(uint32_t count) {
    colorAttachmentCount = count;
    hasColorAttachments = count > 0;
    return *this;
}

void GraphicsPipelineFactory::initDefaultColorBlendAttachment() {
    colorBlendAttachment = {};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;
}

bool GraphicsPipelineFactory::loadShaderModules(std::vector<VkPipelineShaderStageCreateInfo>& stages) {
    if (vertShaderPath.empty() || fragShaderPath.empty()) {
        SDL_Log("GraphicsPipelineFactory: Shader paths not set");
        return false;
    }

    auto vertCode = ShaderLoader::readFile(vertShaderPath);
    auto fragCode = ShaderLoader::readFile(fragShaderPath);

    if (!vertCode || !fragCode) {
        SDL_Log("GraphicsPipelineFactory: Failed to read shader files");
        return false;
    }

    // Scoped modules destroy themselves on every early return below.
    auto vertModule = ShaderLoader::createShaderModule(device, *vertCode, ShaderLoader::RaiiTag{});
    auto fragModule = ShaderLoader::createShaderModule(device, *fragCode, ShaderLoader::RaiiTag{});

    if (!vertModule || !fragModule) {
        SDL_Log("GraphicsPipelineFactory: Failed to create shader modules");
        return false;
    }

    auto vertStage = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eVertex)
        .setModule(vertModule->get())
        .setPName("main");

    auto fragStage = vk::PipelineShaderStageCreateInfo{}
        .setStage(vk::ShaderStageFlagBits::eFragment)
        .setModule(fragModule->get())
        .setPName("main");

    shaderModules.push_back(std::move(*vertModule));
    shaderModules.push_back(std::move(*fragModule));

    stages.push_back(vertStage);

    // Load tessellation shaders if provided
    if (!tescShaderPath.empty() && !teseShaderPath.empty()) {
        auto tescCode = ShaderLoader::readFile(tescShaderPath);
        auto teseCode = ShaderLoader::readFile(teseShaderPath);

        if (!tescCode || !teseCode) {
            SDL_Log("GraphicsPipelineFactory: Failed to read tessellation shader files");
            return false;
        }

        auto tescModule = ShaderLoader::createShaderModule(device, *tescCode, ShaderLoader::RaiiTag{});
        auto teseModule = ShaderLoader::createShaderModule(device, *teseCode, ShaderLoader::RaiiTag{});

        if (!tescModule || !teseModule) {
            SDL_Log("GraphicsPipelineFactory: Failed to create tessellation shader modules");
            return false;
        }

        auto tescStage = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eTessellationControl)
            .setModule(tescModule->get())
            .setPName("main");

        auto teseStage = vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eTessellationEvaluation)
            .setModule(teseModule->get())
            .setPName("main");

        shaderModules.push_back(std::move(*tescModule));
        shaderModules.push_back(std::move(*teseModule));

        stages.push_back(tescStage);
        stages.push_back(teseStage);
    }

    stages.push_back(fragStage);

    return true;
}

bool GraphicsPipelineFactory::build(vk::Pipeline& pipeline) {
    // Validate required state
    if (!renderPass) {
        SDL_Log("GraphicsPipelineFactory: Render pass not set");
        return false;
    }
    if (!pipelineLayout) {
        SDL_Log("GraphicsPipelineFactory: Pipeline layout not set");
        return false;
    }
    if (!dynamicViewport && (extent.width == 0 || extent.height == 0)) {
        SDL_Log("GraphicsPipelineFactory: Extent not set and viewport is not dynamic");
        return false;
    }

    // Load shaders (a failed load leaves any partially loaded modules to be
    // released here, before the next build)
    shaderModules.clear();
    std::vector<VkPipelineShaderStageCreateInfo> shaderStages;
    if (!loadShaderModules(shaderStages)) {
        shaderModules.clear();
        return false;
    }

    // Vertex input state
    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptionCount(static_cast<uint32_t>(vertexBindings.size()))
        .setPVertexBindingDescriptions(reinterpret_cast<const vk::VertexInputBindingDescription*>(vertexBindings.data()))
        .setVertexAttributeDescriptionCount(static_cast<uint32_t>(vertexAttributes.size()))
        .setPVertexAttributeDescriptions(reinterpret_cast<const vk::VertexInputAttributeDescription*>(vertexAttributes.data()));

    // Check if tessellation is enabled
    bool hasTessellation = !tescShaderPath.empty() && !teseShaderPath.empty();

    // Input assembly state - when using tessellation, topology must be patch list
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(hasTessellation ? vk::PrimitiveTopology::ePatchList : static_cast<vk::PrimitiveTopology>(topology))
        .setPrimitiveRestartEnable(false);

    // Tessellation state (only used when tessellation shaders are present)
    auto tessellationState = vk::PipelineTessellationStateCreateInfo{}
        .setPatchControlPoints(3);  // Triangles - 3 control points per patch

    // Viewport and scissor
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent.width))
        .setHeight(static_cast<float>(extent.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{extent.width, extent.height});

    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);
    if (!dynamicViewport) {
        viewportState.setPViewports(&viewport).setPScissors(&scissor);
    }

    // Rasterization state
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(depthClampEnable)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(static_cast<vk::PolygonMode>(polygonMode))
        .setLineWidth(lineWidth)
        .setCullMode(static_cast<vk::CullModeFlags>(cullMode))
        .setFrontFace(static_cast<vk::FrontFace>(frontFace))
        .setDepthBiasEnable(depthBiasEnable)
        .setDepthBiasConstantFactor(depthBiasConstant)
        .setDepthBiasSlopeFactor(depthBiasSlope)
        .setDepthBiasClamp(0.0f);

    // Multisampling state
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(false)
        .setRasterizationSamples(static_cast<vk::SampleCountFlagBits>(sampleCount))
        .setAlphaToCoverageEnable(alphaToCoverageEnable)
        .setAlphaToOneEnable(alphaToOneEnable);

    // Depth/stencil state
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(depthTestEnable)
        .setDepthWriteEnable(depthWriteEnable)
        .setDepthCompareOp(static_cast<vk::CompareOp>(depthCompareOp))
        .setDepthBoundsTestEnable(depthBoundsTestEnable)
        .setMinDepthBounds(minDepthBounds)
        .setMaxDepthBounds(maxDepthBounds)
        .setStencilTestEnable(stencilTestEnable);

    // Color blend state
    // For MRT, we create an array of blend attachments (all using the same settings)
    std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments(colorAttachmentCount, colorBlendAttachment);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(false);
    if (hasColorAttachments) {
        colorBlending.setAttachmentCount(colorAttachmentCount)
            .setPAttachments(reinterpret_cast<const vk::PipelineColorBlendAttachmentState*>(colorBlendAttachments.data()));
    }

    // Dynamic states
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{};
    if (dynamicViewport) {
        dynamicState.setDynamicStates(dynamicStates);
    }

    // Create the pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(reinterpret_cast<const vk::PipelineShaderStageCreateInfo*>(shaderStages.data()))
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPTessellationState(hasTessellation ? &tessellationState : nullptr)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(dynamicViewport ? &dynamicState : nullptr)
        .setLayout(pipelineLayout)
        .setRenderPass(renderPass)
        .setSubpass(subpass);

    auto result = device.createGraphicsPipelines(pipelineCacheHandle, pipelineInfo);
    shaderModules.clear();  // modules are only needed until pipeline creation

    pipeline = result.value[0];
    return true;
}
