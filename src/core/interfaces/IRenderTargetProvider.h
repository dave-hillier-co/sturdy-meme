#pragma once

// ============================================================================
// IRenderTargetProvider.h - Interface for systems that provide render targets
// ============================================================================
//
// This interface decouples systems that need access to render targets (HDR,
// depth buffers) from the concrete PostProcessSystem implementation.
//
// Benefits:
// - Systems can bind depth/color targets without knowing PostProcessSystem
// - Enables alternative render target sources
// - Makes render target contract explicit
// - Reduces header dependencies
//

#include <vulkan/vulkan.hpp>

/**
 * Interface for systems that provide HDR render targets.
 *
 * Implement this for systems that manage HDR framebuffers (e.g., PostProcessSystem).
 * Consumers use these for reading scene color/depth in post-processing or
 * depth-dependent effects.
 */
class IRenderTargetProvider {
public:
    virtual ~IRenderTargetProvider() = default;

    /**
     * Get the HDR color attachment view.
     * This is the main scene color buffer before tone mapping.
     */
    virtual vk::ImageView getHDRColorView() const = 0;

    /**
     * Get the HDR depth attachment view.
     * This is the scene depth buffer.
     */
    virtual vk::ImageView getHDRDepthView() const = 0;

    /**
     * Get the render pass for HDR rendering.
     */
    virtual vk::RenderPass getHDRRenderPass() const = 0;

    /**
     * Get the HDR framebuffer.
     */
    virtual vk::Framebuffer getHDRFramebuffer() const = 0;

    /**
     * Get the extent of the render targets.
     */
    virtual vk::Extent2D getRenderExtent() const = 0;
};

/**
 * Extended interface for systems that provide multiple render targets.
 *
 * Use this when you need access to additional intermediate buffers
 * (e.g., bloom, motion vectors, etc.)
 */
class IMultiTargetProvider : public IRenderTargetProvider {
public:
    /**
     * Get the bloom intermediate texture view.
     */
    virtual vk::ImageView getBloomView() const = 0;

    /**
     * Get the motion vectors texture view.
     */
    virtual vk::ImageView getMotionVectorsView() const = 0;
};
