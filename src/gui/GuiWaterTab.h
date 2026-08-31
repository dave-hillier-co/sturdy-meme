#pragma once

class WaterSystem;
class WaterTileCull;
class OceanFFT;

namespace GuiWaterTab {
    // oceanFFT is nullable: the FFT ocean is optional and created late.
    void render(WaterSystem& water, WaterTileCull& tileCull, OceanFFT* oceanFFT);
}
