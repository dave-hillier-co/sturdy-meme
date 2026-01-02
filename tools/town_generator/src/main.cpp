#include <town_generator/building/Model.h>
#include <town_generator/rendering/CityMap.h>
#include <town_generator/rendering/Palette.h>
#include <town_generator/utils/Random.h>

#include <iostream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

void printUsage(const char* program) {
    std::cerr << "Usage: " << program << " [options] <output_dir>\n"
              << "\n"
              << "Options:\n"
              << "  --seed <int>       Random seed (default: random)\n"
              << "  --size <size>      City size: small, medium, large, huge (default: medium)\n"
              << "  --palette <name>   Color palette: default, blueprint, bw, ancient,\n"
              << "                     night, ink, colour, simple (default: default)\n"
              << "  --help             Show this help message\n"
              << "\n"
              << "Examples:\n"
              << "  " << program << " output/\n"
              << "  " << program << " --seed 12345 --size large output/\n"
              << "  " << program << " --palette blueprint --seed 42 output/\n";
}

int getSizePatches(const std::string& size) {
    if (size == "small") return 6;       // Small Town
    if (size == "medium") return 10;     // Large Town
    if (size == "large") return 15;      // Small City
    if (size == "huge") return 24;       // Large City
    if (size == "metropolis") return 40; // Metropolis
    return 10; // Default to medium
}

town_generator::Palette getPalette(const std::string& name) {
    if (name == "blueprint") return town_generator::Palette::BLUEPRINT();
    if (name == "bw") return town_generator::Palette::BW();
    if (name == "ancient") return town_generator::Palette::ANCIENT();
    if (name == "night") return town_generator::Palette::NIGHT();
    if (name == "ink") return town_generator::Palette::INK();
    if (name == "colour") return town_generator::Palette::COLOUR();
    if (name == "simple") return town_generator::Palette::SIMPLE();
    return town_generator::Palette::DEFAULT();
}

int main(int argc, char* argv[]) {
    int seed = -1;
    std::string size = "medium";
    std::string paletteName = "default";
    std::string outputDir;

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            size = argv[++i];
        } else if (arg == "--palette" && i + 1 < argc) {
            paletteName = argv[++i];
        } else if (arg[0] != '-') {
            outputDir = arg;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (outputDir.empty()) {
        std::cerr << "Error: Output directory is required\n\n";
        printUsage(argv[0]);
        return 1;
    }

    // Generate seed if not provided
    if (seed < 0) {
        seed = static_cast<int>(std::time(nullptr));
    }

    // Create output directory if needed
    fs::create_directories(outputDir);

    int nPatches = getSizePatches(size);

    std::cout << "Generating city with:\n"
              << "  Seed: " << seed << "\n"
              << "  Size: " << size << " (" << nPatches << " patches)\n"
              << "  Palette: " << paletteName << "\n"
              << "  Output: " << outputDir << "\n";

    // Set palette
    town_generator::CityMap::palette = getPalette(paletteName);

    // Generate the city
    std::cout << "Generating city model..." << std::endl;
    town_generator::Model model(nPatches, seed);
    std::cout << "Model generated." << std::endl;

    // Render to SVG
    std::cout << "Rendering to SVG..." << std::endl;
    town_generator::CityMap cityMap(&model);

    std::string svgPath = (fs::path(outputDir) / "city.svg").string();
    if (cityMap.saveToFile(svgPath)) {
        std::cout << "Saved: " << svgPath << "\n";
    } else {
        std::cerr << "Error: Failed to save " << svgPath << "\n";
        return 1;
    }

    std::cout << "Done!\n";
    return 0;
}
