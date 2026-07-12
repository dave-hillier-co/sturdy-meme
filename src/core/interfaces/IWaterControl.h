#pragma once

class WaterSystem;
class WaterTileCull;
class OceanFFT;

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

    // FFT ocean simulation (optional - may be null)
    virtual OceanFFT* getOceanFFT() = 0;
    virtual const OceanFFT* getOceanFFT() const = 0;
};
