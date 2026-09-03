#include "DebugLineSystem.h"
#include "ShaderLoader.h"
#include "core/vulkan/VertexInputBuilder.h"
#include <SDL3/SDL_log.h>
#include <vulkan/vulkan.hpp>
#include <glm/gtc/constants.hpp>
#include <cstring>
#include <algorithm>

// Factory methods
std::unique_ptr<DebugLineSystem> DebugLineSystem::create(const vk::raii::Device& raiiDevice, VmaAllocator allocator,
                                                          vk::RenderPass renderPass,
                                                          const std::string& shaderPath,
                                                          uint32_t framesInFlight) {
    auto system = std::make_unique<DebugLineSystem>(ConstructToken{});
    if (!system->initInternal(raiiDevice, allocator, renderPass, shaderPath, framesInFlight)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<DebugLineSystem> DebugLineSystem::create(const InitContext& ctx, vk::RenderPass renderPass) {
    if (!ctx.raiiDevice) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem requires raiiDevice");
        return nullptr;
    }
    return create(*ctx.raiiDevice, ctx.allocator, renderPass, ctx.shaderPath, ctx.framesInFlight);
}

// Internal initialization
bool DebugLineSystem::initInternal(const vk::raii::Device& raiiDevice, VmaAllocator allocator, vk::RenderPass renderPass,
                                    const std::string& shaderPath, uint32_t framesInFlight) {
    this->raiiDevice_ = &raiiDevice;
    this->device = *raiiDevice;
    this->allocator = allocator;

    // Create per-frame data
    frameData.resize(framesInFlight);

    // Create pipeline
    if (!createPipeline(renderPass, shaderPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline");
        return false;
    }

    SDL_Log("DebugLineSystem: Initialized with %u frames in flight", framesInFlight);
    return true;
}

bool DebugLineSystem::createPipeline(vk::RenderPass renderPass, const std::string& shaderPath) {
    // Load shaders
    auto vertShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.vert.spv", ShaderLoader::RaiiTag{});
    auto fragShader = ShaderLoader::loadShaderModule(device, shaderPath + "/debug_line.frag.spv", ShaderLoader::RaiiTag{});

    if (!vertShader || !fragShader) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to load shaders");
        return false;
    }

    // Push constant for view-projection matrix
    auto pushConstantRange = vk::PushConstantRange{}
        .setStageFlags(vk::ShaderStageFlagBits::eVertex)
        .setOffset(0)
        .setSize(sizeof(glm::mat4));

    auto layoutInfo = vk::PipelineLayoutCreateInfo{}
        .setPushConstantRanges(pushConstantRange);

    try {
        pipelineLayout_.emplace(*raiiDevice_, layoutInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create pipeline layout: %s", e.what());
        return false;
    }

    // Shader stages
    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {{
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eVertex)
            .setModule(vertShader->get())
            .setPName("main"),
        vk::PipelineShaderStageCreateInfo{}
            .setStage(vk::ShaderStageFlagBits::eFragment)
            .setModule(fragShader->get())
            .setPName("main")
    }};

    // Vertex input: position (vec3) + color (vec4)
    auto vertexInput = VertexInputBuilder()
        .addBinding(VertexBindingBuilder::perVertex<DebugLineVertex>(0))
        .addAttribute(AttributeBuilder::vec3(0, offsetof(DebugLineVertex, position)))
        .addAttribute(AttributeBuilder::vec4(1, offsetof(DebugLineVertex, color)));

    auto vertexInputInfo = vertexInput.build();

    // Input assembly for lines
    auto inputAssemblyLine = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eLineList)
        .setPrimitiveRestartEnable(VK_FALSE);

    // Input assembly for triangles
    auto inputAssemblyTriangle = vk::PipelineInputAssemblyStateCreateInfo{}
        .setTopology(vk::PrimitiveTopology::eTriangleList)
        .setPrimitiveRestartEnable(VK_FALSE);

    // Dynamic viewport and scissor
    auto viewportState = vk::PipelineViewportStateCreateInfo{}
        .setViewportCount(1)
        .setScissorCount(1);

    std::array<vk::DynamicState, 2> dynamicStates = { vk::DynamicState::eViewport, vk::DynamicState::eScissor };
    auto dynamicState = vk::PipelineDynamicStateCreateInfo{}
        .setDynamicStateCount(static_cast<uint32_t>(dynamicStates.size()))
        .setPDynamicStates(dynamicStates.data());

    // Rasterization
    auto rasterizer = vk::PipelineRasterizationStateCreateInfo{}
        .setDepthClampEnable(VK_FALSE)
        .setRasterizerDiscardEnable(VK_FALSE)
        .setPolygonMode(vk::PolygonMode::eFill)
        .setLineWidth(1.0f)
        .setCullMode(vk::CullModeFlagBits::eNone)
        .setFrontFace(vk::FrontFace::eCounterClockwise)
        .setDepthBiasEnable(VK_FALSE);

    // Multisampling
    auto multisampling = vk::PipelineMultisampleStateCreateInfo{}
        .setSampleShadingEnable(VK_FALSE)
        .setRasterizationSamples(vk::SampleCountFlagBits::e1);

    // Depth stencil - read depth but don't write (overlay on top of scene)
    auto depthStencil = vk::PipelineDepthStencilStateCreateInfo{}
        .setDepthTestEnable(VK_TRUE)
        .setDepthWriteEnable(VK_FALSE)
        .setDepthCompareOp(vk::CompareOp::eLessOrEqual)
        .setDepthBoundsTestEnable(VK_FALSE)
        .setStencilTestEnable(VK_FALSE);

    // Color blending - alpha blending for semi-transparent debug visualization
    auto colorBlendAttachment = vk::PipelineColorBlendAttachmentState{}
        .setColorWriteMask(vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                           vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA)
        .setBlendEnable(VK_TRUE)
        .setSrcColorBlendFactor(vk::BlendFactor::eSrcAlpha)
        .setDstColorBlendFactor(vk::BlendFactor::eOneMinusSrcAlpha)
        .setColorBlendOp(vk::BlendOp::eAdd)
        .setSrcAlphaBlendFactor(vk::BlendFactor::eOne)
        .setDstAlphaBlendFactor(vk::BlendFactor::eZero)
        .setAlphaBlendOp(vk::BlendOp::eAdd);

    auto colorBlending = vk::PipelineColorBlendStateCreateInfo{}
        .setLogicOpEnable(VK_FALSE)
        .setAttachmentCount(1)
        .setPAttachments(&colorBlendAttachment);

    // Create line pipeline
    auto pipelineInfo = vk::GraphicsPipelineCreateInfo{}
        .setStageCount(static_cast<uint32_t>(shaderStages.size()))
        .setPStages(shaderStages.data())
        .setPVertexInputState(&vertexInputInfo)
        .setPInputAssemblyState(&inputAssemblyLine)
        .setPViewportState(&viewportState)
        .setPRasterizationState(&rasterizer)
        .setPMultisampleState(&multisampling)
        .setPDepthStencilState(&depthStencil)
        .setPColorBlendState(&colorBlending)
        .setPDynamicState(&dynamicState)
        .setLayout(**pipelineLayout_)
        .setRenderPass(renderPass)
        .setSubpass(0);

    try {
        linePipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create line pipeline");
        return false;
    }

    // Create triangle pipeline
    pipelineInfo.setPInputAssemblyState(&inputAssemblyTriangle);
    try {
        trianglePipeline_.emplace(*raiiDevice_, nullptr, pipelineInfo);
    } catch (const vk::SystemError&) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create triangle pipeline");
        linePipeline_.reset();
        return false;
    }

    return true;
}

