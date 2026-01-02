#include <town_generator/wards/PatriciateWard.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

PatriciateWard::PatriciateWard()
    : CommonWard(
        nullptr, nullptr,
        80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
        0.5f + Random::getFloat() * 0.3f,                          // moderately regular
        0.8f,
        0.2f
    ) {
}

PatriciateWard::PatriciateWard(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
        0.5f + Random::getFloat() * 0.3f,                          // moderately regular
        0.8f,
        0.2f
    ) {
}

float PatriciateWard::rateLocation(Model* model, Patch* patch) {
    // Patriciate ward prefers to border a park and not to border slums
    // TODO: Implement when Model class is available
    (void)model;
    (void)patch;
    return 0.0f;
}

} // namespace town_generator
