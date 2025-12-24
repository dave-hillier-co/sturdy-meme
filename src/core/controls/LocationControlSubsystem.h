#pragma once

#include "interfaces/ILocationControl.h"

class CelestialCalculator;

/**
 * LocationControlSubsystem - Implements ILocationControl
 * Delegates to CelestialCalculator for geographic location management.
 */
class LocationControlSubsystem : public ILocationControl {
public:
    explicit LocationControlSubsystem(CelestialCalculator& celestial)
        : celestial_(celestial) {}

    void setLocation(const GeographicLocation& location) override;
    const GeographicLocation& getLocation() const override;

private:
    CelestialCalculator& celestial_;
};
