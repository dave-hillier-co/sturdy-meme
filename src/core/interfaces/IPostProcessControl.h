#pragma once

/**
 * Interface for post-processing controls.
 * Used by GuiPostFXTab to control HDR, bloom, god rays, tone mapping, etc.
 */
class IPostProcessControl {
public:
    virtual ~IPostProcessControl() = default;

    // HDR pipeline
    virtual void setHDRPassEnabled(bool enabled) = 0;
    virtual bool isHDRPassEnabled() const = 0;
    virtual void setHDREnabled(bool enabled) = 0;
    virtual bool isHDREnabled() const = 0;

    // Cloud shadows
    virtual void setCloudShadowEnabled(bool enabled) = 0;
    virtual bool isCloudShadowEnabled() const = 0;
    virtual void setCloudShadowIntensity(float intensity) = 0;
    virtual float getCloudShadowIntensity() const = 0;

    // Bloom
    virtual void setBloomEnabled(bool enabled) = 0;
    virtual bool isBloomEnabled() const = 0;

    // God rays
    virtual void setGodRaysEnabled(bool enabled) = 0;
    virtual bool isGodRaysEnabled() const = 0;
    virtual void setGodRayQuality(int quality) = 0;
    virtual int getGodRayQuality() const = 0;

    // Froxel volumetric fog quality
    virtual void setFroxelFilterQuality(bool highQuality) = 0;
    virtual bool isFroxelFilterHighQuality() const = 0;

    // Local tone mapping (bilateral grid)
    virtual void setLocalToneMapEnabled(bool enabled) = 0;
    virtual bool isLocalToneMapEnabled() const = 0;
    virtual void setLocalToneMapContrast(float contrast) = 0;
    virtual float getLocalToneMapContrast() const = 0;
    virtual void setLocalToneMapDetail(float detail) = 0;
    virtual float getLocalToneMapDetail() const = 0;
    virtual void setBilateralBlend(float blend) = 0;
    virtual float getBilateralBlend() const = 0;

    // Exposure
    virtual void setAutoExposureEnabled(bool enabled) = 0;
    virtual bool isAutoExposureEnabled() const = 0;
    virtual void setManualExposure(float ev) = 0;
    virtual float getManualExposure() const = 0;
    virtual float getCurrentExposure() const = 0;
};
