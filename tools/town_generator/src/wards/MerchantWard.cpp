#include <town_generator/wards/MerchantWard.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

MerchantWard::MerchantWard()
    : CommonWard(
        nullptr, nullptr,
        50.0f + 60.0f * Random::getFloat() * Random::getFloat(),  // medium to large
        0.5f + Random::getFloat() * 0.3f,                          // moderately regular
        0.7f,
        0.15f
    ) {
}

MerchantWard::MerchantWard(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        50.0f + 60.0f * Random::getFloat() * Random::getFloat(),  // medium to large
        0.5f + Random::getFloat() * 0.3f,                          // moderately regular
        0.7f,
        0.15f
    ) {
}

float MerchantWard::rateLocation(Model* model, Patch* patch) {
    // Merchant ward should be as close to the center as possible
    // TODO: Implement when Model class is available with plaza
    (void)model;
    if (!patch) return 0.0f;
    return patch->shape.square();
}

} // namespace town_generator