void DebugLineSystem::beginFrame(uint32_t frameIndex) {
    currentFrame = frameIndex;
    lineVertices.clear();
    triangleVertices.clear();
}

void DebugLineSystem::addLine(const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
    lineVertices.push_back({start, color});
    lineVertices.push_back({end, color});
}

void DebugLineSystem::reserveLines(size_t lineCount) {
    lineVertices.reserve(lineVertices.size() + lineCount * 2);
}

void DebugLineSystem::reserveTriangles(size_t triangleCount) {
    triangleVertices.reserve(triangleVertices.size() + triangleCount * 3);
}

void DebugLineSystem::appendLineVertices(const DebugLineVertex* vertices, size_t count) {
    lineVertices.insert(lineVertices.end(), vertices, vertices + count);
}

void DebugLineSystem::appendTriangleVertices(const DebugLineVertex* vertices, size_t count) {
    triangleVertices.insert(triangleVertices.end(), vertices, vertices + count);
}

void DebugLineSystem::setPersistentLines(const DebugLineVertex* vertices, size_t count) {
    persistentLineVertices.assign(vertices, vertices + count);
}

void DebugLineSystem::clearPersistentLines() {
    persistentLineVertices.clear();
    persistentLineVertices.shrink_to_fit();
}

void DebugLineSystem::addTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, const glm::vec4& color) {
    triangleVertices.push_back({v0, color});
    triangleVertices.push_back({v1, color});
    triangleVertices.push_back({v2, color});
}

