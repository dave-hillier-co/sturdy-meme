#include "WeatherControlSubsystem.h"
#include "WeatherSystem.h"
#include "EnvironmentSettings.h"
#include <glm/glm.hpp>

void WeatherControlSubsystem::setWeatherType(uint32_t type) {
    weather_.setWeatherType(type);
}

uint32_t WeatherControlSubsystem::getWeatherType() const {
    return weather_.getWeatherType();
}

void WeatherControlSubsystem::setWeatherIntensity(float intensity) {
    weather_.setIntensity(intensity);
}

float WeatherControlSubsystem::getIntensity() const {
    return weather_.getIntensity();
}

void WeatherControlSubsystem::setSnowAmount(float amount) {
    envSettings_.snowAmount = glm::clamp(amount, 0.0f, 1.0f);
}

float WeatherControlSubsystem::getSnowAmount() const {
    return envSettings_.snowAmount;
}

void WeatherControlSubsystem::setSnowColor(const glm::vec3& color) {
    envSettings_.snowColor = color;
}

const glm::vec3& WeatherControlSubsystem::getSnowColor() const {
    return envSettings_.snowColor;
}

EnvironmentSettings& WeatherControlSubsystem::getEnvironmentSettings() {
    return envSettings_;
}
