#pragma once

#include <town_generator/wards/CommonWard.h>
#include <string>

namespace town_generator {

class Model;  // Forward declaration
class Patch;  // Forward declaration

class MerchantWard : public CommonWard {
public:
    MerchantWard();
    MerchantWard(Model* model, Patch* patch);
    ~MerchantWard() override = default;

    std::string getLabel() const override { return "Merchant"; }

    static float rateLocation(Model* model, Patch* patch);
};

} // namespace town_generator
