#pragma once

#include "PassScheduler.h"
#include <vulkan/vulkan_raii.hpp>

class RendererSystems;
struct PerformanceToggles;

/**
 * PostPasses - Post-processing pass definitions
 *
 * Includes: HiZ, Bloom, BilateralGrid, PostProcess (final composite)
 */
namespace PostPasses {

struct Config {
    std::function<void(VkCommandBuffer)>* guiRenderCallback = nullptr;
    std::vector<vk::raii::Framebuffer>* framebuffers = nullptr;
    PerformanceToggles* perfToggles = nullptr;
};

struct PassIds {
    PassScheduler::PassId hiZ = PassScheduler::INVALID_PASS;
    PassScheduler::PassId bloom = PassScheduler::INVALID_PASS;
    PassScheduler::PassId godRays = PassScheduler::INVALID_PASS;
    PassScheduler::PassId bilateralGrid = PassScheduler::INVALID_PASS;
    PassScheduler::PassId postProcess = PassScheduler::INVALID_PASS;
};

PassIds addPasses(PassScheduler& graph, RendererSystems& systems, const Config& config);

} // namespace PostPasses
