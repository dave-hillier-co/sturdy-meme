#include <town_generator/wards/AdministrationWard.h>
#include <town_generator/building/Patch.h>
#include <town_generator/building/Model.h>
#include <town_generator/utils/Random.h>

namespace town_generator {

AdministrationWard::AdministrationWard()
    : CommonWard(
        nullptr, nullptr,
        80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
        0.1f + Random::getFloat() * 0.3f,                          // regular
        0.3f,
        0.04f
    ) {
}

AdministrationWard::AdministrationWard(Model* model, Patch* patch)
    : CommonWard(
        model, patch,
        80.0f + 30.0f * Random::getFloat() * Random::getFloat(),  // large
        0.1f + Random::getFloat() * 0.3f,                          // regular
        0.3f,
        0.04f
    ) {
}

float AdministrationWard::rateLocation(Model* model, Patch* patch) {
    // Ideally administration ward should overlook the plaza,
    // otherwise it should be as close to the plaza as possible
    // TODO: Implement when Model class is available with plaza
    (void)model;
    if (!patch) return 0.0f;
    return patch->shape.square();
}

} // namespace town_generator
