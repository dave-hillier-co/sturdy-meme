#pragma once

namespace town_generator {

class Circle {
public:
    float x = 0.0f;
    float y = 0.0f;
    float r = 0.0f;

    Circle(float x = 0.0f, float y = 0.0f, float r = 0.0f) : x(x), y(y), r(r) {}
};

} // namespace town_generator
