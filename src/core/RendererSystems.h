#pragma once

// ============================================================================
// RendererSystems.h - Dependency Injection Container for Rendering Subsystems
// ============================================================================
//
// This file includes the Fruit DI-based RendererSystems implementation.
// RendererSystems uses Google's Fruit library for dependency injection:
//
// - Systems with simple constructors (TimeSystem, CelestialCalculator, etc.)
//   are owned and created by the Fruit injector
// - Systems requiring Vulkan resources use factory patterns with setters
//
// The Fruit injector provides automatic lifecycle management for DI-managed
// systems, while complex systems maintain their existing factory patterns.

#include "di/SystemsInjector.h"
