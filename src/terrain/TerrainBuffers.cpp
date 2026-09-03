#include "TerrainBuffers.h"
#include "UBOs.h"
#include "core/vulkan/VmaBufferFactory.h"
#include <SDL3/SDL_log.h>
#include <cstring>
#include <utility>

std::unique_ptr<TerrainBuffers> TerrainBuffers::create(const InitInfo& info) {
    auto buffers = std::make_unique<TerrainBuffers>(ConstructToken{});
    if (!buffers->initInternal(info)) {
        return nullptr;
    }
    return buffers;
}

bool TerrainBuffers::initInternal(const InitInfo& info) {
    if (!createUniformBuffers(info)) return false;
    if (!createIndirectBuffers(info)) return false;
    return true;
}

bool TerrainBuffers::createPerFrameUniforms(VmaAllocator allocator, uint32_t frameCount,
                                            vk::DeviceSize size, PerFrameUniforms& out) {
    out.buffers.clear();
    out.mapped.clear();
    out.buffers.resize(frameCount);
    out.mapped.resize(frameCount, nullptr);
    for (uint32_t i = 0; i < frameCount; ++i) {
        // Host-visible, sequential-write, persistently mapped uniform buffer
        if (!VmaBufferFactory::createUniformBuffer(allocator, size, out.buffers[i])) {
            return false;
        }
        out.mapped[i] = out.buffers[i].map();
        if (!out.mapped[i]) {
            return false;
        }
    }
    return true;
}

bool TerrainBuffers::createUniformBuffers(const InitInfo& info) {
    // Main terrain uniforms
    if (!createPerFrameUniforms(info.allocator, info.framesInFlight, sizeof(TerrainUniforms), uniformBuffers)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create terrain uniform buffers");
        return false;
    }

    // Caustics uniforms (8 floats = 32 bytes, std140 aligned)
    // Matches CausticsUniforms in terrain.frag
    constexpr vk::DeviceSize causticsUBOSize = 32;  // 8 * sizeof(float)
    if (!createPerFrameUniforms(info.allocator, info.framesInFlight, causticsUBOSize, causticsUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create caustics uniform buffers");
        return false;
    }

    // Liquid uniforms (composable material system - puddles, wetness)
    // Matches TerrainLiquidUniforms in terrain.frag and TerrainLiquidUBO in C++
    constexpr vk::DeviceSize liquidUBOSize = 128;  // Aligned size of TerrainLiquidUBO
    if (!createPerFrameUniforms(info.allocator, info.framesInFlight, liquidUBOSize, liquidUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create liquid uniform buffers");
        return false;
    }

    // Material layer uniforms (composable material system - layer blending)
    // Matches MaterialLayerUBO in MaterialLayer.h: 4 layers * 5 vec4s + int + padding
    // LayerData = 5 * vec4 = 80 bytes, 4 layers = 320 bytes + 16 bytes header = 336 bytes
    constexpr vk::DeviceSize materialLayerUBOSize = 336;
    if (!createPerFrameUniforms(info.allocator, info.framesInFlight, materialLayerUBOSize, materialLayerUniforms)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create material layer uniform buffers");
        return false;
    }

    return true;
}

bool TerrainBuffers::createIndirectBuffers(const InitInfo& info) {
    // Indirect dispatch buffer for compute shaders
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(vk::DispatchIndirectCommand))
            .asStorage()
            .asIndirect()
            .build(indirectDispatch)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect dispatch buffer");
        return false;
    }

    // Indirect draw buffer for indexed draw commands (host-readable so the CPU can
    // read back the triangle count; persistently mapped for the object's lifetime)
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(vk::DrawIndexedIndirectCommand))
            .asStorage()
            .asIndirect()
            .hostReadable()
            .build(indirectDraw)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create indirect draw buffer");
        return false;
    }
    indirectDrawMapped = indirectDraw.map();
    if (!indirectDrawMapped) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to map indirect draw buffer");
        return false;
    }

    // Initialize with default values (2 triangles = 6 vertices/indices)
    uint32_t drawArgs[5] = {6, 1, 0, 0, 0};
    memcpy(indirectDrawMapped, drawArgs, sizeof(drawArgs));

    // Visible indices buffer for stream compaction: [count, index0, index1, ...]
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(uint32_t) * (1 + info.maxVisibleTriangles))
            .asStorage()
            .asTransferDst()
            .build(visibleIndices)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create visible indices buffer");
        return false;
    }

    // Cull indirect dispatch buffer for compute shaders
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(vk::DispatchIndirectCommand))
            .asStorage()
            .asIndirect()
            .build(cullIndirectDispatch)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create cull indirect dispatch buffer");
        return false;
    }

    // Shadow visible indices buffer
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(uint32_t) * (1 + info.maxVisibleTriangles))
            .asStorage()
            .asTransferDst()
            .build(shadowVisible)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow visible indices buffer");
        return false;
    }

    // Shadow indirect draw buffer for indexed draw commands
    if (!BufferBuilder(info.allocator)
            .setSize(sizeof(vk::DrawIndexedIndirectCommand))
            .asStorage()
            .asIndirect()
            .build(shadowIndirectDraw)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shadow indirect draw buffer");
        return false;
    }

    return true;
}
