// Dwelling generator tool - procedural building floor plan generation
// Ported from watabou's Dwellings (https://watabou.itch.io/dwellings)

#include "DwellingGenerator.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <filesystem>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <output_dir> [options]\n"
              << "\n"
              << "Generates procedural dwelling floor plans.\n"
              << "Based on watabou's Dwellings (https://watabou.itch.io/dwellings)\n"
              << "\n"
              << "Arguments:\n"
              << "  output_dir         Directory for output files\n"
              << "\n"
              << "Options:\n"
              << "  --seed <value>     Random seed (default: random)\n"
              << "  --count <value>    Number of dwellings to generate (default: 1)\n"
              << "  --size <value>     Building size: small, medium, large (default: medium)\n"
              << "  --floors <value>   Number of floors, 0 = random (default: 0)\n"
              << "  --square           Force rectangular floor plan\n"
              << "  --basement         Include basement\n"
              << "  --spiral           Use spiral staircase\n"
              << "  --stairwell        Use central stairwell\n"
              << "  --help             Show this help message\n"
              << "\n"
              << "Output files:\n"
              << "  dwellings.json     Floor plan data in JSON format\n"
              << "  dwellings.svg      SVG visualization of floor plans\n"
              << "  dwellings.geojson  GeoJSON format for GIS compatibility\n"
              << "\n"
              << "Size ranges:\n"
              << "  small   - 10-16 cells per floor\n"
              << "  medium  - 16-24 cells per floor\n"
              << "  large   - 24-34 cells per floor\n"
              << "\n"
              << "Room types assigned automatically based on:\n"
              << "  - Room size and shape\n"
              << "  - Floor level (ground, upper, basement)\n"
              << "  - Number of doors\n"
              << "  - Position relative to entrance\n"
              << "\n"
              << "Examples:\n"
              << "  " << programName << " ./output --seed 12345\n"
              << "  " << programName << " ./output --size large --floors 3 --basement\n"
              << "  " << programName << " ./output --count 10 --size small\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    dwelling::DwellingConfig config{};
    config.outputDir = argv[1];

    // Parse optional arguments
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--seed" && i + 1 < argc) {
            config.seed = std::stoi(argv[++i]);
        } else if (arg == "--count" && i + 1 < argc) {
            config.count = std::stoi(argv[++i]);
        } else if (arg == "--size" && i + 1 < argc) {
            config.size = argv[++i];
            if (config.size != "small" && config.size != "medium" && config.size != "large") {
                std::cerr << "Invalid size: " << config.size << " (use small, medium, or large)\n";
                return 1;
            }
        } else if (arg == "--floors" && i + 1 < argc) {
            config.numFloors = std::stoi(argv[++i]);
        } else if (arg == "--square") {
            config.square = true;
        } else if (arg == "--basement") {
            config.basement = true;
        } else if (arg == "--spiral") {
            config.spiral = true;
        } else if (arg == "--stairwell") {
            config.stairwell = true;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    // Create output directory if it doesn't exist
    std::filesystem::create_directories(config.outputDir);

    SDL_Log("Dwelling Generator");
    SDL_Log("==================");
    SDL_Log("Output: %s", config.outputDir.c_str());
    SDL_Log("Seed: %d", config.seed);
    SDL_Log("Count: %d", config.count);
    SDL_Log("Size: %s", config.size.c_str());
    if (config.numFloors > 0) {
        SDL_Log("Floors: %d", config.numFloors);
    } else {
        SDL_Log("Floors: random");
    }
    if (config.square) SDL_Log("Shape: square");
    if (config.basement) SDL_Log("Basement: yes");
    if (config.spiral) SDL_Log("Stairs: spiral");
    if (config.stairwell) SDL_Log("Stairs: stairwell");

    dwelling::DwellingGenerator generator;

    SDL_Log("Generating dwellings...");

    bool success = generator.generate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (!success) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Dwelling generation failed!");
        return 1;
    }

    // Save outputs
    std::string jsonPath = config.outputDir + "/dwellings.json";
    std::string svgPath = config.outputDir + "/dwellings.svg";
    std::string geojsonPath = config.outputDir + "/dwellings.geojson";

    if (!generator.saveDwellings(jsonPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save JSON!");
        return 1;
    }

    if (!generator.saveDwellingsSVG(svgPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save SVG!");
        return 1;
    }

    if (!generator.saveDwellingsGeoJSON(geojsonPath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to save GeoJSON!");
        return 1;
    }

    const auto& dwellings = generator.getDwellings();

    SDL_Log("Dwelling generation complete!");
    SDL_Log("Generated %zu dwellings", dwellings.size());

    // Print summary for each dwelling
    for (const auto& dwelling : dwellings) {
        SDL_Log("  %s: %d floors", dwelling->name.c_str(), dwelling->floorCount());
        for (const auto& floor : dwelling->floors) {
            SDL_Log("    Floor %d: %zu rooms, %zu cells",
                    floor->getFloorIndex(),
                    floor->rooms.size(),
                    floor->area.size());
        }
        if (dwelling->basement) {
            SDL_Log("    Basement: %zu rooms, %zu cells",
                    dwelling->basement->rooms.size(),
                    dwelling->basement->area.size());
        }
    }

    SDL_Log("Output files:");
    SDL_Log("  %s", jsonPath.c_str());
    SDL_Log("  %s", svgPath.c_str());
    SDL_Log("  %s", geojsonPath.c_str());

    return 0;
}
