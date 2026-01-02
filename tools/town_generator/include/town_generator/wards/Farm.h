#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class Model;
class Patch;

class Farm : public Ward {
public:
    Farm() = default;
    Farm(Model* model, Patch* patch);
    ~Farm() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Farm"; }
};

} // namespace town_generator
