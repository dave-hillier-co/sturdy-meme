#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;
class Patch;

class CraftsmenWard : public CommonWard {
public:
    CraftsmenWard();
    CraftsmenWard(Model* model, Patch* patch);
    ~CraftsmenWard() override = default;

    std::string getLabel() const override { return "Craftsmen"; }
};

} // namespace town_generator
