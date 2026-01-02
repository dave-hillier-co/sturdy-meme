#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class PatriciateWard : public CommonWard {
public:
    PatriciateWard();
    PatriciateWard(Model* model, Patch* patch);
    ~PatriciateWard() override = default;

    std::string getLabel() const override { return "Patriciate"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
