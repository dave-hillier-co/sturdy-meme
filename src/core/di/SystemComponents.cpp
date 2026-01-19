// SystemComponents.cpp - Fruit DI component implementations
//
// This file provides the Fruit binding implementations for rendering systems.
// Each component function tells Fruit how to create instances of the systems.

#include "SystemComponents.h"

// Include system headers
#include "TimeSystem.h"
#include "CelestialCalculator.h"
#include "ResizeCoordinator.h"
#include "UBOBuilder.h"
#include "ErosionDataLoader.h"
#include "RoadNetworkLoader.h"
#include "RoadRiverVisualization.h"
#include "EnvironmentSettings.h"

namespace di {

// ============================================================================
// Simple Systems - Default constructors registered with Fruit
// ============================================================================

fruit::Component<TimeSystem> getTimeSystemComponent() {
    return fruit::createComponent()
        .registerConstructor<TimeSystem()>();
}

fruit::Component<CelestialCalculator> getCelestialCalculatorComponent() {
    return fruit::createComponent()
        .registerConstructor<CelestialCalculator()>();
}

fruit::Component<ResizeCoordinator> getResizeCoordinatorComponent() {
    return fruit::createComponent()
        .registerConstructor<ResizeCoordinator()>();
}

fruit::Component<UBOBuilder> getUBOBuilderComponent() {
    return fruit::createComponent()
        .registerConstructor<UBOBuilder()>();
}

fruit::Component<ErosionDataLoader> getErosionDataLoaderComponent() {
    return fruit::createComponent()
        .registerConstructor<ErosionDataLoader()>();
}

fruit::Component<RoadNetworkLoader> getRoadNetworkLoaderComponent() {
    return fruit::createComponent()
        .registerConstructor<RoadNetworkLoader()>();
}

fruit::Component<RoadRiverVisualization> getRoadRiverVisualizationComponent() {
    return fruit::createComponent()
        .registerConstructor<RoadRiverVisualization()>();
}

fruit::Component<EnvironmentSettings> getEnvironmentSettingsComponent() {
    return fruit::createComponent()
        .registerConstructor<EnvironmentSettings()>();
}

// ============================================================================
// Infrastructure Component - Combines all simple systems
// ============================================================================

InfrastructureComponent getInfrastructureComponent() {
    return fruit::createComponent()
        .install(getTimeSystemComponent)
        .install(getCelestialCalculatorComponent)
        .install(getResizeCoordinatorComponent)
        .install(getUBOBuilderComponent)
        .install(getErosionDataLoaderComponent)
        .install(getRoadNetworkLoaderComponent)
        .install(getRoadRiverVisualizationComponent)
        .install(getEnvironmentSettingsComponent);
}

} // namespace di
