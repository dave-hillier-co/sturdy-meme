#pragma once

// ============================================================================
// HDRDrawableFactory.h - Registers all drawable adapters with the HDR pass
// ============================================================================
//
// Centralizes the knowledge of which concrete systems participate in HDR
// rendering, keeping Renderer.cpp free from those concrete includes.

#include <functional>
#include <cstdint>
#include <vulkan/vulkan.h>

class HDRPassRecorder;
class RendererSystems;

namespace HDRDrawableFactory {

/// Register all drawable adapters with the HDR pass recorder.
/// The optional ragdollCallback is forwarded to SkinnedCharDrawable for ragdoll rendering.
void registerAll(HDRPassRecorder& recorder, RendererSystems& systems,
                 const std::function<void(VkCommandBuffer, uint32_t)>& ragdollCallback = {});

} // namespace HDRDrawableFactory
