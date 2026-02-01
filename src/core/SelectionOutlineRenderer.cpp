#include "SelectionOutlineRenderer.h"
#include "ShaderLoader.h"
#include "Mesh.h"
#include "material/IDescriptorAllocator.h"
#include "ecs/ECSMaterialDemo.h"
#include <SDL3/SDL_log.h>
#include <array>

std::unique_ptr<SelectionOutlineRenderer> SelectionOutlineRenderer::create(const InitInfo& info) {
    auto instance = std::make_unique<SelectionOutlineRenderer>(ConstructToken{});
    if (!instance->initInternal(info)) {
        return nullptr;
    }
    return instance;
}

SelectionOutlineRenderer::~SelectionOutlineRenderer() {
    // RAII handles cleanup via optional members
}

bool SelectionOutlineRenderer::initInternal(const InitInfo& info) {
    if (!info.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: raiiDevice is required");
        return false;
    }

    raiiDevice_ = info.raiiDevice;
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    descriptorAllocator_ = info.descriptorAllocator;
    resourcePath_ = info.resourcePath;
    extent_ = info.extent;
    maxFramesInFlight_ = info.maxFramesInFlight;

    if (!createDescriptorSetLayout()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: Failed to create descriptor set layout");
        return false;
    }

    if (!allocateDescriptorSets(info.maxFramesInFlight)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: Failed to allocate descriptor sets");
        return false;
    }

    if (!createPipeline(info)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: Failed to create pipeline");
        return false;
    }

    SDL_Log("SelectionOutlineRenderer: Initialized successfully");
    return true;
}

bool SelectionOutlineRenderer::createDescriptorSetLayout() {
    // Layout: binding 0 = GlobalUBO
    std::array<vk::DescriptorSetLayoutBinding, 1> bindings = {{
        vk::DescriptorSetLayoutBinding{}
            .setBinding(0)
            .setDescriptorType(vk::DescriptorType::eUniformBuffer)
            .setDescriptorCount(1)
            .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
    }};

    auto layoutInfo = vk::DescriptorSetLayoutCreateInfo{}
        .setBindings(bindings);

    descriptorSetLayout_ = raiiDevice_->createDescriptorSetLayout(layoutInfo);
    return true;
}

bool SelectionOutlineRenderer::allocateDescriptorSets(uint32_t maxFramesInFlight) {
    if (!descriptorAllocator_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: No descriptor allocator");
        return false;
    }

    descriptorSets_.resize(maxFramesInFlight);
    for (uint32_t i = 0; i < maxFramesInFlight; ++i) {
        descriptorSets_[i] = descriptorAllocator_->allocateSingle(**descriptorSetLayout_);
        if (descriptorSets_[i] == VK_NULL_HANDLE) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: Failed to allocate descriptor set %u", i);
            return false;
        }
    }

    return true;
}

bool SelectionOutlineRenderer::createPipeline(const InitInfo& info) {
    // Load shaders
    std::string vertPath = resourcePath_ + "/shaders/selection_outline.vert.spv";
    std::string fragPath = resourcePath_ + "/shaders/selection_outline.frag.spv";

    auto vertShader = ShaderLoader::loadShaderModule(*raiiDevice_, vertPath);
    auto fragShader = ShaderLoader::loadShaderModule(*raiiDevice_, fragPath);

    if (!vertShader || !fragShader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SelectionOutlineRenderer: Failed to load shaders");
        return false;
    }

    // Shader stages
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(**vertShader)
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(**fragShader)
            .setPName("main")
    }};

    // Vertex input - standard mesh format (position, normal, texcoord)
    auto bindingDesc = vk::VertexInputBindingDescription{}
        .setBinding(0)
        .setStride(sizeof(Vertex))
        .setInputRate(vk::VertexInputRate::eVertex);

    std::array<vk::VertexInputAttributeDescription, 3> attrDescs = {{
        vk::VertexInputAttributeDescription{}
            .setLocation(0)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Vertex, position)),
        vk::VertexInputAttributeDescription{}
            .setLocation(1)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32B32Sfloat)
            .setOffset(offsetof(Vertex, normal)),
        vk::VertexInputAttributeDescription{}
            .setLocation(2)
            .setBinding(0)
            .setFormat(vk::Format::eR32G32Sfloat)
            .setOffset(offsetof(Vertex, texCoord))
    }};

    auto vertexInputInfo = vk::PipelineVertexInputStateCreateInfo{}
        .setVertexBindingDescriptions(bindingDesc)
        .setVertexAttributeDescriptions(attrDescs);

    // Input assembly
    auto inputAssembly = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(false);

    // Viewport/scissor (dynamic)
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    // Rasterization - render front faces only, with depth bias to push outlines behind geometry
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(false)
        .setRasterizerDiscardEnable(false)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eFront)  // Render back faces for outline
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(false);

    // Multisampling
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(false)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // Depth/stencil - depth test enabled, no stencil
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(true)
        .setDepthWriteEnable(false)  // Don't write depth for outlines
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setDepthBoundsTestEnable(false)
        .setStencilTestEnable(false);

    // Color blending - alpha blending for smooth outlines
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(
            vk::ColorComponentFlagBits::eR |
            vk::ColorComponentFlagBits::eG |
            vk::ColorComponentFlagBits::eB |
            vk::ColorComponentFlagBits::eA)
        .setBlendEnable(true)
        .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(false)
        .setAttachments(colorBlendAttachment);

    // Dynamic states
    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };

    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStates(dynamicStates);

    // Push constant range
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment)
        .setOffset(0)
        .setSize(sizeof(OutlinePushConstants));

    // Pipeline layout
    auto pipelineLayoutInfo = vk::PipelineLayoutCreateInfo{}
        .setSetLayouts(**descriptorSetLayout_)
        .setPushConstantRanges(pushConstantRange);

    pipelineLayout_ = raiiDevice_->createPipelineLayout(pipelineLayoutInfo);

    // Create graphics pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStages(shaderStages)
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssembly)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**pipelineLayout_)
        .setRenderPass(info.renderPass)
        .setSubpass(0);

    pipeline_ = raiiDevice_->createGraphicsPipeline(nullptr, pipelineInfo);
    SDL_Log("SelectionOutlineRenderer: Pipeline created successfully");
    return true;
}

