#pragma once

#include <cstdint>

namespace towngenerator {
namespace mapping {

/**
 * Palette - Color scheme for city map rendering
 * Ported from Haxe Palette.hx
 *
 * Contains four colors: paper (background), light, medium, dark
 */
struct Palette {
    uint32_t paper;   // Background/paper color
    uint32_t light;   // Light accent (buildings, roads)
    uint32_t medium;  // Medium accent (streets, parks)
    uint32_t dark;    // Dark accent (walls, outlines)

    constexpr Palette(uint32_t paper, uint32_t light, uint32_t medium, uint32_t dark)
        : paper(paper), light(light), medium(medium), dark(dark) {}

    // Predefined palettes (matching Haxe originals)
    static constexpr Palette DEFAULT()   { return Palette(0xccc5b8, 0x99948a, 0x67635c, 0x1a1917); }
    static constexpr Palette BLUEPRINT() { return Palette(0x455b8d, 0x7383aa, 0xa1abc6, 0xfcfbff); }
    static constexpr Palette BW()        { return Palette(0xffffff, 0xcccccc, 0x888888, 0x000000); }
    static constexpr Palette INK()       { return Palette(0xcccac2, 0x9a979b, 0x6c6974, 0x130f26); }
    static constexpr Palette NIGHT()     { return Palette(0x000000, 0x402306, 0x674b14, 0x99913d); }
    static constexpr Palette ANCIENT()   { return Palette(0xccc5a3, 0xa69974, 0x806f4d, 0x342414); }
    static constexpr Palette COLOUR()    { return Palette(0xfff2c8, 0xd6a36e, 0x869a81, 0x4c5950); }
    static constexpr Palette SIMPLE()    { return Palette(0xffffff, 0x000000, 0x000000, 0x000000); }

    // Comparison operators
    bool operator==(const Palette& other) const {
        return paper == other.paper && light == other.light &&
               medium == other.medium && dark == other.dark;
    }

    bool operator!=(const Palette& other) const {
        return !(*this == other);
    }
};

} // namespace mapping
} // namespace towngenerator
