#pragma once

// ============================================================================
// HDRPassRecorder.h - HDR render pass recording with registered drawables
// ============================================================================
//
// Manages HDR pass recording by iterating over registered IHDRDrawable items.
// Instead of depending on every concrete rendering system, HDRPassRecorder
// depends only on the IHDRDrawable interface. Systems register themselves
// (or are registered via adapters) and the recorder iterates them in order.
//
// This inverts the dependency: new systems can be added without modifying
// this class.
//
// Design: Stateless recording - all per-frame configuration passed via Params.
//

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

#include "interfaces/IHDRDrawable.h"

class Profiler;
class PostProcessSystem;
class GPUSceneBuffer;

/**
 * HDRPassRecorder - HDR pass command recording via registered drawables
 *
 * Drawables are registered with a draw order, secondary command buffer slot,
 * and GPU profiler zone name. The recorder iterates them in order, calling
 * shouldDraw() and recordHDRDraw() for each.
 */
class HDRPassRecorder {
public:
    // Params is an alias for HDRDrawParams (defined in IHDRDrawable.h)
    using Params = HDRDrawParams;

    /**
     * Construct with core infrastructure needed for render pass management.
     *
     * @param profiler GPU profiler for timing zones
     * @param postProcess Post-process system providing render pass and framebuffer
     */
    HDRPassRecorder(Profiler& profiler, PostProcessSystem& postProcess);

    /**
     * Register a drawable to participate in HDR pass recording.
     *
     * Drawables are called in drawOrder (ascending). The slot determines
     * which secondary command buffer group the drawable belongs to for
     * parallel recording.
     *
     * @param drawable Owned pointer to the drawable (lifetime managed by recorder)
     * @param drawOrder Rendering order (lower = drawn first)
     * @param slot Secondary command buffer slot (0, 1, or 2)
     * @param profileZone GPU profiler zone name (e.g., "HDR:Sky")
     */
    void registerDrawable(std::unique_ptr<IHDRDrawable> drawable, int drawOrder,
                          int slot, const char* profileZone);

    // Record the complete HDR pass (stateless - all config via params)
    void record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const Params& params);

    // Record HDR pass with pre-recorded secondary command buffers
    void recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                               const std::vector<vk::CommandBuffer>& secondaries, const Params& params);

    // Record a specific slot to a secondary command buffer
    // Slot 0: Sky + Terrain + Catmull-Clark
    // Slot 1: Scene Objects + Skinned Character
    // Slot 2: Grass + Water + Leaves + Weather + Debug lines
    void recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                             uint32_t slot, const Params& params);

private:
    struct RegisteredDrawable {
        std::unique_ptr<IHDRDrawable> drawable;
        int drawOrder;
        int slot;
        const char* profileZone;
    };

    void beginHDRRenderPass(vk::CommandBuffer vkCmd, vk::SubpassContents contents);

    Profiler& profiler_;
    PostProcessSystem& postProcess_;
    std::vector<RegisteredDrawable> drawables_;
};
