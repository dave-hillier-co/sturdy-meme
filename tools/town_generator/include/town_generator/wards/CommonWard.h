#pragma once

#include <town_generator/wards/Ward.h>
#include <string>

namespace town_generator {

class CommonWard : public Ward {
public:
    CommonWard() = default;
    CommonWard(Model* model, Patch* patch);
    CommonWard(Model* model, Patch* patch, float minSq, float gridChaos, float sizeChaos, float emptyProb = 0.04f);
    ~CommonWard() override = default;

    void createGeometry() override;

protected:
    float minSq_ = 0.0f;
    float gridChaos_ = 0.0f;
    float sizeChaos_ = 0.0f;
    float emptyProb_ = 0.04f;
};

} // namespace town_generator
