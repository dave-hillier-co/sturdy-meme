// AtmosphereSystemGroup.cpp - Self-initialization for atmosphere systems

#include "AtmosphereSystemGroup.h"
#include "SkySystem.h"
#include "AtmosphereLUTSystem.h"
#include "CloudShadowSystem.h"
#include "FroxelSystem.h"
#include "PostProcessSystem.h"
#include "VulkanServices.h"
#include "core/vulkan/CommandBufferUtils.h"
#include <glm/glm.hpp>
#include <SDL3/SDL.h>

std::optional<AtmosphereSystemGroup::Bundle> AtmosphereSystemGroup::createAll(
    const CreateDeps& deps
) {
    Bundle bundle;

    // 1. Create AtmosphereLUTSystem first (no dependencies)
    bundle.atmosphereLUT = AtmosphereLUTSystem::create(deps.ctx);
    if (!bundle.atmosphereLUT) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create AtmosphereLUTSystem");
        return std::nullopt;
    }

    // Compute atmosphere LUTs at startup
    {
        CommandScope cmdScope(deps.ctx.device, deps.ctx.commandPool, deps.ctx.graphicsQueue);
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to begin command buffer for LUT computation");
            return std::nullopt;
        }

        // Compute transmittance and multi-scatter LUTs (once at startup)
        bundle.atmosphereLUT->computeTransmittanceLUT(cmdScope.get());
        bundle.atmosphereLUT->computeMultiScatterLUT(cmdScope.get());
        bundle.atmosphereLUT->computeIrradianceLUT(cmdScope.get());

        // Compute sky-view LUT for current sun direction
        glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);  // Default 45 degree sun
        bundle.atmosphereLUT->computeSkyViewLUT(cmdScope.get(), sunDir, glm::vec3(0.0f), 0.0f);

        // Compute cloud map LUT (paraboloid projection)
        bundle.atmosphereLUT->computeCloudMapLUT(cmdScope.get(), glm::vec3(0.0f), 0.0f);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to end command buffer for LUT computation");
            return std::nullopt;
        }
    }
    SDL_Log("AtmosphereSystemGroup: Atmosphere LUTs computed");

    // Export LUTs as PNG files for visualization
    bundle.atmosphereLUT->exportLUTsAsPNG(deps.ctx.resourcePath);
    SDL_Log("AtmosphereSystemGroup: LUTs exported to %s", deps.ctx.resourcePath.c_str());

    // 2. Create FroxelSystem (needs shadow resources)
    bundle.froxel = FroxelSystem::create(
        deps.ctx,
        deps.shadowMapView,
        deps.shadowSampler,
        deps.lightBuffers);
    if (!bundle.froxel) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create FroxelSystem");
        return std::nullopt;
    }

    // 3. Create CloudShadowSystem (needs AtmosphereLUT cloud map)
    bundle.cloudShadow = CloudShadowSystem::create(
        deps.ctx,
        bundle.atmosphereLUT->getCloudMapLUTView(),
        bundle.atmosphereLUT->getLUTSampler());
    if (!bundle.cloudShadow) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create CloudShadowSystem");
        return std::nullopt;
    }

    // 4. Create SkySystem (needs HDR render pass)
    bundle.sky = SkySystem::create(deps.ctx, deps.hdrRenderPass);
    if (!bundle.sky) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create SkySystem");
        return std::nullopt;
    }

    SDL_Log("AtmosphereSystemGroup: All systems created successfully");
    return bundle;
}

std::optional<AtmosphereSystemGroup::Bundle> AtmosphereSystemGroup::createAll(
    const CreateDepsDI& deps
) {
    Bundle bundle;
    const auto& s = deps.services;

    // 1. Create AtmosphereLUTSystem first (no dependencies)
    AtmosphereLUTSystem::InitInfo lutInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice()
    };
    bundle.atmosphereLUT = AtmosphereLUTSystem::create(lutInfo);
    if (!bundle.atmosphereLUT) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create AtmosphereLUTSystem");
        return std::nullopt;
    }

    // Compute atmosphere LUTs at startup
    {
        CommandScope cmdScope(s.device(), s.commandPool(), s.graphicsQueue());
        if (!cmdScope.begin()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to begin command buffer for LUT computation");
            return std::nullopt;
        }

        bundle.atmosphereLUT->computeTransmittanceLUT(cmdScope.get());
        bundle.atmosphereLUT->computeMultiScatterLUT(cmdScope.get());
        bundle.atmosphereLUT->computeIrradianceLUT(cmdScope.get());

        glm::vec3 sunDir = glm::vec3(0.0f, 0.707f, 0.707f);
        bundle.atmosphereLUT->computeSkyViewLUT(cmdScope.get(), sunDir, glm::vec3(0.0f), 0.0f);
        bundle.atmosphereLUT->computeCloudMapLUT(cmdScope.get(), glm::vec3(0.0f), 0.0f);

        if (!cmdScope.end()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to end command buffer for LUT computation");
            return std::nullopt;
        }
    }
    SDL_Log("AtmosphereSystemGroup: Atmosphere LUTs computed");

    bundle.atmosphereLUT->exportLUTsAsPNG(s.resourcePath());
    SDL_Log("AtmosphereSystemGroup: LUTs exported to %s", s.resourcePath().c_str());

    // 2. Create FroxelSystem
    FroxelSystem::InitInfo froxelInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .extent = s.extent(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice()
    };
    bundle.froxel = FroxelSystem::create(froxelInfo, deps.shadowMapView, deps.shadowSampler, deps.lightBuffers);
    if (!bundle.froxel) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create FroxelSystem");
        return std::nullopt;
    }

    // 3. Create CloudShadowSystem
    CloudShadowSystem::InitInfo cloudInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .raiiDevice = s.raiiDevice(),
        .cloudMapLUTView = bundle.atmosphereLUT->getCloudMapLUTView(),
        .cloudMapSampler = bundle.atmosphereLUT->getLUTSampler()
    };
    bundle.cloudShadow = CloudShadowSystem::create(cloudInfo);
    if (!bundle.cloudShadow) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create CloudShadowSystem");
        return std::nullopt;
    }

    // 4. Create SkySystem
    SkySystem::InitInfo skyInfo{
        .device = s.device(),
        .allocator = s.allocator(),
        .descriptorPool = s.descriptorPool(),
        .shaderPath = s.shaderPath(),
        .framesInFlight = s.framesInFlight(),
        .extent = s.extent(),
        .hdrRenderPass = deps.hdrRenderPass,
        .raiiDevice = s.raiiDevice()
    };
    bundle.sky = SkySystem::create(skyInfo);
    if (!bundle.sky) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "AtmosphereSystemGroup: Failed to create SkySystem");
        return std::nullopt;
    }

    SDL_Log("AtmosphereSystemGroup: All systems created successfully (DI)");
    return bundle;
}

void AtmosphereSystemGroup::wireToPostProcess(
    FroxelSystem& froxel,
    PostProcessSystem& postProcess
) {
    // Connect froxel volume to post-process system for compositing
    postProcess.setFroxelVolume(froxel.getIntegratedVolumeView(), froxel.getVolumeSampler());
    postProcess.setFroxelParams(froxel.getVolumetricFarPlane(), FroxelSystem::DEPTH_DISTRIBUTION);
    postProcess.setFroxelEnabled(true);
    SDL_Log("AtmosphereSystemGroup: Wired froxel to post-process");
}
