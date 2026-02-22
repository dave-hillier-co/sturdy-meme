#include "WaterDrawable.h"
#include "WaterTileCull.h"

bool WaterDrawable::shouldDraw(uint32_t frameIndex, const HDRDrawParams& /*params*/) const {
    return !tileCull_ || tileCull_->wasWaterVisibleLastFrame(frameIndex);
}
