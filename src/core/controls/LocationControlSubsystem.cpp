#include "LocationControlSubsystem.h"
#include "CelestialCalculator.h"

void LocationControlSubsystem::setLocation(const GeographicLocation& location) {
    celestial_.setLocation(location);
}

const GeographicLocation& LocationControlSubsystem::getLocation() const {
    return celestial_.getLocation();
}
