#pragma once

#include <cstdint>
#include <string>

namespace town_generator {

struct Palette {
    uint32_t paper;
    uint32_t light;
    uint32_t medium;
    uint32_t dark;

    Palette(uint32_t paper, uint32_t light, uint32_t medium, uint32_t dark)
        : paper(paper), light(light), medium(medium), dark(dark) {}

    // Convert color to SVG hex format (#RRGGBB)
    static std::string toHex(uint32_t color) {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%06X", color & 0xFFFFFF);
        return std::string(buf);
    }

    // Predefined palettes matching the original
    static Palette DEFAULT() { return Palette(0xccc5b8, 0x99948a, 0x67635c, 0x1a1917); }
    static Palette BLUEPRINT() { return Palette(0x455b8d, 0x7383aa, 0xa1abc6, 0xfcfbff); }
    static Palette BW() { return Palette(0xffffff, 0xcccccc, 0x888888, 0x000000); }
    static Palette INK() { return Palette(0xcccac2, 0x9a979b, 0x6c6974, 0x130f26); }
    static Palette NIGHT() { return Palette(0x000000, 0x402306, 0x674b14, 0x99913d); }
    static Palette ANCIENT() { return Palette(0xccc5a3, 0xa69974, 0x806f4d, 0x342414); }
    static Palette COLOUR() { return Palette(0xfff2c8, 0xd6a36e, 0x869a81, 0x4c5950); }
    static Palette SIMPLE() { return Palette(0xffffff, 0x000000, 0x000000, 0x000000); }
};

} // namespace town_generator
