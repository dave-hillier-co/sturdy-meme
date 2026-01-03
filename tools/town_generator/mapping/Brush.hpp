/**
 * Ported from: Source/com/watabou/towngenerator/mapping/Brush.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

namespace town {

// Stroke width constants from Brush.hx
// These are used for SVG stroke-width attributes
struct Brush {
    static constexpr float NORMAL_STROKE = 0.300f;
    static constexpr float THICK_STROKE  = 1.800f;
    static constexpr float THIN_STROKE   = 0.150f;
};

} // namespace town
