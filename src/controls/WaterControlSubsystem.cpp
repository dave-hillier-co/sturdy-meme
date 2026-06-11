#include "WaterControlSubsystem.h"
#include "WaterSystem.h"
#include "WaterTileCull.h"

WaterSystem& WaterControlSubsystem::getWaterSystem() {
    return water_;
}

const WaterSystem& WaterControlSubsystem::getWaterSystem() const {
    return water_;
}

WaterTileCull& WaterControlSubsystem::getWaterTileCull() {
    return waterTileCull_;
}

const WaterTileCull& WaterControlSubsystem::getWaterTileCull() const {
    return waterTileCull_;
}

OceanFFT* WaterControlSubsystem::getOceanFFT() {
    return oceanFFT_;
}

const OceanFFT* WaterControlSubsystem::getOceanFFT() const {
    return oceanFFT_;
}
