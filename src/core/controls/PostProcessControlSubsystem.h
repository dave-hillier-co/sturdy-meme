#pragma once

#include "interfaces/IPostProcessControl.h"

class PostProcessSystem;
class CloudShadowSystem;

/**
 * PostProcessControlSubsystem - Implements IPostProcessControl
 * Coordinates PostProcessSystem and CloudShadowSystem for post-processing effects.
 */
class PostProcessControlSubsystem : public IPostProcessControl {
public:
    PostProcessControlSubsystem(PostProcessSystem& postProcess, CloudShadowSystem& cloudShadow)
        : postProcess_(postProcess)
        , cloudShadow_(cloudShadow) {}

    // HDR pipeline
    void setHDRPassEnabled(bool enabled) override { hdrPassEnabled_ = enabled; }
    bool isHDRPassEnabled() const override { return hdrPassEnabled_; }
    void setHDREnabled(bool enabled) override { hdrEnabled_ = enabled; }
    bool isHDREnabled() const override { return hdrEnabled_; }

    // Cloud shadows
    void setCloudShadowEnabled(bool enabled) override;
    bool isCloudShadowEnabled() const override;
    void setCloudShadowIntensity(float intensity) override;
    float getCloudShadowIntensity() const override;

    // Bloom
    void setBloomEnabled(bool enabled) override;
    bool isBloomEnabled() const override;

    // God rays
    void setGodRaysEnabled(bool enabled) override;
    bool isGodRaysEnabled() const override;
    void setGodRayQuality(int quality) override;
    int getGodRayQuality() const override;

    // Froxel volumetric fog quality
    void setFroxelFilterQuality(bool highQuality) override;
    bool isFroxelFilterHighQuality() const override;

    // Local tone mapping (bilateral grid)
    void setLocalToneMapEnabled(bool enabled) override;
    bool isLocalToneMapEnabled() const override;
    void setLocalToneMapContrast(float contrast) override;
    float getLocalToneMapContrast() const override;
    void setLocalToneMapDetail(float detail) override;
    float getLocalToneMapDetail() const override;
    void setBilateralBlend(float blend) override;
    float getBilateralBlend() const override;

    // Exposure
    void setAutoExposureEnabled(bool enabled) override;
    bool isAutoExposureEnabled() const override;
    void setManualExposure(float ev) override;
    float getManualExposure() const override;
    float getCurrentExposure() const override;

    // Access to local state for Renderer
    bool& hdrEnabled() { return hdrEnabled_; }
    bool& hdrPassEnabled() { return hdrPassEnabled_; }

private:
    PostProcessSystem& postProcess_;
    CloudShadowSystem& cloudShadow_;
    bool hdrEnabled_ = true;
    bool hdrPassEnabled_ = true;
};
