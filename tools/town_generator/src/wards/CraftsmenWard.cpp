#include <town_generator/wards/CraftsmenWard.h>
#include <town_generator/building/Model.h>
#include <town_generator/building/Patch.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

CraftsmenWard::CraftsmenWard()
    : CommonWard(
        nullptr, nullptr,
        10.0f + 80.0f * Random::getFloat() * Random::getFloat(),  // small to large
        0.5f + Random::getFloat() * 0.2f,                          // moderately regular
        0.6f,
        0.04f
    ) {
}

CraftsmenWard::CraftsmenWard(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        10.0f + 80.0f * Random::getFloat() * Random::getFloat(),  // small to large
        0.5f + Random::getFloat() * 0.2f,                          // moderately regular
        0.6f,
        0.04f
    ) {
}

} // namespace town_generator
