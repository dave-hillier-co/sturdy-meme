#include "DebugLinesDrawable.h"

#include "DebugLineSystem.h"
#include "PostProcessSystem.h"

DebugLinesDrawable::DebugLinesDrawable(DebugLineSystem& debugLine, PostProcessSystem& postProcess)
    : debugLine_(debugLine), postProcess_(postProcess)
{
}

bool DebugLinesDrawable::shouldDraw(uint32_t /*frameIndex*/, const HDRDrawParams& /*params*/) const {
    return debugLine_.hasLines();
}

void DebugLinesDrawable::recordHDRDraw(VkCommandBuffer cmd, uint32_t /*frameIndex*/,
                                        float /*time*/, const HDRDrawParams& params) {
    vk::CommandBuffer vkCmd(cmd);

    // Set up viewport and scissor for debug rendering
    auto viewport = vk::Viewport{}
        .setX(0.0f)
        .setY(0.0f)
        .setWidth(static_cast<float>(postProcess_.getExtent().width))
        .setHeight(static_cast<float>(postProcess_.getExtent().height))
        .setMinDepth(0.0f)
        .setMaxDepth(1.0f);
    vkCmd.setViewport(0, viewport);

    VkExtent2D debugExtent = postProcess_.getExtent();
    auto scissor = vk::Rect2D{}
        .setOffset({0, 0})
        .setExtent(vk::Extent2D{}.setWidth(debugExtent.width).setHeight(debugExtent.height));
    vkCmd.setScissor(0, scissor);

    debugLine_.recordCommands(cmd, params.viewProj);
}
