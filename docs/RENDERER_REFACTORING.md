# Renderer Architecture Refactoring Plan

This document outlines a refactoring plan to address dependency management and architectural concerns in the renderer subsystem.

## Current Issues

### 1. RendererSystems - Service Locator Anti-Pattern

`RendererSystems` (~570 lines) owns 40+ systems and provides 100+ accessor methods. It acts as a global service locator where any system needing another reaches through RendererSystems.

**Problems:**
- No clear single responsibility
- Tight coupling - everything depends on this one class
- Difficult to test systems in isolation
- Changes propagate through the entire codebase

### 2. SystemWiring - Concrete Type Coupling

`SystemWiring` contains methods like `wireGrassDescriptors()` that reach into multiple concrete system types to connect resources.

```cpp
void SystemWiring::wireGrassDescriptors(RendererSystems& systems) {
    auto& grass = systems.grass();
    auto& shadow = systems.shadow();
    auto& terrain = systems.terrain();
    auto& cloudShadow = systems.cloudShadow();
    // ... knows internals of 5+ systems
}
```

**Problems:**
- Each wire method requires knowledge of multiple system internals
- Adding new systems requires modifying SystemWiring
- Systems cannot manage their own dependencies

### 3. RendererInitPhases - Monolithic Initialization

`RendererInitPhases.cpp` (~560 lines) has 50+ includes and creates all systems. Initialization logic is split across:
- `Renderer::initSubsystems()`
- SystemGroup factories (`VegetationSystemGroup::createAll()`, etc.)
- `SystemWiring` (post-init wiring)

### 4. Mixed Responsibilities

`RendererSystems` combines three distinct concerns:
1. **Ownership** - unique_ptrs for all systems
2. **Access** - getters for systems and groups
3. **Control Interfaces** - GUI-facing adapters (IEnvironmentControl, IWaterControl, etc.)

## Proposed Architecture

### Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│                          Renderer                                    │
│  - Frame execution (render loop)                                    │
│  - Owns SystemGroups (6-8 instead of 40+ systems)                   │
│  - Provides focused resource bundles                                │
└─────────────────────────────────────────────────────────────────────┘
                              │
                              ├── CoreSystemGroup (owns Shadow, PostProcess, GlobalBuffers)
                              ├── TerrainSystemGroup (owns Terrain)
                              ├── AtmosphereSystemGroup (owns Sky, Froxel, AtmosphereLUT, CloudShadow)
                              ├── VegetationSystemGroup (owns Grass, Wind, Trees, Rocks, Displacement)
                              ├── WaterSystemGroup (owns Water, Foam, SSR, Displacement)
                              ├── SnowSystemGroup (owns SnowMask, VolumetricSnow, Weather, Leaf)
                              ├── InfrastructureGroup (owns Scene, Profiler, DebugLine)
                              └── ControlLayer (GUI adapters - separate concern)
```

### Interface-Based Wiring

Systems communicate through provider/consumer interfaces rather than concrete types.

#### Provider Interfaces

Define what resources a system offers:

```cpp
// src/core/interfaces/providers/IShadowMapProvider.h
class IShadowMapProvider {
public:
    virtual ~IShadowMapProvider() = default;
    virtual VkImageView getShadowMapView() const = 0;
    virtual VkSampler getShadowMapSampler() const = 0;
};

// src/core/interfaces/providers/IWindBufferProvider.h
class IWindBufferProvider {
public:
    virtual ~IWindBufferProvider() = default;
    virtual VkBuffer getWindBuffer(uint32_t frameIndex) const = 0;
    virtual uint32_t getFrameCount() const = 0;
};

// src/core/interfaces/providers/IHeightMapProvider.h
class IHeightMapProvider {
public:
    virtual ~IHeightMapProvider() = default;
    virtual VkImageView getHeightMapView() const = 0;
    virtual VkSampler getHeightMapSampler() const = 0;
};

// src/core/interfaces/providers/ICloudShadowProvider.h
class ICloudShadowProvider {
public:
    virtual ~ICloudShadowProvider() = default;
    virtual VkImageView getCloudShadowView() const = 0;
    virtual VkSampler getCloudShadowSampler() const = 0;
};
```

#### Consumer Interfaces

Define what bindings a system accepts:

```cpp
// src/core/interfaces/consumers/IShadowMapConsumer.h
class IShadowMapConsumer {
public:
    virtual ~IShadowMapConsumer() = default;
    virtual void bindShadowMap(VkDevice device, VkImageView view, VkSampler sampler) = 0;
};

