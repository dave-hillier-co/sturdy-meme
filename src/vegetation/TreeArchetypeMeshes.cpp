#include "TreeArchetypeMeshes.h"
#include "TreeSystem.h"
#include "TreeOptions.h"
#include "TreeGenerator.h"
#include "Mesh.h"
#include <SDL3/SDL.h>

std::unique_ptr<TreeArchetypeMeshes> TreeArchetypeMeshes::create(const InitInfo& info) {
    auto instance = std::unique_ptr<TreeArchetypeMeshes>(new TreeArchetypeMeshes());
    if (!instance->init(info)) {
        return nullptr;
    }
    return instance;
}

TreeArchetypeMeshes::~TreeArchetypeMeshes() {
    cleanup();
}

bool TreeArchetypeMeshes::init(const InitInfo& info) {
    device_ = info.device;
    physicalDevice_ = info.physicalDevice;
    allocator_ = info.allocator;
    commandPool_ = info.commandPool;
    graphicsQueue_ = info.graphicsQueue;
    resourcePath_ = info.resourcePath;

    // Initialize all archetypes as invalid
    for (auto& arch : archetypes_) {
        arch.valid = false;
    }

    initialized_ = true;
    return true;
}

void TreeArchetypeMeshes::cleanup() {
    if (device_ == VK_NULL_HANDLE) return;

    vkDeviceWaitIdle(device_);

    for (auto& arch : archetypes_) {
        destroyArchetype(arch);
    }

    initialized_ = false;
}

void TreeArchetypeMeshes::destroyArchetype(ArchetypeMesh& archetype) {
    if (archetype.vertexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, archetype.vertexBuffer, archetype.vertexAllocation);
        archetype.vertexBuffer = VK_NULL_HANDLE;
        archetype.vertexAllocation = VK_NULL_HANDLE;
    }
    if (archetype.indexBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, archetype.indexBuffer, archetype.indexAllocation);
        archetype.indexBuffer = VK_NULL_HANDLE;
        archetype.indexAllocation = VK_NULL_HANDLE;
    }
    archetype.indexCount = 0;
    archetype.vertexCount = 0;
    archetype.valid = false;
}

bool TreeArchetypeMeshes::buildFromPresets(const TreeSystem& /*treeSystem*/) {
    // Build archetypes from tree presets (oak, pine, ash, aspen)
    // Each uses the default preset from TreeOptions
    struct PresetInfo {
        std::string name;
        TreeOptions (*getOptions)();
    };

    const std::array<PresetInfo, 4> presets = {{
        {"oak",   TreeOptions::defaultOak},
        {"pine",  TreeOptions::defaultPine},
        {"ash",   TreeOptions::defaultOak},    // Use oak variant for ash
        {"aspen", TreeOptions::defaultAspen}
    }};

    for (uint32_t i = 0; i < presets.size(); ++i) {
        TreeOptions options = presets[i].getOptions();

        if (!buildArchetype(i, presets[i].name, options)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "TreeArchetypeMeshes: Failed to build archetype %s", presets[i].name.c_str());
            return false;
        }
    }

    archetypeCount_ = 4;
    SDL_Log("TreeArchetypeMeshes: Built %u archetypes", archetypeCount_);
    return true;
}

