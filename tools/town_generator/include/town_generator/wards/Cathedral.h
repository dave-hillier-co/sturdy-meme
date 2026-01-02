#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class Cathedral : public Ward {
public:
    Cathedral() = default;
    Cathedral(Model* model, Patch* patch);
    ~Cathedral() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Temple"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