void DebugLineSystem::addBox(const glm::vec3& min, const glm::vec3& max, const glm::vec4& color) {
    // 12 edges of a box
    glm::vec3 corners[8] = {
        {min.x, min.y, min.z}, {max.x, min.y, min.z},
        {max.x, max.y, min.z}, {min.x, max.y, min.z},
        {min.x, min.y, max.z}, {max.x, min.y, max.z},
        {max.x, max.y, max.z}, {min.x, max.y, max.z}
    };
    // Bottom face
    addLine(corners[0], corners[1], color);
    addLine(corners[1], corners[2], color);
    addLine(corners[2], corners[3], color);
    addLine(corners[3], corners[0], color);
    // Top face
    addLine(corners[4], corners[5], color);
    addLine(corners[5], corners[6], color);
    addLine(corners[6], corners[7], color);
    addLine(corners[7], corners[4], color);
    // Vertical edges
    addLine(corners[0], corners[4], color);
    addLine(corners[1], corners[5], color);
    addLine(corners[2], corners[6], color);
    addLine(corners[3], corners[7], color);
}

void DebugLineSystem::addSphere(const glm::vec3& center, float radius, const glm::vec4& color, int segments) {
    const float step = glm::two_pi<float>() / static_cast<float>(segments);

    // XY circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(cosf(a0) * radius, sinf(a0) * radius, 0);
        glm::vec3 p1 = center + glm::vec3(cosf(a1) * radius, sinf(a1) * radius, 0);
        addLine(p0, p1, color);
    }
    // XZ circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(cosf(a0) * radius, 0, sinf(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(cosf(a1) * radius, 0, sinf(a1) * radius);
        addLine(p0, p1, color);
    }
    // YZ circle
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 p0 = center + glm::vec3(0, cosf(a0) * radius, sinf(a0) * radius);
        glm::vec3 p1 = center + glm::vec3(0, cosf(a1) * radius, sinf(a1) * radius);
        addLine(p0, p1, color);
    }
}

