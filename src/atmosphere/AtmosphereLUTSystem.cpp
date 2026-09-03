#include "AtmosphereLUTSystem.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<AtmosphereLUTSystem> AtmosphereLUTSystem::create(const InitInfo& info) {
    auto system = std::make_unique<AtmosphereLUTSystem>(ConstructToken{});
    if (!system->initInternal(info)) {
        return nullptr;
    }
    return system;
}

std::unique_ptr<AtmosphereLUTSystem> AtmosphereLUTSystem::create(const InitContext& ctx) {
    InitInfo info{};
    info.device = ctx.device;
    info.allocator = ctx.allocator;
    info.descriptorPool = ctx.descriptorPool;
    info.shaderPath = ctx.shaderPath;
    info.framesInFlight = ctx.framesInFlight;
    info.raiiDevice = ctx.raiiDevice;
    return create(info);
}

bool AtmosphereLUTSystem::initInternal(const InitInfo& info) {
    device = info.device;
    allocator = info.allocator;
    descriptorPool = info.descriptorPool;
    shaderPath = info.shaderPath;
    framesInFlight = info.framesInFlight;
    raiiDevice_ = info.raiiDevice;

    if (!createTransmittanceLUT()) return false;
    if (!createMultiScatterLUT()) return false;
    if (!createSkyViewLUT()) return false;
    if (!createIrradianceLUTs()) return false;
    if (!createCloudMapLUT()) return false;
    if (!createLUTSampler()) return false;
    if (!createUniformBuffer()) return false;
    if (!createDescriptorSetLayouts()) return false;
    if (!createDescriptorSets()) return false;
    if (!createComputePipelines()) return false;

    SDL_Log("Atmosphere LUT System initialized");
    return true;
}
