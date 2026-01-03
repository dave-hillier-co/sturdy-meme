/**
 * Ported from: Source/com/watabou/towngenerator/mapping/Palette.hx
 *
 * This is a direct port of the original Haxe code. The goal is to preserve
 * the original structure and algorithms as closely as possible. Do NOT "fix"
 * issues by changing how the code works - fix root causes instead.
 */
#pragma once

#include <cstdint>
#include <string>
#include <sstream>
#include <iomanip>
#include <map>

namespace town {

class Palette {
public:
    uint32_t paper;
    uint32_t light;
    uint32_t medium;
    uint32_t dark;

    Palette(uint32_t paper, uint32_t light, uint32_t medium, uint32_t dark)
        : paper(paper), light(light), medium(medium), dark(dark) {}

    // Convert color to SVG hex format (#RRGGBB)
    static std::string toHex(uint32_t color) {
        std::ostringstream oss;
        oss << "#" << std::hex << std::setfill('0') << std::setw(6) << (color & 0xFFFFFF);
        return oss.str();
    }

    std::string paperHex() const { return toHex(paper); }
    std::string lightHex() const { return toHex(light); }
    std::string mediumHex() const { return toHex(medium); }
    std::string darkHex() const { return toHex(dark); }

    // Predefined palettes (matching Palette.hx exactly)
    static Palette DEFAULT()   { return Palette(0xccc5b8, 0x99948a, 0x67635c, 0x1a1917); }
    static Palette BLUEPRINT() { return Palette(0x455b8d, 0x7383aa, 0xa1abc6, 0xfcfbff); }
    static Palette BW()        { return Palette(0xffffff, 0xcccccc, 0x888888, 0x000000); }
    static Palette INK()       { return Palette(0xcccac2, 0x9a979b, 0x6c6974, 0x130f26); }
    static Palette NIGHT()     { return Palette(0x000000, 0x402306, 0x674b14, 0x99913d); }
    static Palette ANCIENT()   { return Palette(0xccc5a3, 0xa69974, 0x806f4d, 0x342414); }
    static Palette COLOUR()    { return Palette(0xfff2c8, 0xd6a36e, 0x869a81, 0x4c5950); }
    static Palette SIMPLE()    { return Palette(0xffffff, 0x000000, 0x000000, 0x000000); }

    // Get palette by name (case-insensitive)
    static Palette byName(const std::string& name) {
        std::string lower = name;
        for (auto& c : lower) c = std::tolower(c);

        if (lower == "default")   return DEFAULT();
        if (lower == "blueprint") return BLUEPRINT();
        if (lower == "bw")        return BW();
        if (lower == "ink")       return INK();
        if (lower == "night")     return NIGHT();
        if (lower == "ancient")   return ANCIENT();
        if (lower == "colour")    return COLOUR();
        if (lower == "simple")    return SIMPLE();

        return DEFAULT(); // fallback
    }

    // List all palette names
    static std::string listPalettes() {
        return "Available palettes:\n"
               "  default   - Warm parchment tones (paper=#ccc5b8)\n"
               "  blueprint - Blue technical drawing (paper=#455b8d)\n"
               "  bw        - Black and white (paper=#ffffff)\n"
               "  ink       - Dark ink on aged paper (paper=#cccac2)\n"
               "  night     - Dark night view (paper=#000000)\n"
               "  ancient   - Aged map style (paper=#ccc5a3)\n"
               "  colour    - Colorful medieval (paper=#fff2c8)\n"
               "  simple    - Simple black on white (paper=#ffffff)\n";
    }
};

} // namespace town
