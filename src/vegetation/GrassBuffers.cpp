#include "GrassBuffers.h"
#include "DisplacementSystem.h"
#include "CullCommon.h"
#include <SDL3/SDL.h>
#include <cstring>

bool GrassBuffers::create(VmaAllocator allocator, uint32_t framesInFlight) {
    bufferSets_ = BufferSetManager(framesInFlight);

    VkDeviceSize instanceBufferSize = sizeof(GrassInstance) * GrassConstants::MAX_INSTANCES;
    VkDeviceSize indirectBufferSize = sizeof(VkDrawIndirectCommand);
    VkDeviceSize cullingUniformSize = sizeof(CullingUniforms);
    VkDeviceSize grassParamsSize = sizeof(GrassParams);

    uint32_t bufferSetCount = framesInFlight;
    const auto doubleBufferedConfig = BufferUtils::DoubleBufferedBufferConfig(allocator, bufferSetCount);
    const auto perFrameConfig = BufferUtils::PerFrameBufferConfig(allocator, framesInFlight);

    if (!BufferUtils::DoubleBufferedBufferBuilder::fromConfig(doubleBufferedConfig)
             .withSize(instanceBufferSize)
             .withUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT)
             .build(instanceBuffers_)) {
        SDL_Log("Failed to create grass instance buffers");
        return false;
    }

    if (!BufferUtils::DoubleBufferedBufferBuilder::fromConfig(doubleBufferedConfig)
             .withSize(indirectBufferSize)
             .withUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT)
             .build(indirectBuffers_)) {
        SDL_Log("Failed to create grass indirect buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder::fromConfig(perFrameConfig)
             .withSize(cullingUniformSize)
             .build(uniformBuffers_)) {
        SDL_Log("Failed to create grass culling uniform buffers");
        return false;
    }

    if (!BufferUtils::PerFrameBufferBuilder::fromConfig(perFrameConfig)
             .withSize(grassParamsSize)
             .build(paramsBuffers_)) {
        SDL_Log("Failed to create grass params buffers");
        return false;
    }

    return true;
}

void GrassBuffers::destroy(VmaAllocator allocator) {
    BufferUtils::destroyBuffers(allocator, instanceBuffers_);
    BufferUtils::destroyBuffers(allocator, indirectBuffers_);
    BufferUtils::destroyBuffers(allocator, uniformBuffers_);
    BufferUtils::destroyBuffers(allocator, paramsBuffers_);
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
    memcpy(uniformBuffers_.mappedPointers[frameIndex], &culling, sizeof(CullingUniforms));

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
    memcpy(paramsBuffers_.mappedPointers[frameIndex], &params, sizeof(GrassParams));
}