bool TreeArchetypeMeshes::buildArchetype(uint32_t archetypeIndex, const std::string& name,
                                          const TreeOptions& options) {
    if (archetypeIndex >= MAX_ARCHETYPES) {
        return false;
    }

    // Destroy existing archetype if any
    destroyArchetype(archetypes_[archetypeIndex]);

    // Generate tree mesh using TreeGenerator
    TreeGenerator generator;
    TreeMeshData meshData = generator.generate(options);

    if (meshData.branches.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "TreeArchetypeMeshes: Empty mesh generated for %s", name.c_str());
        return false;
    }

    // Build branch mesh vertices (same algorithm as TreeSystem::generateTreeMesh)
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    // Get texture scale from options
    glm::vec2 textureScale = options.bark.textureScale;
    float vRepeat = 1.0f / textureScale.y;

    uint32_t indexOffset = 0;
    for (const auto& branch : meshData.branches) {
        int segmentCount = branch.segmentCount;

        for (size_t sectionIdx = 0; sectionIdx < branch.sections.size(); ++sectionIdx) {
            const SectionData& section = branch.sections[sectionIdx];

            float vCoord = (sectionIdx % 2 == 0) ? 0.0f : vRepeat;

            for (int seg = 0; seg <= segmentCount; ++seg) {
                float angle = 2.0f * glm::pi<float>() * static_cast<float>(seg) / static_cast<float>(segmentCount);

                glm::vec3 localPos(std::cos(angle), 0.0f, std::sin(angle));
                glm::vec3 localNormal = -localPos;

                glm::vec3 worldOffset = section.orientation * (localPos * section.radius);
                glm::vec3 worldNormal = glm::normalize(section.orientation * localNormal);

                float uCoord = static_cast<float>(seg) / static_cast<float>(segmentCount) * textureScale.x;

                Vertex v{};
                v.position = section.origin + worldOffset;
                v.normal = worldNormal;
                v.texCoord = glm::vec2(uCoord, vCoord);
                v.tangent = glm::vec4(
                    glm::normalize(section.orientation * glm::vec3(0.0f, 1.0f, 0.0f)),
                    1.0f
                );

                float normalizedLevel = static_cast<float>(branch.level) / 3.0f * 0.95f;
                if (branch.level == 0) {
                    v.color = glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
                } else {
                    v.color = glm::vec4(branch.origin, normalizedLevel);
                }

                vertices.push_back(v);
            }
        }

        // Generate indices for this branch
        uint32_t vertsPerRing = static_cast<uint32_t>(segmentCount + 1);
        int sectionCount = branch.sectionCount;
        for (int section = 0; section < sectionCount; ++section) {
            for (int seg = 0; seg < segmentCount; ++seg) {
                uint32_t v0 = indexOffset + section * vertsPerRing + seg;
                uint32_t v1 = v0 + 1;
                uint32_t v2 = v0 + vertsPerRing;
                uint32_t v3 = v2 + 1;

                indices.push_back(v0);
                indices.push_back(v2);
                indices.push_back(v1);

                indices.push_back(v1);
                indices.push_back(v2);
                indices.push_back(v3);
            }
        }

        indexOffset += static_cast<uint32_t>(branch.sections.size()) * vertsPerRing;
    }

    if (vertices.empty() || indices.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                    "TreeArchetypeMeshes: No geometry generated for %s", name.c_str());
        return false;
    }

    // Create vertex buffer
    VkDeviceSize vertexSize = vertices.size() * sizeof(Vertex);
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = vertexSize;
        bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &archetypes_[archetypeIndex].vertexBuffer,
                           &archetypes_[archetypeIndex].vertexAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "TreeArchetypeMeshes: Failed to create vertex buffer for %s", name.c_str());
            return false;
        }
    }

    // Create index buffer
    VkDeviceSize indexSize = indices.size() * sizeof(uint32_t);
    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = indexSize;
        bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &archetypes_[archetypeIndex].indexBuffer,
                           &archetypes_[archetypeIndex].indexAllocation, nullptr) != VK_SUCCESS) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                        "TreeArchetypeMeshes: Failed to create index buffer for %s", name.c_str());
            destroyArchetype(archetypes_[archetypeIndex]);
            return false;
        }
    }

    // Create staging buffer and upload data
    VkBuffer stagingBuffer;
    VmaAllocation stagingAllocation;
    VkDeviceSize stagingSize = vertexSize + indexSize;

    {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = stagingSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

        VmaAllocationInfo allocationInfo;
        if (vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo,
                           &stagingBuffer, &stagingAllocation, &allocationInfo) != VK_SUCCESS) {
            destroyArchetype(archetypes_[archetypeIndex]);
            return false;
        }

        // Copy data to staging buffer
        uint8_t* data = static_cast<uint8_t*>(allocationInfo.pMappedData);
        memcpy(data, vertices.data(), vertexSize);
        memcpy(data + vertexSize, indices.data(), indexSize);
    }

    // Transfer data to GPU
    VkCommandBuffer cmd;
    {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        vkAllocateCommandBuffers(device_, &allocInfo, &cmd);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.srcOffset = 0;
        copyRegion.dstOffset = 0;
        copyRegion.size = vertexSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, archetypes_[archetypeIndex].vertexBuffer, 1, &copyRegion);

        copyRegion.srcOffset = vertexSize;
        copyRegion.size = indexSize;
        vkCmdCopyBuffer(cmd, stagingBuffer, archetypes_[archetypeIndex].indexBuffer, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue_);

        vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);
    }

    // Clean up staging buffer
    vmaDestroyBuffer(allocator_, stagingBuffer, stagingAllocation);

    // Fill in archetype info
    archetypes_[archetypeIndex].name = name;
    archetypes_[archetypeIndex].indexCount = static_cast<uint32_t>(indices.size());
    archetypes_[archetypeIndex].vertexCount = static_cast<uint32_t>(vertices.size());
    archetypes_[archetypeIndex].valid = true;

    SDL_Log("TreeArchetypeMeshes: Built archetype %s (idx=%u) with %u vertices, %u indices",
            name.c_str(), archetypeIndex,
            archetypes_[archetypeIndex].vertexCount,
            archetypes_[archetypeIndex].indexCount);

    return true;
}

const TreeArchetypeMeshes::ArchetypeMesh& TreeArchetypeMeshes::getArchetype(uint32_t index) const {
    static ArchetypeMesh invalidArchetype{};
    if (index >= MAX_ARCHETYPES) {
        return invalidArchetype;
    }
    return archetypes_[index];
}

void TreeArchetypeMeshes::renderArchetypeIndirect(
    VkCommandBuffer cmd,
    uint32_t archetypeIndex,
    VkBuffer instanceBuffer,
    VkDeviceSize instanceOffset,
    VkBuffer indirectBuffer,
    VkDeviceSize indirectOffset
) {
    if (archetypeIndex >= MAX_ARCHETYPES || !archetypes_[archetypeIndex].valid) {
        return;
    }

    const auto& arch = archetypes_[archetypeIndex];

    // Bind vertex buffer (archetype mesh) and instance buffer
    VkBuffer vertexBuffers[] = {arch.vertexBuffer, instanceBuffer};
    VkDeviceSize offsets[] = {0, instanceOffset};
    vkCmdBindVertexBuffers(cmd, 0, 2, vertexBuffers, offsets);

    // Bind index buffer
    vkCmdBindIndexBuffer(cmd, arch.indexBuffer, 0, VK_INDEX_TYPE_UINT32);

    // Draw using indirect command (GPU determines instance count)
    vkCmdDrawIndexedIndirect(cmd, indirectBuffer, indirectOffset, 1, sizeof(VkDrawIndexedIndirectCommand));
}