void SelectionOutlineRenderer::updateDescriptorSet(uint32_t frameIndex, vk::Buffer globalUBO) {
    if (frameIndex >= descriptorSets_.size()) return;

    auto bufferInfo = vk::DescriptorBufferInfo{}
        .setBuffer(globalUBO)
        .setOffset(0)
        .setRange(VK_WHOLE_SIZE);

    auto write = vk::WriteDescriptorSet{}
        .setDstSet(descriptorSets_[frameIndex])
        .setDstBinding(0)
        .setDstArrayElement(0)
        .setDescriptorType(vk::DescriptorType::eUniformBuffer)
        .setDescriptorCount(1)
        .setPBufferInfo(&bufferInfo);

    device_.updateDescriptorSets(write, {});
}

void SelectionOutlineRenderer::render(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                       const ecs::World& world) {
    // Gather entities with selection outlines
    auto selectedEntities = ecs::gatherSelectedEntities(world);

    if (selectedEntities.empty()) return;

    renderEntities(cmd, frameIndex, time, selectedEntities, world);
}

void SelectionOutlineRenderer::renderEntities(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                                               const std::vector<ecs::SelectionRenderData>& entities,
                                               const ecs::World& world) {
    if (entities.empty()) return;
    if (!pipeline_ || !pipelineLayout_) return;
    if (frameIndex >= descriptorSets_.size()) return;

    // Bind pipeline
    cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pipeline_);

    // Bind descriptor set
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, **pipelineLayout_,
                           0, vk::DescriptorSet(descriptorSets_[frameIndex]), {});

    // Set viewport and scissor
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(extent_.width))
        .setHeight(static_cast<float>(extent_.height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    cmd.setViewport(0, viewport);

    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(extent_);
    cmd.setScissor(0, scissor);

    // Render each selected entity
    for (const auto& data : entities) {
        // Skip entities without required components
        if (!world.has<ecs::Transform>(data.entity)) continue;
        if (!world.has<ecs::MeshRef>(data.entity)) continue;

        const auto& transform = world.get<ecs::Transform>(data.entity);
        const auto& meshRef = world.get<ecs::MeshRef>(data.entity);

        if (!meshRef.valid() || !meshRef.mesh) continue;
        Mesh* mesh = meshRef.mesh;

        // Set up push constants
        OutlinePushConstants push{};
        push.model = transform.matrix;
        push.outlineColor = glm::vec4(data.color, 1.0f);
        push.outlineThickness = data.thickness * 0.01f;  // Scale to world units
        push.pulseSpeed = data.pulseSpeed;

        cmd.pushConstants<OutlinePushConstants>(**pipelineLayout_,
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, push);

        // Bind vertex buffer
        vk::Buffer vertexBuffers[] = {mesh->getVertexBuffer()};
        vk::DeviceSize offsets[] = {0};
        cmd.bindVertexBuffers(0, vertexBuffers, offsets);

        // Bind index buffer and draw
        cmd.bindIndexBuffer(mesh->getIndexBuffer(), 0, vk::IndexType::eUint32);
        cmd.drawIndexed(mesh->getIndexCount(), 1, 0, 0, 0);
    }
}

vk::PipelineLayout SelectionOutlineRenderer::getPipelineLayout() const {
    return pipelineLayout_ ? **pipelineLayout_ : vk::PipelineLayout{};
}
