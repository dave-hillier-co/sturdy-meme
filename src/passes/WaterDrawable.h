#pragma once

// ============================================================================
// WaterDrawable.h - Water rendering with optional temporal tile culling
// ============================================================================

#include "interfaces/IHDRDrawable.h"
#include "interfaces/IRecordable.h"

class WaterTileCull;

/**
 * Wraps water rendering as an IHDRDrawable.
 *
 * Skips drawing when temporal tile culling determines no water tiles
 * were visible last frame.
 */
class WaterDrawable : public IHDRDrawable {
    IRecordable& water_;
    WaterTileCull* tileCull_;
public:
    WaterDrawable(IRecordable& water, WaterTileCull* tileCull)
        : water_(water), tileCull_(tileCull) {}

    bool shouldDraw(uint32_t frameIndex, const HDRDrawParams& params) const override;

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float /*time*/, const HDRDrawParams& /*params*/) override {
        water_.recordDraw(cmd, frameIndex);
    }
};
