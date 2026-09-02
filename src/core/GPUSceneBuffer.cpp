#include "GPUSceneBuffer.h"
#include "Mesh.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <algorithm>
#include <numeric>
#include <functional>

bool GPUSceneBuffer::init(VmaAllocator allocator, uint32_t frameCount) {
    allocator_ = allocator;
    frameCount_ = frameCount;

    // Pre-allocate CPU staging
    instances_.reserve(MAX_GPU_SCENE_OBJECTS);
    cullObjects_.reserve(MAX_GPU_SCENE_OBJECTS);
    batches_.reserve(256);

    // Create per-frame instance buffers (SSBO)
    vk::DeviceSize instanceBufferSize = sizeof(GPUSceneInstanceData) * MAX_GPU_SCENE_OBJECTS;
    bool success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(instanceBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(instanceBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create instance buffers");
        cleanup();
        return false;
    }

    // Create cull object buffer (single, updated when scene changes)
    vk::DeviceSize cullBufferSize = sizeof(GPUCullObjectData) * MAX_GPU_SCENE_OBJECTS;
    if (!VmaBufferFactory::createStorageBufferHostWritable(allocator, cullBufferSize, cullObjectBuffer_)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create cull object buffer");
        cleanup();
        return false;
    }

    // Create per-frame indirect draw buffers
    vk::DeviceSize indirectBufferSize = sizeof(GPUDrawIndexedIndirectCommand) * MAX_GPU_SCENE_OBJECTS;
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(indirectBufferSize)
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(0)  // GPU-only
        .build(indirectBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create indirect buffers");
        cleanup();
        return false;
    }

    // Create per-frame draw count buffers
    success = BufferUtils::PerFrameBufferBuilder()
        .setAllocator(allocator)
        .setFrameCount(frameCount)
        .setSize(sizeof(uint32_t))
        .setUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                  VK_BUFFER_USAGE_TRANSFER_DST_BIT)
        .setAllocationFlags(VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
                           VMA_ALLOCATION_CREATE_MAPPED_BIT)
        .build(drawCountBuffers_);

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Failed to create draw count buffers");
        cleanup();
        return false;
    }

    SDL_Log("GPUSceneBuffer: Initialized with %u frames, max %zu objects",
            frameCount, MAX_GPU_SCENE_OBJECTS);
    return true;
}

void GPUSceneBuffer::cleanup() {
    auto release = [this](auto& set) {
        BufferUtils::destroyBuffers(allocator_, set);
        set.buffers.clear();
        set.allocations.clear();
    };
    release(drawCountBuffers_);
    release(indirectBuffers_);
    cullObjectBuffer_.reset();
    release(instanceBuffers_);

    instances_.clear();
    cullObjects_.clear();
    batches_.clear();
}

void GPUSceneBuffer::beginFrame(uint32_t frameIndex) {
    currentFrame_ = frameIndex;
    instances_.clear();
    cullObjects_.clear();
    drawInfo_.clear();
    batches_.clear();
    cullDataDirty_ = true;
}

int32_t GPUSceneBuffer::addObject(const ecs::RenderData& data, vk::DescriptorSet overrideSet) {
    if (instances_.size() >= MAX_GPU_SCENE_OBJECTS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
            "GPUSceneBuffer: Max objects reached (%zu)", MAX_GPU_SCENE_OBJECTS);
        return -1;
    }

    if (!data.mesh) {
        return -1;
    }

    uint32_t objectIndex = static_cast<uint32_t>(instances_.size());

    // Build instance data
    GPUSceneInstanceData instance{};
    instance.model = data.transform;
    instance.materialParams = glm::vec4(
        data.roughness,
        data.metallic,
        data.emissiveIntensity,
        data.opacity
    );
    instance.emissiveColor = glm::vec4(data.emissiveColor, 1.0f);
    instance.pbrFlags = data.pbrFlags;
    instance.alphaTestThreshold = data.alphaTestThreshold;
    instance.hueShift = data.hueShift;
    instance._pad1 = 0.0f;

    instances_.push_back(instance);

    // Build cull data
    const AABB& localBounds = data.mesh->getBounds();
    AABB worldBounds = localBounds.transformed(data.transform);

    GPUCullObjectData cullData{};
    glm::vec3 center = worldBounds.getCenter();
    glm::vec3 extents = worldBounds.getExtents();
    float radius = glm::length(extents);

    cullData.boundingSphere = glm::vec4(center, radius);
    // aabbMin.w carries castsShadow (1 = caster, 0 = not). The color cull pass ignores it;
    // the shadow cull pass (cullMode==1) rejects non-casters so the shared cull-object
    // buffer can drive both passes.
    cullData.aabbMin = glm::vec4(worldBounds.min, data.castsShadow ? 1.0f : 0.0f);
    cullData.aabbMax = glm::vec4(worldBounds.max, 0.0f);
    cullData.objectIndex = objectIndex;
    cullData.firstIndex = 0;
    cullData.indexCount = data.mesh->getIndexCount();
    cullData.vertexOffset = 0;

    cullObjects_.push_back(cullData);

    // Track mesh+material+descriptor override for batching in finalize().
    drawInfo_.push_back({data.mesh, data.materialId, overrideSet});

    return static_cast<int32_t>(objectIndex);
}

