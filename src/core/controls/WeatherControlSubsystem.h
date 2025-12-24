#pragma once

#include "interfaces/IWeatherControl.h"
#include <glm/glm.hpp>

class WeatherSystem;
struct EnvironmentSettings;

/**
 * WeatherControlSubsystem - Implements IWeatherControl
 * Coordinates WeatherSystem and EnvironmentSettings for weather and snow control.
 */
class WeatherControlSubsystem : public IWeatherControl {
public:
    WeatherControlSubsystem(WeatherSystem& weather, EnvironmentSettings& envSettings)
        : weather_(weather)
        , envSettings_(envSettings) {}

    void setWeatherType(uint32_t type) override;
    uint32_t getWeatherType() const override;
    void setWeatherIntensity(float intensity) override;
    float getIntensity() const override;

    void setSnowAmount(float amount) override;
    float getSnowAmount() const override;
    void setSnowColor(const glm::vec3& color) override;
    const glm::vec3& getSnowColor() const override;

    EnvironmentSettings& getEnvironmentSettings() override;

private:
    WeatherSystem& weather_;
    EnvironmentSettings& envSettings_;
};