// src/core/interfaces/consumers/ICloudShadowConsumer.h
class ICloudShadowConsumer {
public:
    virtual ~ICloudShadowConsumer() = default;
    virtual void bindCloudShadow(VkDevice device, VkImageView view, VkSampler sampler) = 0;
};
```

#### Systems Implement Interfaces

```cpp
class ShadowSystem : public IShadowMapProvider {
public:
    VkImageView getShadowMapView() const override { return shadowImageView_; }
    VkSampler getShadowMapSampler() const override { return shadowSampler_; }
};

class TerrainSystem : public IShadowMapConsumer,
                      public ICloudShadowConsumer,
                      public IHeightMapProvider {
public:
    void bindShadowMap(VkDevice device, VkImageView view, VkSampler sampler) override;
    void bindCloudShadow(VkDevice device, VkImageView view, VkSampler sampler) override;
    VkImageView getHeightMapView() const override { return heightMapView_; }
    VkSampler getHeightMapSampler() const override { return heightMapSampler_; }
};
```

#### Wiring Classes Work with Interfaces Only

```cpp
// src/core/wiring/ShadowWiring.h
class ShadowWiring {
public:
    explicit ShadowWiring(VkDevice device) : device_(device) {}

    void wire(const IShadowMapProvider& provider,
              std::vector<IShadowMapConsumer*> consumers) {
        auto view = provider.getShadowMapView();
        auto sampler = provider.getShadowMapSampler();
        for (auto* consumer : consumers) {
            consumer->bindShadowMap(device_, view, sampler);
        }
    }

private:
    VkDevice device_;
};

// src/core/wiring/CloudShadowWiring.h
class CloudShadowWiring {
public:
    explicit CloudShadowWiring(VkDevice device) : device_(device) {}

    void wire(const ICloudShadowProvider& provider,
              std::vector<ICloudShadowConsumer*> consumers) {
        auto view = provider.getCloudShadowView();
        auto sampler = provider.getCloudShadowSampler();
        for (auto* consumer : consumers) {
            consumer->bindCloudShadow(device_, view, sampler);
        }
    }

private:
    VkDevice device_;
};
```

### Owning SystemGroups

Convert existing non-owning group structs to owning containers:

```cpp
// src/core/groups/AtmosphereSystemGroup.h
class AtmosphereSystemGroup : public ICloudShadowProvider {
public:
    struct CreateDeps {
        const InitContext& initCtx;
        VkRenderPass hdrRenderPass;
        VkImageView cascadeShadowView;
        VkSampler shadowSampler;
        const std::vector<VkBuffer>& lightBuffers;
    };

    static std::unique_ptr<AtmosphereSystemGroup> create(const CreateDeps& deps);

    // Provider interface (delegates to internal system)
    VkImageView getCloudShadowView() const override;
    VkSampler getCloudShadowSampler() const override;

    // Access to systems for rendering (not wiring)
    SkySystem& sky() { return *sky_; }
    FroxelSystem& froxel() { return *froxel_; }
    AtmosphereLUTSystem& atmosphereLUT() { return *atmosphereLUT_; }
    CloudShadowSystem& cloudShadow() { return *cloudShadow_; }

private:
    std::unique_ptr<SkySystem> sky_;
    std::unique_ptr<FroxelSystem> froxel_;
    std::unique_ptr<AtmosphereLUTSystem> atmosphereLUT_;
    std::unique_ptr<CloudShadowSystem> cloudShadow_;
};
```

### RendererBuilder - Composition Root

One class responsible for creation order and cross-group wiring:

```cpp
// src/core/RendererBuilder.h
class RendererBuilder {
public:
    struct Config {
        VkDevice device;
        VmaAllocator allocator;
        VkRenderPass hdrRenderPass;
        VkRenderPass shadowRenderPass;
        std::string resourcePath;
        uint32_t framesInFlight;
    };

    explicit RendererBuilder(const Config& config);

    // Build phases - each returns success/failure
    bool buildCoreGroup();
    bool buildTerrainGroup();
    bool buildAtmosphereGroup();
    bool buildVegetationGroup();
    bool buildWaterGroup();
    bool buildSnowGroup();

    // Cross-group wiring via interfaces
    void wireGroups();

    // Transfer ownership to caller
    struct BuiltGroups {
        std::unique_ptr<CoreSystemGroup> core;
        std::unique_ptr<TerrainSystemGroup> terrain;
        std::unique_ptr<AtmosphereSystemGroup> atmosphere;
        std::unique_ptr<VegetationSystemGroup> vegetation;
        std::unique_ptr<WaterSystemGroup> water;
        std::unique_ptr<SnowSystemGroup> snow;
        std::unique_ptr<InfrastructureGroup> infrastructure;
    };
    BuiltGroups build();

private:
    Config config_;
    // Groups being built...
};
```

### ControlLayer - Separate GUI Concern

Extract control interfaces from RendererSystems:

```cpp
// src/core/ControlLayer.h
class ControlLayer {
public:
    void init(CoreSystemGroup& core,
              AtmosphereSystemGroup& atmosphere,
              VegetationSystemGroup& vegetation,
              WaterSystemGroup& water,
              PerformanceToggles& perfToggles);

