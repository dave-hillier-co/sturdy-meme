#pragma once

class WaterSystem;
class WaterTileCull;

/**
 * Interface for water system controls.
 * Used by GuiWaterTab to control water level, waves, and appearance.
 */
class IWaterControl {
public:
    virtual ~IWaterControl() = default;

    // Water system access (for detailed wave/appearance configuration)
    virtual WaterSystem& getWaterSystem() = 0;
    virtual const WaterSystem& getWaterSystem() const = 0;

    // Water tile culling (performance optimization)
    virtual WaterTileCull& getWaterTileCull() = 0;
    virtual const WaterTileCull& getWaterTileCull() const = 0;
};
