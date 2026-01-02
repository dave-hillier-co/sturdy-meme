#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class CurtainWall;  // Forward declaration

class Castle : public Ward {
public:
    CurtainWall* wall = nullptr;

    Castle() = default;
    Castle(Model* model, Patch* patch);
    ~Castle() override = default;

    void createGeometry() override;
    std::string getLabel() const override { return "Castle"; }
};

} // namespace town_generator