    // Interface accessors for GUI
    IEnvironmentControl& environment();
    IWaterControl& water();
    ITreeControl& trees();
    IGrassControl& grass();
    IDebugControl& debug();
    IPerformanceControl& performance();

private:
    std::unique_ptr<EnvironmentControlSubsystem> environmentCtrl_;
    std::unique_ptr<WaterControlSubsystem> waterCtrl_;
    std::unique_ptr<TreeControlSubsystem> treeCtrl_;
    // ...
};
```

### Renderer Becomes the Holder

```cpp
// src/core/Renderer.h
class Renderer {
public:
    // Frame execution
    bool render(const Camera& camera);

    // Group access
    CoreSystemGroup& core() { return *core_; }
    AtmosphereSystemGroup& atmosphere() { return *atmosphere_; }
    VegetationSystemGroup& vegetation() { return *vegetation_; }
    WaterSystemGroup& water() { return *water_; }
    SnowSystemGroup& snow() { return *snow_; }
    TerrainSystemGroup& terrain() { return *terrain_; }

    // Focused resource bundles for pass recorders
    HDRPassResources hdrPassResources() const;
    ShadowPassResources shadowPassResources() const;

    // Control layer (GUI)
    ControlLayer& controls() { return *controls_; }

private:
    std::unique_ptr<CoreSystemGroup> core_;
    std::unique_ptr<TerrainSystemGroup> terrain_;
    std::unique_ptr<AtmosphereSystemGroup> atmosphere_;
    std::unique_ptr<VegetationSystemGroup> vegetation_;
    std::unique_ptr<WaterSystemGroup> water_;
    std::unique_ptr<SnowSystemGroup> snow_;
    std::unique_ptr<InfrastructureGroup> infrastructure_;
    std::unique_ptr<ControlLayer> controls_;
};
```

## Migration Path

### Phase 1: Define Provider/Consumer Interfaces

1. Create `src/core/interfaces/providers/` directory
2. Define interfaces for common resources:
   - `IShadowMapProvider`
   - `ICloudShadowProvider`
   - `IWindBufferProvider`
   - `IHeightMapProvider`
   - `IUniformBufferProvider`
3. Create `src/core/interfaces/consumers/` directory
4. Define corresponding consumer interfaces

### Phase 2: Systems Implement Interfaces

1. Have `ShadowSystem` implement `IShadowMapProvider`
2. Have `CloudShadowSystem` implement `ICloudShadowProvider`
3. Have `WindSystem` implement `IWindBufferProvider`
4. Have `TerrainSystem` implement `IHeightMapProvider`, `IShadowMapConsumer`, `ICloudShadowConsumer`
5. Continue for other systems

### Phase 3: Create Interface-Based Wiring Classes

1. Create `src/core/wiring/ShadowWiring.h`
2. Create `src/core/wiring/CloudShadowWiring.h`
3. Create other wiring classes as needed
4. These classes work only with interfaces

### Phase 4: Convert SystemGroups to Owning Containers

1. Start with `AtmosphereSystemGroup` (already has `createAll()`)
2. Make it own its systems instead of returning a bundle
3. Move internal wiring into the group's `create()` method
4. Repeat for other groups

### Phase 5: Create RendererBuilder

1. Create `RendererBuilder` class
2. Move creation logic from `RendererInitPhases.cpp`
3. Cross-group wiring uses interface-based wiring classes

### Phase 6: Extract ControlLayer

1. Create `ControlLayer` class
2. Move control subsystems from `RendererSystems`
3. Update GUI code to use `renderer.controls()`

### Phase 7: Update Renderer and Delete RendererSystems

1. Renderer holds groups directly
2. Update all consumers of `RendererSystems` to use groups or focused bundles
3. Delete `RendererSystems`

## Summary

| Component | Before | After |
|-----------|--------|-------|
| `RendererSystems` | 570 lines, 40+ systems, 100+ accessors | Deleted |
| System ownership | Individual unique_ptrs | Groups own their systems |
| Cross-system wiring | `SystemWiring` with concrete types | Interface-based wiring classes |
| GUI controls | Mixed into `RendererSystems` | Separate `ControlLayer` |
| Creation logic | Split across multiple files | `RendererBuilder` |
| Renderer | Holds `RendererSystems` | Holds 6-8 groups directly |

## Benefits

1. **Clear ownership** - Each group owns its systems
2. **Decoupled wiring** - Wiring classes know only interfaces
3. **Testable** - Systems can be tested with mock providers
4. **Focused responsibility** - Each class has one job
5. **Reduced coupling** - Changes don't propagate through RendererSystems
