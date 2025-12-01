#include "CatmullClarkSystem.h"
#include "ShaderLoader.h"
#include "BufferUtils.h"
#include <iostream>
#include <array>
#include <cstring>

bool CatmullClarkSystem::init(const InitInfo& info, const CatmullClarkConfig& cfg) {
    device = info.device;
    physicalDevice = info.physicalDevice;
    allocator = info.allocator;
    renderPass = info.renderPass;
    descriptorPool = info.descriptorPool;
    extent = info.extent;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    graphicsQueue = info.graphicsQueue;
    commandPool = info.commandPool;
    config = cfg;

    // Create base mesh (cube for now)
    mesh = CatmullClarkMesh::createCube();
    if (!mesh.uploadToGPU(allocator)) {
        std::cerr << "Failed to upload Catmull-Clark mesh to GPU" << std::endl;
        return false;
    }

    // Initialize CBT
    CatmullClarkCBT::InitInfo cbtInfo{};
    cbtInfo.allocator = allocator;
    cbtInfo.maxDepth = config.maxDepth;
    cbtInfo.faceCount = static_cast<int>(mesh.faces.size());

    if (!cbt.init(cbtInfo)) {
        std::cerr << "Failed to initialize Catmull-Clark CBT" << std::endl;
        return false;
    }

    // Create buffers and pipelines
    if (!createUniformBuffers()) return false;
    if (!createIndirectBuffers()) return false;
    if (!createComputeDescriptorSetLayout()) return false;
    if (!createRenderDescriptorSetLayout()) return false;
    if (!createDescriptorSets()) return false;
    if (!createSubdivisionPipeline()) return false;
    if (!createRenderPipeline()) return false;
    if (!createWireframePipeline()) return false;

    std::cout << "Catmull-Clark subdivision system initialized" << std::endl;
    return true;
}

void CatmullClarkSystem::destroy(VkDevice device, VmaAllocator allocator) {
    // Destroy mesh buffers
    mesh.destroy(allocator);

    // Destroy CBT
    cbt.destroy(allocator);

    // Destroy indirect buffers
    if (indirectDispatchBuffer) {
        vmaDestroyBuffer(allocator, indirectDispatchBuffer, indirectDispatchAllocation);
    }
    if (indirectDrawBuffer) {
        vmaDestroyBuffer(allocator, indirectDrawBuffer, indirectDrawAllocation);
    }

    // Destroy uniform buffers
    for (size_t i = 0; i < uniformBuffers.size(); ++i) {
        if (uniformBuffers[i]) {
            vmaDestroyBuffer(allocator, uniformBuffers[i], uniformAllocations[i]);
        }
    }
    uniformBuffers.clear();
    uniformAllocations.clear();
    uniformMappedPtrs.clear();

    // Destroy pipelines
    if (subdivisionPipeline) vkDestroyPipeline(device, subdivisionPipeline, nullptr);
    if (renderPipeline) vkDestroyPipeline(device, renderPipeline, nullptr);
    if (wireframePipeline) vkDestroyPipeline(device, wireframePipeline, nullptr);

    // Destroy pipeline layouts
    if (subdivisionPipelineLayout) vkDestroyPipelineLayout(device, subdivisionPipelineLayout, nullptr);
    if (renderPipelineLayout) vkDestroyPipelineLayout(device, renderPipelineLayout, nullptr);

    // Destroy descriptor set layouts
    if (computeDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, computeDescriptorSetLayout, nullptr);
    if (renderDescriptorSetLayout) vkDestroyDescriptorSetLayout(device, renderDescriptorSetLayout, nullptr);
}

bool CatmullClarkSystem::createUniformBuffers() {
    VkDeviceSize bufferSize = sizeof(SceneUBO);

    uniformBuffers.resize(framesInFlight);
    uniformAllocations.resize(framesInFlight);
    uniformMappedPtrs.resize(framesInFlight);

    for (uint32_t i = 0; i < framesInFlight; ++i) {
        if (!createUniformBuffer(allocator, bufferSize, uniformBuffers[i],
                                  uniformAllocations[i], uniformMappedPtrs[i])) {
            std::cerr << "Failed to create Catmull-Clark uniform buffer " << i << std::endl;
            return false;
        }
    }

    return true;
}

bool CatmullClarkSystem::createIndirectBuffers() {
    // Indirect dispatch buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkDispatchIndirectCommand);
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDispatchBuffer,
                            &indirectDispatchAllocation, nullptr) != VK_SUCCESS) {
            std::cerr << "Failed to create indirect dispatch buffer" << std::endl;
            return false;
        }
    }

    // Indirect draw buffer
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = sizeof(VkDrawIndirectCommand);
        bufferInfo.usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

        if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &indirectDrawBuffer,
                            &indirectDrawAllocation, nullptr) != VK_SUCCESS) {
            std::cerr << "Failed to create indirect draw buffer" << std::endl;
            return false;
        }
    }

    return true;
}

