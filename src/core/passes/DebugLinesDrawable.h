#pragma once

// ============================================================================
// DebugLinesDrawable.h - Debug line rendering as IHDRDrawable
// ============================================================================
//
// Handles debug line visualization with proper viewport/scissor setup.
// Only draws when debug lines are present.
//

#include "interfaces/IHDRDrawable.h"

class DebugLineSystem;
class PostProcessSystem;

/**
 * Renders debug lines in the HDR pass.
 *
 * Handles viewport and scissor setup before recording debug line commands.
 * Skips drawing entirely when no debug lines are queued (physics debug,
 * road/river visualization, etc.).
 */
class DebugLinesDrawable : public IHDRDrawable {
public:
    DebugLinesDrawable(DebugLineSystem& debugLine, PostProcessSystem& postProcess);

    bool shouldDraw(uint32_t frameIndex, const HDRDrawParams& params) const override;

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float time, const HDRDrawParams& params) override;

private:
    DebugLineSystem& debugLine_;
    PostProcessSystem& postProcess_;
};
