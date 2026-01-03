/**
 * main.cpp - CLI entry point for Town Generator
 *
 * Generates medieval fantasy city layouts as SVG files.
 * This replaces the OpenFL/Flash UI with command-line arguments.
 */

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <optional>

#include "utils/Random.hpp"
#include "building/Model.hpp"
#include "mapping/Palette.hpp"
#include "mapping/SVGRenderer.hpp"

namespace {

const char* VERSION = "1.0.0";

void printHelp(const char* progName) {
    std::cout << "Town Generator - Medieval Fantasy City Generator\n";
    std::cout << "Usage: " << progName << " [options]\n\n";
    std::cout << "Generation Options:\n";
    std::cout << "  -s, --size <n>        Number of patches (6-40, default: 15)\n";
    std::cout << "  --seed <n>            Random seed (default: current time)\n";
    std::cout << "  --plaza               Force plaza in center\n";
    std::cout << "  --no-plaza            Disable plaza\n";
    std::cout << "  --citadel             Force citadel\n";
    std::cout << "  --no-citadel          Disable citadel\n";
    std::cout << "  --walls               Force city walls\n";
    std::cout << "  --no-walls            Disable city walls\n";
    std::cout << "\n";
    std::cout << "Output Options:\n";
    std::cout << "  -o, --output <file>   Output SVG file (default: stdout)\n";
    std::cout << "  -w, --width <px>      SVG width in pixels (default: 1024)\n";
    std::cout << "  -h, --height <px>     SVG height in pixels (default: 1024)\n";
    std::cout << "  --viewbox             Use viewBox instead of fixed dimensions\n";
    std::cout << "\n";
    std::cout << "Style Options:\n";
    std::cout << "  -p, --palette <name>  Color palette (default: default)\n";
    std::cout << "  --stroke-scale <f>    Stroke width multiplier (default: 1.0)\n";
    std::cout << "\n";
    std::cout << "Info Options:\n";
    std::cout << "  --help                Show this help message\n";
    std::cout << "  --version             Show version\n";
    std::cout << "  --list-palettes       List available palettes\n";
    std::cout << "\n";
    std::cout << "Size Presets:\n";
    std::cout << "  small-town   6-10 patches\n";
    std::cout << "  large-town   10-15 patches\n";
    std::cout << "  small-city   15-24 patches\n";
    std::cout << "  large-city   24-40 patches\n";
    std::cout << "\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " -o city.svg\n";
    std::cout << "  " << progName << " -s 30 --seed 12345 -o large.svg\n";
    std::cout << "  " << progName << " -p blueprint --walls -o blueprint.svg\n";
}

void printVersion() {
    std::cout << "Town Generator v" << VERSION << "\n";
}

struct Options {
    int size = 15;
    int seed = -1;  // -1 means use current time
    std::optional<bool> plaza;
    std::optional<bool> citadel;
    std::optional<bool> walls;
    std::string output;
    float width = 1024.0f;
    float height = 1024.0f;
    bool useViewBox = false;
    std::string paletteName = "default";
    float strokeScale = 1.0f;
};

bool parseArgs(int argc, char* argv[], Options& opts) {
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help") {
            printHelp(argv[0]);
            return false;
        } else if (arg == "--version") {
            printVersion();
            return false;
        } else if (arg == "--list-palettes") {
            std::cout << town::Palette::listPalettes();
            return false;
        } else if (arg == "-s" || arg == "--size") {
            if (++i >= argc) {
                std::cerr << "Error: --size requires a value\n";
                return false;
            }
            std::string sizeArg = argv[i];
            // Handle presets
            if (sizeArg == "small-town") {
                opts.size = 6 + std::rand() % 5;  // 6-10
            } else if (sizeArg == "large-town") {
                opts.size = 10 + std::rand() % 6;  // 10-15
            } else if (sizeArg == "small-city") {
                opts.size = 15 + std::rand() % 10;  // 15-24
            } else if (sizeArg == "large-city") {
                opts.size = 24 + std::rand() % 17;  // 24-40
            } else {
                opts.size = std::atoi(sizeArg.c_str());
                if (opts.size < 6) opts.size = 6;
                if (opts.size > 40) opts.size = 40;
            }
        } else if (arg == "--seed") {
            if (++i >= argc) {
                std::cerr << "Error: --seed requires a value\n";
                return false;
            }
            opts.seed = std::atoi(argv[i]);
        } else if (arg == "--plaza") {
            opts.plaza = true;
        } else if (arg == "--no-plaza") {
            opts.plaza = false;
        } else if (arg == "--citadel") {
            opts.citadel = true;
        } else if (arg == "--no-citadel") {
            opts.citadel = false;
        } else if (arg == "--walls") {
            opts.walls = true;
        } else if (arg == "--no-walls") {
            opts.walls = false;
        } else if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                std::cerr << "Error: --output requires a filename\n";
                return false;
            }
            opts.output = argv[i];
        } else if (arg == "-w" || arg == "--width") {
            if (++i >= argc) {
                std::cerr << "Error: --width requires a value\n";
                return false;
            }
            opts.width = static_cast<float>(std::atof(argv[i]));
        } else if (arg == "-h" || arg == "--height") {
            if (++i >= argc) {
                std::cerr << "Error: --height requires a value\n";
                return false;
            }
            opts.height = static_cast<float>(std::atof(argv[i]));
        } else if (arg == "--viewbox") {
            opts.useViewBox = true;
        } else if (arg == "-p" || arg == "--palette") {
            if (++i >= argc) {
                std::cerr << "Error: --palette requires a name\n";
                return false;
            }
            opts.paletteName = argv[i];
        } else if (arg == "--stroke-scale") {
            if (++i >= argc) {
                std::cerr << "Error: --stroke-scale requires a value\n";
                return false;
            }
            opts.strokeScale = static_cast<float>(std::atof(argv[i]));
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information\n";
            return false;
        }
    }
    return true;
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    // Seed the standard random for size presets
    std::srand(static_cast<unsigned>(std::time(nullptr)));

    Options opts;
    if (!parseArgs(argc, argv, opts)) {
        return (argc > 1 && (std::string(argv[1]) == "--help" ||
                             std::string(argv[1]) == "--version" ||
                             std::string(argv[1]) == "--list-palettes")) ? 0 : 1;
    }

    // Initialize the town generator's random
    if (opts.seed > 0) {
        town::Random::reset(opts.seed);
    } else {
        town::Random::reset();
    }

    try {
        // Create the model with optional overrides
        // Convert optional<bool> to int: nullopt=-1, false=0, true=1
        int plazaFlag = opts.plaza.has_value() ? (opts.plaza.value() ? 1 : 0) : -1;
        int citadelFlag = opts.citadel.has_value() ? (opts.citadel.value() ? 1 : 0) : -1;
        int wallsFlag = opts.walls.has_value() ? (opts.walls.value() ? 1 : 0) : -1;

        auto model = town::Model::create(opts.size, opts.seed,
                                          plazaFlag, citadelFlag, wallsFlag);

        // Get palette
        auto palette = town::Palette::byName(opts.paletteName);

        // Render to SVG
        town::SVGRenderer renderer(model, palette, opts.strokeScale);
        std::string svg = renderer.render(opts.width, opts.height, opts.useViewBox);

        // Output
        if (opts.output.empty()) {
            std::cout << svg;
        } else {
            std::ofstream file(opts.output);
            if (!file) {
                std::cerr << "Error: Cannot open file for writing: " << opts.output << "\n";
                return 1;
            }
            file << svg;
            std::cerr << "Generated: " << opts.output << "\n";
        }

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