bool CatmullClarkSystem::createComputeDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Binding 0: Scene UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: CBT buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Mesh vertices
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Mesh halfedges
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Mesh faces
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &computeDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create compute descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createRenderDescriptorSetLayout() {
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};

    // Binding 0: Scene UBO
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    // Binding 1: CBT buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 2: Mesh vertices
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 3: Mesh halfedges
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Binding 4: Mesh faces
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &renderDescriptorSetLayout) != VK_SUCCESS) {
        std::cerr << "Failed to create render descriptor set layout" << std::endl;
        return false;
    }

    return true;
}

bool CatmullClarkSystem::createDescriptorSets() {
    // Allocate compute descriptor sets
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, computeDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        computeDescriptorSets.resize(framesInFlight);
        if (vkAllocateDescriptorSets(device, &allocInfo, computeDescriptorSets.data()) != VK_SUCCESS) {
            std::cerr << "Failed to allocate compute descriptor sets" << std::endl;
            return false;
        }
    }

    // Allocate render descriptor sets
    {
        std::vector<VkDescriptorSetLayout> layouts(framesInFlight, renderDescriptorSetLayout);
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.descriptorSetCount = framesInFlight;
        allocInfo.pSetLayouts = layouts.data();

        renderDescriptorSets.resize(framesInFlight);
        if (vkAllocateDescriptorSets(device, &allocInfo, renderDescriptorSets.data()) != VK_SUCCESS) {
            std::cerr << "Failed to allocate render descriptor sets" << std::endl;
            return false;
        }
    }

    return true;
}

