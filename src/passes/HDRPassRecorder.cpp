#include "HDRPassRecorder.h"

#include "PostProcessSystem.h"
#include "Profiler.h"

#include <SDL3/SDL.h>
#include <algorithm>

HDRPassRecorder::HDRPassRecorder(Profiler& profiler, PostProcessSystem& postProcess)
    : profiler_(profiler), postProcess_(postProcess)
{
}

void HDRPassRecorder::registerDrawable(std::unique_ptr<IHDRDrawable> drawable, int drawOrder,
                                        int slot, const char* profileZone) {
    drawables_.push_back({std::move(drawable), drawOrder, slot, profileZone});

    // Keep sorted by draw order for correct rendering sequence
    std::sort(drawables_.begin(), drawables_.end(),
              [](const RegisteredDrawable& a, const RegisteredDrawable& b) {
                  return a.drawOrder < b.drawOrder;
              });
}

void HDRPassRecorder::beginHDRRenderPass(vk::CommandBuffer vkCmd, vk::SubpassContents contents) {
    std::array<vk::ClearValue, 2> clearValues{};
    clearValues[0].color = vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = vk::ClearDepthStencilValue{1.0f, 0};

    VkExtent2D rawExtent = postProcess_.getExtent();
    vk::Extent2D hdrExtent = vk::Extent2D{}.setWidth(rawExtent.width).setHeight(rawExtent.height);

    auto hdrPassInfo = vk::RenderPassBeginInfo{}
        .setRenderPass(postProcess_.getHDRRenderPass())
        .setFramebuffer(postProcess_.getHDRFramebuffer())
        .setRenderArea(vk::Rect2D{{0, 0}, hdrExtent})
        .setClearValues(clearValues);

    vkCmd.beginRenderPass(hdrPassInfo, contents);
}

void HDRPassRecorder::record(VkCommandBuffer cmd, uint32_t frameIndex, float time, const Params& params) {
    vk::CommandBuffer vkCmd(cmd);

    profiler_.beginGpuZone(cmd, "HDRPass");

    beginHDRRenderPass(vkCmd, vk::SubpassContents::eInline);

    // Iterate registered drawables in order
    for (const auto& entry : drawables_) {
        if (entry.drawable->shouldDraw(frameIndex, params)) {
            profiler_.beginGpuZone(cmd, entry.profileZone);
            entry.drawable->recordHDRDraw(cmd, frameIndex, time, params);
            profiler_.endGpuZone(cmd, entry.profileZone);
        }
    }

    vkCmd.endRenderPass();

    profiler_.endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordWithSecondaries(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                            const std::vector<vk::CommandBuffer>& secondaries,
                                            const Params& params) {
    (void)params;
    vk::CommandBuffer vkCmd(cmd);

    profiler_.beginGpuZone(cmd, "HDRPass");

    beginHDRRenderPass(vkCmd, vk::SubpassContents::eSecondaryCommandBuffers);

    // Execute all pre-recorded secondary command buffers
    if (!secondaries.empty()) {
        vkCmd.executeCommands(secondaries);
    }

    vkCmd.endRenderPass();

    profiler_.endGpuZone(cmd, "HDRPass");
}

void HDRPassRecorder::recordSecondarySlot(VkCommandBuffer cmd, uint32_t frameIndex, float time,
                                          uint32_t slot, const Params& params) {
    // Record only drawables assigned to this slot
    for (const auto& entry : drawables_) {
        if (entry.slot == static_cast<int>(slot) && entry.drawable->shouldDraw(frameIndex, params)) {
            profiler_.beginGpuZone(cmd, entry.profileZone);
            entry.drawable->recordHDRDraw(cmd, frameIndex, time, params);
            profiler_.endGpuZone(cmd, entry.profileZone);
        }
    }
}
