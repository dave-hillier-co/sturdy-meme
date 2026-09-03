#include "GrassBuffers.h"
#include "DisplacementSystem.h"
#include "CullCommon.h"
#include <SDL3/SDL.h>
#include <cstring>

bool GrassBuffers::create(VmaAllocator allocator, uint32_t framesInFlight) {
    bufferSets_ = BufferSetManager(framesInFlight);

    vk::DeviceSize instanceBufferSize = sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES;
    vk::DeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    vk::DeviceSize cullingUniformSize = sizeof(CullingUniforms);
    vk::DeviceSize grassParamsSize = sizeof(GrassParams);

    uint32_t bufferSetCount = framesInFlight;
    const auto perFrameConfig = BufferUtils::PerFrameBufferConfig(allocator, framesInFlight);

    // GPU-only sets, one per buffer set (matches the former DoubleBufferedBufferSet: AUTO memory usage)
    if (!instanceBuffers_.resize(allocator, bufferSetCount, instanceBufferSize,
                                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eVertexBuffer,
                                 VMA_MEMORY_USAGE_AUTO)) {
        SDL_Log("Failed to create grass instance buffers");
        return false;
    }

    if (!indirectBuffers_.resize(allocator, bufferSetCount, indirectBufferSize,
                                 vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer |
                                 vk::BufferUsageFlagBits::eTransferDst,
                                 VMA_MEMORY_USAGE_AUTO)) {
        SDL_Log("Failed to create grass indirect buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator,
             BufferUtils::PerFrameBufferBuilder::fromConfig(perFrameConfig).withSize(cullingUniformSize),
             uniformBuffers_)) {
        SDL_Log("Failed to create grass culling uniform buffers");
        return false;
    }

    if (!BufferUtils::MappedFrameBuffers::build(allocator,
             BufferUtils::PerFrameBufferBuilder::fromConfig(perFrameConfig).withSize(grassParamsSize),
             paramsBuffers_)) {
        SDL_Log("Failed to create grass params buffers");
        return false;
    }

    return true;
}

void GrassBuffers::updateUniforms(uint32_t frameIndex, const glm::vec3& cameraPos, const glm::mat4& viewProj,
                                   float terrainSize, float terrainHeightScale, float time,
                                   DisplacementSystem* displacementSystem) {
    // Fill CullingUniforms (shared culling parameters) using unified constants
    CullingUniforms culling{};
    culling.cameraPosition = glm::vec4(cameraPos, 1.0f);
    extractFrustumPlanes(viewProj, culling.frustumPlanes);
    culling.maxDrawDistance = GrassConstants::MAX_DRAW_DISTANCE;
    // Legacy fields - not used with continuous stochastic culling
    culling.lodTransitionStart = -1.0f;
    culling.lodTransitionEnd = -1.0f;
    culling.maxLodDropRate = 0.0f;
    memcpy(uniformBuffers_.mapped(frameIndex), &culling, sizeof(CullingUniforms));

    // Fill GrassParams (grass-specific parameters)
    GrassParams params{};

    // Displacement region info for grass compute shader
    // xy = world center, z = region size, w = texel size
    if (displacementSystem) {
        params.displacementRegion = displacementSystem->getRegionVec4();
    } else {
        // Fallback: center on camera with default constants
        params.displacementRegion = glm::vec4(cameraPos.x, cameraPos.z,
                                              GrassConstants::DISPLACEMENT_REGION_SIZE,
                                              GrassConstants::DISPLACEMENT_TEXEL_SIZE);
    }

    // Terrain parameters for heightmap sampling
    params.terrainSize = terrainSize;
    params.terrainHeightScale = terrainHeightScale;
    memcpy(paramsBuffers_.mapped(frameIndex), &params, sizeof(GrassParams));
}