void DebugLineSystem::addCapsule(const glm::vec3& start, const glm::vec3& end, float radius, const glm::vec4& color, int segments) {
    // Draw the cylinder part
    glm::vec3 axis = end - start;
    float height = glm::length(axis);
    if (height < 0.0001f) {
        addSphere(start, radius, color, segments);
        return;
    }
    axis = glm::normalize(axis);

    // Find perpendicular vectors
    glm::vec3 perp1 = glm::abs(axis.y) < 0.9f ?
        glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0))) :
        glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));
    glm::vec3 perp2 = glm::cross(axis, perp1);

    const float step = glm::two_pi<float>() / static_cast<float>(segments);

    // Cylinder lines
    for (int i = 0; i < segments; i++) {
        float a = step * i;
        glm::vec3 offset = (cosf(a) * perp1 + sinf(a) * perp2) * radius;
        addLine(start + offset, end + offset, color);
    }

    // End cap circles
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 off0 = (cosf(a0) * perp1 + sinf(a0) * perp2) * radius;
        glm::vec3 off1 = (cosf(a1) * perp1 + sinf(a1) * perp2) * radius;
        addLine(start + off0, start + off1, color);
        addLine(end + off0, end + off1, color);
    }

    // Hemisphere arcs
    for (int i = 0; i < segments / 2; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        // Start hemisphere (pointing away from end)
        glm::vec3 p0 = start + (-axis * cosf(a0) + perp1 * sinf(a0)) * radius;
        glm::vec3 p1 = start + (-axis * cosf(a1) + perp1 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        p0 = start + (-axis * cosf(a0) + perp2 * sinf(a0)) * radius;
        p1 = start + (-axis * cosf(a1) + perp2 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        // End hemisphere (pointing away from start)
        p0 = end + (axis * cosf(a0) + perp1 * sinf(a0)) * radius;
        p1 = end + (axis * cosf(a1) + perp1 * sinf(a1)) * radius;
        addLine(p0, p1, color);
        p0 = end + (axis * cosf(a0) + perp2 * sinf(a0)) * radius;
        p1 = end + (axis * cosf(a1) + perp2 * sinf(a1)) * radius;
        addLine(p0, p1, color);
    }
}

void DebugLineSystem::addCone(const glm::vec3& base, const glm::vec3& tip, float radius, const glm::vec4& color, int segments) {
    glm::vec3 axis = tip - base;
    float height = glm::length(axis);
    if (height < 0.0001f) {
        return; // Degenerate cone
    }
    axis = glm::normalize(axis);

    // Find perpendicular vectors
    glm::vec3 perp1 = glm::abs(axis.y) < 0.9f ?
        glm::normalize(glm::cross(axis, glm::vec3(0, 1, 0))) :
        glm::normalize(glm::cross(axis, glm::vec3(1, 0, 0)));
    glm::vec3 perp2 = glm::cross(axis, perp1);

    const float step = glm::two_pi<float>() / static_cast<float>(segments);

    // Draw base circle and lines to tip
    for (int i = 0; i < segments; i++) {
        float a0 = step * i;
        float a1 = step * (i + 1);
        glm::vec3 off0 = (cosf(a0) * perp1 + sinf(a0) * perp2) * radius;
        glm::vec3 off1 = (cosf(a1) * perp1 + sinf(a1) * perp2) * radius;

        // Base circle edge
        addLine(base + off0, base + off1, color);

        // Side edges to tip
        addLine(base + off0, tip, color);
    }
}

#ifdef JPH_DEBUG_RENDERER
void DebugLineSystem::importFromPhysicsDebugRenderer(const PhysicsDebugRenderer& renderer) {
    // Import lines
    for (const auto& line : renderer.getLines()) {
        addLine(line.start, line.end, line.color);
    }

    // Import triangles (convert to wireframe lines)
    for (const auto& tri : renderer.getTriangles()) {
        addLine(tri.v0, tri.v1, tri.color);
        addLine(tri.v1, tri.v2, tri.color);
        addLine(tri.v2, tri.v0, tri.color);
    }
}
#endif

bool DebugLineSystem::ensureVertexBuffer(VertexBuffer& vb, size_t requiredSize, const char* what) {
    if (vb.size >= requiredSize) return true;

    // Releasing the old buffer here is safe: this frame slot was last used
    // framesInFlight frames ago, so no submitted work references it.
    vb.buffer = ManagedBuffer();
    vb.mapped = nullptr;
    vb.size = 0;

    size_t newSize = std::max(requiredSize, INITIAL_BUFFER_SIZE);

    auto bufferInfo = vk::BufferCreateInfo{}
        .setSize(newSize)
        .setUsage(vk::BufferUsageFlagBits::eVertexBuffer)
        .setSharingMode(vk::SharingMode::eExclusive);

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    if (!VmaBuffer::create(allocator, bufferInfo, allocInfo, vb.buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "DebugLineSystem: Failed to create %s vertex buffer", what);
        return false;
    }

    VmaAllocationInfo allocationInfo{};
    vmaGetAllocationInfo(allocator, vb.buffer.getAllocation(), &allocationInfo);
    vb.mapped = allocationInfo.pMappedData;
    vb.size = newSize;
    return true;
}

void DebugLineSystem::uploadLines() {
    if (currentFrame >= frameData.size()) return;

    auto& frame = frameData[currentFrame];

    // Calculate total line vertices (persistent + per-frame)
    size_t totalLineVertices = persistentLineVertices.size() + lineVertices.size();

    // Upload lines (persistent + per-frame combined)
    if (totalLineVertices > 0) {
        size_t requiredSize = totalLineVertices * sizeof(DebugLineVertex);

        // Recreate buffer if too small
        if (!ensureVertexBuffer(frame.lines, requiredSize, "line")) {
            return;
        }

        // Copy persistent lines first, then per-frame lines
        char* dst = static_cast<char*>(frame.lines.mapped);
        if (!persistentLineVertices.empty()) {
            size_t persistentSize = persistentLineVertices.size() * sizeof(DebugLineVertex);
            memcpy(dst, persistentLineVertices.data(), persistentSize);
            dst += persistentSize;
        }
        if (!lineVertices.empty()) {
            memcpy(dst, lineVertices.data(), lineVertices.size() * sizeof(DebugLineVertex));
        }
    }

    // Upload triangles
    if (!triangleVertices.empty()) {
        size_t requiredSize = triangleVertices.size() * sizeof(DebugLineVertex);

        if (!ensureVertexBuffer(frame.triangles, requiredSize, "triangle")) {
            return;
        }

        memcpy(frame.triangles.mapped, triangleVertices.data(), requiredSize);
    }
}

void DebugLineSystem::recordCommands(vk::CommandBuffer cmd, const glm::mat4& viewProj) {
    if (currentFrame >= frameData.size()) return;

    auto& frame = frameData[currentFrame];

    vk::CommandBuffer vkCmd(cmd);

    // Draw lines (persistent + per-frame)
    size_t totalLineVertices = persistentLineVertices.size() + lineVertices.size();
    if (totalLineVertices > 0 && frame.lines.buffer) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **linePipeline_);
        vkCmd.pushConstants<glm::mat4>(**pipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, viewProj);

        vk::Buffer buffer = frame.lines.buffer.get();
        vk::DeviceSize offset = 0;
        vkCmd.bindVertexBuffers(0, buffer, offset);
        vkCmd.draw(static_cast<uint32_t>(totalLineVertices), 1, 0, 0);
    }

    // Draw triangles (as wireframe)
    if (!triangleVertices.empty() && frame.triangles.buffer) {
        vkCmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **trianglePipeline_);
        vkCmd.pushConstants<glm::mat4>(**pipelineLayout_, vk::ShaderStageFlagBits::eVertex, 0, viewProj);

        vk::Buffer buffer = frame.triangles.buffer.get();
        vk::DeviceSize offset = 0;
        vkCmd.bindVertexBuffers(0, buffer, offset);
        vkCmd.draw(static_cast<uint32_t>(triangleVertices.size()), 1, 0, 0);
    }
}
