#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class Slum : public CommonWard {
public:
    Slum();
    Slum(Model* model, Patch* patch);
    ~Slum() override = default;

    std::string getLabel() const override { return "Slum"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
