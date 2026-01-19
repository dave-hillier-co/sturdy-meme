#pragma once

// SystemComponents.h - Fruit DI component definitions for rendering systems
//
// This file defines Fruit components that specify how to create and wire
// rendering subsystems. The Fruit injector owns all created objects.
//
// Key concepts:
// - Components define factories/constructors for types
// - Dependencies between components are resolved automatically
// - The Injector owns all created objects (no manual unique_ptr)

#include <fruit/fruit.h>
#include <vulkan/vulkan.h>
#include <string>

// Forward declarations for systems
class TimeSystem;
class CelestialCalculator;
class ResizeCoordinator;
class UBOBuilder;
class ErosionDataLoader;
class RoadNetworkLoader;
class RoadRiverVisualization;
struct EnvironmentSettings;

// Forward declaration for init context
struct InitContext;

namespace di {

// ============================================================================
// Injectable wrapper for InitContext (runtime parameter)
// ============================================================================
struct InitContextHolder {
    const InitContext* ctx = nullptr;
};

// ============================================================================
// Simple Systems Component - Systems with default constructors
// These systems have no complex dependencies and can be directly constructed.
// ============================================================================

// TimeSystem - manages time of day, delta time
fruit::Component<TimeSystem> getTimeSystemComponent();

// CelestialCalculator - astronomical calculations
fruit::Component<CelestialCalculator> getCelestialCalculatorComponent();

// ResizeCoordinator - handles window resize coordination
fruit::Component<ResizeCoordinator> getResizeCoordinatorComponent();

// UBOBuilder - builds uniform buffer objects
fruit::Component<UBOBuilder> getUBOBuilderComponent();

// ErosionDataLoader - loads erosion/river data
fruit::Component<ErosionDataLoader> getErosionDataLoaderComponent();

// RoadNetworkLoader - loads road network data
fruit::Component<RoadNetworkLoader> getRoadNetworkLoaderComponent();

// RoadRiverVisualization - debug visualization
fruit::Component<RoadRiverVisualization> getRoadRiverVisualizationComponent();

// EnvironmentSettings - environment configuration
fruit::Component<EnvironmentSettings> getEnvironmentSettingsComponent();

// ============================================================================
// Infrastructure Component - Combines all simple systems
// ============================================================================
using InfrastructureComponent = fruit::Component<
    TimeSystem,
    CelestialCalculator,
    ResizeCoordinator,
    UBOBuilder,
    ErosionDataLoader,
    RoadNetworkLoader,
    RoadRiverVisualization,
    EnvironmentSettings
>;

InfrastructureComponent getInfrastructureComponent();

} // namespace di
