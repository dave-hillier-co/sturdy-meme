#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class Model;
class Patch;

class Park : public Ward {
public:
    Park() = default;
    Park(Model* model, Patch* patch);
    ~Park() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Park"; }
};

} // namespace town_generator
