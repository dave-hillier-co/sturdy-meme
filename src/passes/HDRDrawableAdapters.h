#pragma once

// ============================================================================
// HDRDrawableAdapters.h - Simple adapters wrapping existing interfaces as IHDRDrawable
// ============================================================================
//
// These adapters allow systems that already implement IRecordable or
// IRecordableAnimated to be registered with HDRPassRecorder without
// modification. Each adapter holds a non-owning reference to the target.
//

#include "interfaces/IHDRDrawable.h"
#include "interfaces/IRecordable.h"

/**
 * Wraps an IRecordable as an IHDRDrawable.
 *
 * For systems like SkySystem, CatmullClarkSystem that use
 * recordDraw(cmd, frameIndex).
 */
class RecordableDrawable : public IHDRDrawable {
    IRecordable& target_;
public:
    explicit RecordableDrawable(IRecordable& target) : target_(target) {}

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float /*time*/, const HDRDrawParams& /*params*/) override {
        target_.recordDraw(cmd, frameIndex);
    }
};

/**
 * Wraps an IRecordableAnimated as an IHDRDrawable.
 *
 * For systems like GrassSystem, LeafSystem, WeatherSystem that use
 * recordDraw(cmd, frameIndex, time).
 */
class AnimatedRecordableDrawable : public IHDRDrawable {
    IRecordableAnimated& target_;
public:
    explicit AnimatedRecordableDrawable(IRecordableAnimated& target) : target_(target) {}

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float time, const HDRDrawParams& /*params*/) override {
        target_.recordDraw(cmd, frameIndex, time);
    }
};

/**
 * Wraps an IRecordable with a conditional check on terrainEnabled.
 */
class TerrainDrawable : public IHDRDrawable {
    IRecordable& target_;
public:
    explicit TerrainDrawable(IRecordable& target) : target_(target) {}

    bool shouldDraw(uint32_t /*frameIndex*/, const HDRDrawParams& params) const override {
        return params.terrainEnabled;
    }

    void recordHDRDraw(VkCommandBuffer cmd, uint32_t frameIndex,
                        float /*time*/, const HDRDrawParams& /*params*/) override {
        target_.recordDraw(cmd, frameIndex);
    }
};
