#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class Market : public Ward {
public:
    Market() = default;
    Market(Model* model, Patch* patch);
    ~Market() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Market"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
