/**
 * Ported from: Source/com/watabou/towngenerator/wards/Park.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */

#pragma once

#include <memory>

#include "../wards/Ward.hpp"
#include "../building/Patch.hpp"
#include "../building/Cutter.hpp"

namespace town {

// Forward declaration
class Model;

class Park : public Ward {
public:
    Park(std::shared_ptr<Model> model, std::shared_ptr<Patch> patch)
        : Ward(model, patch) {}

    void createGeometry() override {
        Polygon block = getCityBlock();
        if (block.compactness() >= 0.7f) {
            geometry = Cutter::radial(block, nullptr, Ward::ALLEY);
        } else {
            geometry = Cutter::semiRadial(block, nullptr, Ward::ALLEY);
        }
    }

    std::string getLabel() const override {
        return "Park";
    }
};

} // namespace town
