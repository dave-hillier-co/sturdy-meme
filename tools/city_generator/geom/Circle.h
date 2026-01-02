#pragma once

namespace towngenerator {
namespace geom {

struct Circle {
    float x;
    float y;
    float r;

    Circle(float x = 0.0f, float y = 0.0f, float r = 0.0f) : x(x), y(y), r(r) {}
};

} // namespace geom
} // namespace towngenerator
