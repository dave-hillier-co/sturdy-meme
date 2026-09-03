#pragma once

#include <vulkan/vulkan.hpp>
#include <cstdint>

// Forward declaration
class ShadowSystem;

/**
 * ShadowResources - Resources provided by ShadowSystem
 *
 * Captures shadow maps, render pass, and samplers needed by
 * systems that sample shadows or render to shadow maps.
 */
struct ShadowResources {
    vk::RenderPass renderPass{};
    vk::ImageView cascadeView{};
    vk::Sampler sampler{};
    uint32_t mapSize = 0;

    // Per-frame shadow array views (point lights, spot lights)
    // Index by frame index
    vk::ImageView pointShadowViews[2] = {};
    vk::Sampler pointShadowSampler{};
    vk::ImageView spotShadowViews[2] = {};
    vk::Sampler spotShadowSampler{};

    bool isValid() const { return renderPass && cascadeView; }

    // Collect from ShadowSystem
    static ShadowResources collect(const ShadowSystem& shadow, uint32_t framesInFlight);
};
