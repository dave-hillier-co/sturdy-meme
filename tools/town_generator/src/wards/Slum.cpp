#include <town_generator/wards/Slum.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

Slum::Slum()
    : CommonWard(
        nullptr, nullptr,
        10.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // small to medium
        0.6f + Random::getFloat() * 0.4f,                          // chaotic
        0.8f,
        0.03f
    ) {
}

Slum::Slum(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        10.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // small to medium
        0.6f + Random::getFloat() * 0.4f,                          // chaotic
        0.8f,
        0.03f
    ) {
}

float Slum::rateLocation(Model* model, Patch* patch) {
    // Slums should be as far from the center as possible
    // TODO: Implement when Model class is available with plaza
    (void)model;
    if (!patch) return 0.0f;
    return -patch->shape.square();
}

} // namespace town_generator
