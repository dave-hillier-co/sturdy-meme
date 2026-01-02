#include <town_generator/wards/GateWard.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Patch.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

GateWard::GateWard()
    : CommonWard(
        nullptr, nullptr,
        10.0f + 50.0f * Random::getFloat() * Random::getFloat(),
        0.5f + Random::getFloat() * 0.3f,
        0.7f,
        0.04f
    ) {
}

GateWard::GateWard(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        10.0f + 50.0f * Random::getFloat() * Random::getFloat(),
        0.5f + Random::getFloat() * 0.3f,
        0.7f,
        0.04f
    ) {
}

} // namespace town_generator
