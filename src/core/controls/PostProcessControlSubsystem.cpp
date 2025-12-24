#include "PostProcessControlSubsystem.h"
#include "PostProcessSystem.h"
#include "CloudShadowSystem.h"

// Cloud shadows
void PostProcessControlSubsystem::setCloudShadowEnabled(bool enabled) {
    cloudShadow_.setEnabled(enabled);
}

bool PostProcessControlSubsystem::isCloudShadowEnabled() const {
    return cloudShadow_.isEnabled();
}

void PostProcessControlSubsystem::setCloudShadowIntensity(float intensity) {
    cloudShadow_.setShadowIntensity(intensity);
}

float PostProcessControlSubsystem::getCloudShadowIntensity() const {
    return cloudShadow_.getShadowIntensity();
}

// Bloom
void PostProcessControlSubsystem::setBloomEnabled(bool enabled) {
    postProcess_.setBloomEnabled(enabled);
}

bool PostProcessControlSubsystem::isBloomEnabled() const {
    return postProcess_.isBloomEnabled();
}

// God rays
void PostProcessControlSubsystem::setGodRaysEnabled(bool enabled) {
    postProcess_.setGodRaysEnabled(enabled);
}

bool PostProcessControlSubsystem::isGodRaysEnabled() const {
    return postProcess_.isGodRaysEnabled();
}

void PostProcessControlSubsystem::setGodRayQuality(int quality) {
    postProcess_.setGodRayQuality(static_cast<PostProcessSystem::GodRayQuality>(quality));
}

int PostProcessControlSubsystem::getGodRayQuality() const {
    return static_cast<int>(postProcess_.getGodRayQuality());
}

// Froxel volumetric fog quality
void PostProcessControlSubsystem::setFroxelFilterQuality(bool highQuality) {
    postProcess_.setFroxelFilterQuality(highQuality);
}

bool PostProcessControlSubsystem::isFroxelFilterHighQuality() const {
    return postProcess_.isFroxelFilterHighQuality();
}

// Local tone mapping
void PostProcessControlSubsystem::setLocalToneMapEnabled(bool enabled) {
    postProcess_.setLocalToneMapEnabled(enabled);
}

bool PostProcessControlSubsystem::isLocalToneMapEnabled() const {
    return postProcess_.isLocalToneMapEnabled();
}

void PostProcessControlSubsystem::setLocalToneMapContrast(float contrast) {
    postProcess_.setLocalToneMapContrast(contrast);
}

float PostProcessControlSubsystem::getLocalToneMapContrast() const {
    return postProcess_.getLocalToneMapContrast();
}

void PostProcessControlSubsystem::setLocalToneMapDetail(float detail) {
    postProcess_.setLocalToneMapDetail(detail);
}

float PostProcessControlSubsystem::getLocalToneMapDetail() const {
    return postProcess_.getLocalToneMapDetail();
}

void PostProcessControlSubsystem::setBilateralBlend(float blend) {
    postProcess_.setBilateralBlend(blend);
}

float PostProcessControlSubsystem::getBilateralBlend() const {
    return postProcess_.getBilateralBlend();
}

// Exposure
void PostProcessControlSubsystem::setAutoExposureEnabled(bool enabled) {
    postProcess_.setAutoExposure(enabled);
}

bool PostProcessControlSubsystem::isAutoExposureEnabled() const {
    return postProcess_.isAutoExposureEnabled();
}

void PostProcessControlSubsystem::setManualExposure(float ev) {
    postProcess_.setExposure(ev);
}

float PostProcessControlSubsystem::getManualExposure() const {
    return postProcess_.getExposure();
}

float PostProcessControlSubsystem::getCurrentExposure() const {
    return postProcess_.getCurrentExposure();
}
