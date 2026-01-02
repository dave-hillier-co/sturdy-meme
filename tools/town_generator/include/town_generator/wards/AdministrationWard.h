#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class AdministrationWard : public CommonWard {
public:
    AdministrationWard();
    AdministrationWard(Model* model, Patch* patch);
    ~AdministrationWard() override = default;

    std::string getLabel() const override { return "Administration"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
