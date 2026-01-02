#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class MilitaryWard : public Ward {
public:
    MilitaryWard() = default;
    MilitaryWard(Model* model, Patch* patch);
    ~MilitaryWard() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Military"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
