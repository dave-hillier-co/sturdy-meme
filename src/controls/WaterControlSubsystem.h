#pragma once

#include "interfaces/IWaterControl.h"

class WaterSystem;
class WaterTileCull;
class OceanFFT;

/**
 * WaterControlSubsystem - Implements IWaterControl
 * Wraps WaterSystem, WaterTileCull, and OceanFFT for water rendering control.
 */
class WaterControlSubsystem : public IWaterControl {
public:
    WaterControlSubsystem(WaterSystem& water, WaterTileCull& waterTileCull, OceanFFT* oceanFFT)
        : water_(water)
        , waterTileCull_(waterTileCull)
        , oceanFFT_(oceanFFT) {}

    WaterSystem& getWaterSystem() override;
    const WaterSystem& getWaterSystem() const override;
    WaterTileCull& getWaterTileCull() override;
    const WaterTileCull& getWaterTileCull() const override;
    OceanFFT* getOceanFFT() override;
    const OceanFFT* getOceanFFT() const override;

private:
    WaterSystem& water_;
    WaterTileCull& waterTileCull_;
    OceanFFT* oceanFFT_ = nullptr;  // Optional
};
