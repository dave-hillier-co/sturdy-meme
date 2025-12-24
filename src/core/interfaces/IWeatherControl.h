#pragma once

#include <glm/glm.hpp>
#include <cstdint>

struct EnvironmentSettings;

/**
 * Interface for weather and snow controls.
 * Used by GuiWeatherTab to control weather type, intensity, snow, and wind.
 */
class IWeatherControl {
public:
    virtual ~IWeatherControl() = default;

    // Weather type (0 = Rain, 1 = Snow)
    virtual void setWeatherType(uint32_t type) = 0;
    virtual uint32_t getWeatherType() const = 0;

    // Weather intensity (0.0 = clear, 1.0 = heavy)
    virtual void setWeatherIntensity(float intensity) = 0;
    virtual float getIntensity() const = 0;

    // Snow coverage
    virtual void setSnowAmount(float amount) = 0;
    virtual float getSnowAmount() const = 0;
    virtual void setSnowColor(const glm::vec3& color) = 0;
    virtual const glm::vec3& getSnowColor() const = 0;

    // Environment settings (wind, snow properties, grass interaction)
    virtual EnvironmentSettings& getEnvironmentSettings() = 0;
};