void GPUSceneBuffer::finalize() {
    if (instances_.empty()) {
        return;
    }

    // Group objects into draw batches by (mesh, material). Sort instances_,
    // cullObjects_ and drawInfo_ together so instance slot order == command slot
    // order (the firstInstance->gl_InstanceIndex contract depends on this), then
    // reassign each cull slot's objectIndex to its new position and emit contiguous
    // (mesh, material) batches.
    const size_t count = instances_.size();
    std::vector<uint32_t> order(count);
    std::iota(order.begin(), order.end(), 0u);
    std::stable_sort(order.begin(), order.end(), [this](uint32_t a, uint32_t b) {
        if (drawInfo_[a].mesh != drawInfo_[b].mesh) {
            return std::less<const Mesh*>{}(drawInfo_[a].mesh, drawInfo_[b].mesh);
        }
        if (drawInfo_[a].materialId != drawInfo_[b].materialId) {
            return drawInfo_[a].materialId < drawInfo_[b].materialId;
        }
        return std::less<vk::DescriptorSet>{}(drawInfo_[a].overrideSet, drawInfo_[b].overrideSet);
    });

    std::vector<GPUSceneInstanceData> sortedInstances;
    std::vector<GPUCullObjectData> sortedCull;
    std::vector<ObjectDrawInfo> sortedInfo;
    sortedInstances.reserve(count);
    sortedCull.reserve(count);
    sortedInfo.reserve(count);
    for (uint32_t newIdx = 0; newIdx < count; ++newIdx) {
        const uint32_t oldIdx = order[newIdx];
        sortedInstances.push_back(instances_[oldIdx]);
        GPUCullObjectData cull = cullObjects_[oldIdx];
        cull.objectIndex = newIdx;  // stable slot = sorted position
        sortedCull.push_back(cull);
        sortedInfo.push_back(drawInfo_[oldIdx]);
    }
    instances_ = std::move(sortedInstances);
    cullObjects_ = std::move(sortedCull);
    drawInfo_ = std::move(sortedInfo);

    batches_.clear();
    for (uint32_t i = 0; i < count;) {
        const Mesh* mesh = drawInfo_[i].mesh;
        const MaterialId materialId = drawInfo_[i].materialId;
        const vk::DescriptorSet overrideSet = drawInfo_[i].overrideSet;
        const uint32_t first = i;
        while (i < count && drawInfo_[i].mesh == mesh && drawInfo_[i].materialId == materialId
               && drawInfo_[i].overrideSet == overrideSet) {
            ++i;
        }
        batches_.push_back(GPUMeshBatch{mesh, materialId, first, i - first, overrideSet});
    }

    // Upload instance data to current frame's buffer
    void* mapped = instanceBuffers_.mappedPointers[currentFrame_];
    if (mapped) {
        vk::DeviceSize bytes = instances_.size() * sizeof(GPUSceneInstanceData);
        memcpy(mapped, instances_.data(), bytes);
        vmaFlushAllocation(allocator_, instanceBuffers_.allocations[currentFrame_], 0, bytes);
    }

    // Upload cull data (only if changed)
    if (cullDataDirty_) {
        void* cullMapped = cullObjectBuffer_.map();
        if (cullMapped) {
            vk::DeviceSize bytes = cullObjects_.size() * sizeof(GPUCullObjectData);
            memcpy(cullMapped, cullObjects_.data(), bytes);
            vmaFlushAllocation(allocator_, cullObjectBuffer_.getAllocation(), 0, bytes);
            cullObjectBuffer_.unmap();
        }
        cullDataDirty_ = false;
    }
}

void GPUSceneBuffer::resetDrawCount(vk::CommandBuffer cmd) {
    // Fill draw count buffer with zero
    cmd.fillBuffer(drawCountBuffers_.buffers[currentFrame_], 0, sizeof(uint32_t), 0);
}

uint32_t GPUSceneBuffer::getVisibleCount(uint32_t frameIndex) const {
    if (drawCountBuffers_.mappedPointers.empty()) {
        return 0;
    }
    return *static_cast<uint32_t*>(drawCountBuffers_.mappedPointers[frameIndex]);
}
