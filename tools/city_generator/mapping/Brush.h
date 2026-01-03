#pragma once

#include "Palette.h"
#include <cstdint>
#include <cstdio>
#include <string>

namespace towngenerator {
namespace mapping {

/**
 * Brush - Stroke and fill style management for SVG rendering
 * Ported from Haxe Brush.hx
 *
 * Manages stroke widths, colors, and fill settings for consistent rendering
 */
class Brush {
public:
    // Stroke width constants (matching Haxe originals)
    static constexpr float NORMAL_STROKE = 0.300f;
    static constexpr float THICK_STROKE = 1.800f;
    static constexpr float THIN_STROKE = 0.150f;

    // Current style state
    uint32_t strokeColor = 0x000000;
    uint32_t fillColor = 0xcccccc;
    float stroke = NORMAL_STROKE;

    // Reference to current palette
    const Palette& palette;

    explicit Brush(const Palette& palette)
        : palette(palette)
    {}

    /**
     * Set fill color
     */
    void setFill(uint32_t color) {
        fillColor = color;
    }

    /**
     * Set stroke style
     * @param color Stroke color (-1 for same as fill)
     * @param strokeWidth Stroke width
     * @param miter Use miter join (vs round)
     */
    void setStroke(int32_t color, float strokeWidth = NORMAL_STROKE, bool miter = true) {
        if (strokeWidth == 0) {
            stroke = 0;
        } else {
            strokeColor = (color == -1) ? fillColor : static_cast<uint32_t>(color);
            stroke = strokeWidth;
        }
    }

    /**
     * Disable stroke
     */
    void noStroke() {
        stroke = 0;
    }

    /**
     * Set both fill and stroke at once
     */
    void setColor(uint32_t fill, int32_t line = -1,
                  float strokeWidth = NORMAL_STROKE, bool miter = true) {
        setFill(fill);
        setStroke(line, strokeWidth, miter);
    }

    /**
     * Convert color to SVG hex string (e.g., "#ff0000")
     */
    static std::string colorToSvg(uint32_t color) {
        char buf[8];
        std::snprintf(buf, sizeof(buf), "#%06x", color & 0xFFFFFF);
        return std::string(buf);
    }
};

} // namespace mapping
} // namespace towngenerator
