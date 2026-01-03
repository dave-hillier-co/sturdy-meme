/**
 * Ported from: Source/com/watabou/towngenerator/wards/Castle.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>
#include <cmath>
#include <algorithm>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"
#include "../building/CurtainWall.hpp"

namespace town {

// Forward declaration
class Model;

class Castle : public Ward {
public:
    std::shared_ptr<CurtainWall> wall;

    Castle(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch);

    void createGeometry() override {
        Polygon block = patch->shape.shrinkEq(Ward::MAIN_STREET * 2.0f);
        geometry = Ward::createOrthoBuilding(block, std::sqrt(block.square()) * 4.0f, 0.6f);
    }

    std::string getLabel() const override {
        return "Castle";
    }
};

} // namespace town
