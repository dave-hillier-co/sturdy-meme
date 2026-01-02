#pragma once

#include <town_generator/rendering/Palette.h>

namespace town_generator {

class Brush {
public:
    static constexpr float NORMAL_STROKE = 0.300f;
    static constexpr float THICK_STROKE = 1.800f;
    static constexpr float THIN_STROKE = 0.150f;

    uint32_t strokeColor = 0x000000;
    uint32_t fillColor = 0xcccccc;
    float stroke = NORMAL_STROKE;

    Palette palette;

    explicit Brush(const Palette& palette) : palette(palette) {}

    void setFill(uint32_t color) {
        fillColor = color;
    }

    void setStroke(uint32_t color, float strokeWidth = NORMAL_STROKE) {
        strokeColor = color;
        stroke = strokeWidth;
    }

    void setColor(uint32_t fill, uint32_t line = 0xFFFFFFFF, float strokeWidth = NORMAL_STROKE) {
        setFill(fill);
        if (line != 0xFFFFFFFF) {
            setStroke(line, strokeWidth);
        } else {
            setStroke(fill, strokeWidth);
        }
    }

    void noStroke() {
        stroke = 0;
    }
};

} // namespace town_generator