void CatmullClarkSystem::updateDescriptorSets(VkDevice device, const std::vector<VkBuffer>& sceneUniformBuffers) {
    for (uint32_t i = 0; i < framesInFlight; ++i) {
        std::vector<VkWriteDescriptorSet> descriptorWrites;

        // Compute descriptor set
        {
            VkDescriptorBufferInfo sceneBufferInfo{};
            sceneBufferInfo.buffer = sceneUniformBuffers[i];
            sceneBufferInfo.offset = 0;
            sceneBufferInfo.range = sizeof(SceneUBO);

            VkDescriptorBufferInfo cbtBufferInfo{};
            cbtBufferInfo.buffer = cbt.getBuffer();
            cbtBufferInfo.offset = 0;
            cbtBufferInfo.range = cbt.getBufferSize();

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = mesh.vertexBuffer;
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = mesh.vertices.size() * sizeof(CatmullClarkMesh::Vertex);

            VkDescriptorBufferInfo halfedgeBufferInfo{};
            halfedgeBufferInfo.buffer = mesh.halfedgeBuffer;
            halfedgeBufferInfo.offset = 0;
            halfedgeBufferInfo.range = mesh.halfedges.size() * sizeof(CatmullClarkMesh::Halfedge);

            VkDescriptorBufferInfo faceBufferInfo{};
            faceBufferInfo.buffer = mesh.faceBuffer;
            faceBufferInfo.offset = 0;
            faceBufferInfo.range = mesh.faces.size() * sizeof(CatmullClarkMesh::Face);

            VkWriteDescriptorSet write0{};
            write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write0.dstSet = computeDescriptorSets[i];
            write0.dstBinding = 0;
            write0.dstArrayElement = 0;
            write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write0.descriptorCount = 1;
            write0.pBufferInfo = &sceneBufferInfo;

            VkWriteDescriptorSet write1{};
            write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write1.dstSet = computeDescriptorSets[i];
            write1.dstBinding = 1;
            write1.dstArrayElement = 0;
            write1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write1.descriptorCount = 1;
            write1.pBufferInfo = &cbtBufferInfo;

            VkWriteDescriptorSet write2{};
            write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write2.dstSet = computeDescriptorSets[i];
            write2.dstBinding = 2;
            write2.dstArrayElement = 0;
            write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write2.descriptorCount = 1;
            write2.pBufferInfo = &vertexBufferInfo;

            VkWriteDescriptorSet write3{};
            write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write3.dstSet = computeDescriptorSets[i];
            write3.dstBinding = 3;
            write3.dstArrayElement = 0;
            write3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write3.descriptorCount = 1;
            write3.pBufferInfo = &halfedgeBufferInfo;

            VkWriteDescriptorSet write4{};
            write4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write4.dstSet = computeDescriptorSets[i];
            write4.dstBinding = 4;
            write4.dstArrayElement = 0;
            write4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write4.descriptorCount = 1;
            write4.pBufferInfo = &faceBufferInfo;

            vkUpdateDescriptorSets(device, 5, (VkWriteDescriptorSet[]){write0, write1, write2, write3, write4}, 0, nullptr);
        }

        // Render descriptor set (same buffers)
        {
            VkDescriptorBufferInfo sceneBufferInfo{};
            sceneBufferInfo.buffer = sceneUniformBuffers[i];
            sceneBufferInfo.offset = 0;
            sceneBufferInfo.range = sizeof(SceneUBO);

            VkDescriptorBufferInfo cbtBufferInfo{};
            cbtBufferInfo.buffer = cbt.getBuffer();
            cbtBufferInfo.offset = 0;
            cbtBufferInfo.range = cbt.getBufferSize();

            VkDescriptorBufferInfo vertexBufferInfo{};
            vertexBufferInfo.buffer = mesh.vertexBuffer;
            vertexBufferInfo.offset = 0;
            vertexBufferInfo.range = mesh.vertices.size() * sizeof(CatmullClarkMesh::Vertex);

            VkDescriptorBufferInfo halfedgeBufferInfo{};
            halfedgeBufferInfo.buffer = mesh.halfedgeBuffer;
            halfedgeBufferInfo.offset = 0;
            halfedgeBufferInfo.range = mesh.halfedges.size() * sizeof(CatmullClarkMesh::Halfedge);

            VkDescriptorBufferInfo faceBufferInfo{};
            faceBufferInfo.buffer = mesh.faceBuffer;
            faceBufferInfo.offset = 0;
            faceBufferInfo.range = mesh.faces.size() * sizeof(CatmullClarkMesh::Face);

            VkWriteDescriptorSet write0{};
            write0.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write0.dstSet = renderDescriptorSets[i];
            write0.dstBinding = 0;
            write0.dstArrayElement = 0;
            write0.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            write0.descriptorCount = 1;
            write0.pBufferInfo = &sceneBufferInfo;

            VkWriteDescriptorSet write1{};
            write1.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write1.dstSet = renderDescriptorSets[i];
            write1.dstBinding = 1;
            write1.dstArrayElement = 0;
            write1.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write1.descriptorCount = 1;
            write1.pBufferInfo = &cbtBufferInfo;

            VkWriteDescriptorSet write2{};
            write2.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write2.dstSet = renderDescriptorSets[i];
            write2.dstBinding = 2;
            write2.dstArrayElement = 0;
            write2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write2.descriptorCount = 1;
            write2.pBufferInfo = &vertexBufferInfo;

            VkWriteDescriptorSet write3{};
            write3.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write3.dstSet = renderDescriptorSets[i];
            write3.dstBinding = 3;
            write3.dstArrayElement = 0;
            write3.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write3.descriptorCount = 1;
            write3.pBufferInfo = &halfedgeBufferInfo;

            VkWriteDescriptorSet write4{};
            write4.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write4.dstSet = renderDescriptorSets[i];
            write4.dstBinding = 4;
            write4.dstArrayElement = 0;
            write4.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            write4.descriptorCount = 1;
            write4.pBufferInfo = &faceBufferInfo;

            vkUpdateDescriptorSets(device, 5, (VkWriteDescriptorSet[]){write0, write1, write2, write3, write4}, 0, nullptr);
        }
    }
}

bool CatmullClarkSystem::createSubdivisionPipeline() {
    // TODO: Load and compile subdivision compute shader
    // For now, return true (will be implemented with shaders)
    std::cout << "Note: Subdivision pipeline not yet implemented (needs shader)" << std::endl;
    return true;
}

bool CatmullClarkSystem::createRenderPipeline() {
    // TODO: Load and compile render shaders
    // For now, return true (will be implemented with shaders)
    std::cout << "Note: Render pipeline not yet implemented (needs shaders)" << std::endl;
    return true;
}

bool CatmullClarkSystem::createWireframePipeline() {
    // TODO: Load and compile wireframe shaders
    // For now, return true (will be implemented with shaders)
    std::cout << "Note: Wireframe pipeline not yet implemented (needs shaders)" << std::endl;
    return true;
}

void CatmullClarkSystem::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos,
                                         const glm::mat4& view, const glm::mat4& proj) {
    // Currently a stub - uniforms will be updated when rendering is implemented
}

void CatmullClarkSystem::recordCompute(VkCommandBuffer cmd, uint32_t frameIndex) {
    // TODO: Record subdivision compute pass
    // For now, this is a stub
}

void CatmullClarkSystem::recordDraw(VkCommandBuffer cmd, uint32_t frameIndex) {
    // TODO: Record rendering commands
    // For now, this is a stub
}
