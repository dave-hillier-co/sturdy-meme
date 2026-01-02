#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;
class Patch;

class GateWard : public CommonWard {
public:
    GateWard();
    GateWard(Model* model, Patch* patch);
    ~GateWard() override = default;

    std::string getLabel() const override { return "Gate"; }
};

} // namespace town_generator
