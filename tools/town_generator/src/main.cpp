#include "town_generator/building/City.h"
#include "town_generator/svg/SVGWriter.h"
#include "town_generator/geojson/GeoJSONWriter.h"
#include "town_generator/utils/Random.h"

#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include <ctime>
#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

void printUsage(const char* programName) {
    SDL_Log("Usage: %s [options] <output.svg>", programName);
    SDL_Log("       %s --settlements <settlements.json> --output-dir <dir>", programName);
    SDL_Log("");
    SDL_Log("Options:");
    SDL_Log("  --seed <int>     Random seed (default: random)");
    SDL_Log("  --size <name>    City size: small, medium, large (default: medium)");
    SDL_Log("  --cells <int>  Number of cells (overrides --size)");
    SDL_Log("  --coast          Generate a coastal city with harbour");
    SDL_Log("  --no-coast       Generate an inland city (no water)");
    SDL_Log("  --geojson <path> Also write a GeoJSON layout (town-local placement)");
    SDL_Log("  --help           Show this help message");
    SDL_Log("");
    SDL_Log("Batch mode (one town per settlement, content-space GeoJSON):");
    SDL_Log("  --settlements <path>  settlements.json from biome_preprocess");
    SDL_Log("  --output-dir <dir>    Output directory for town_<id>.geojson");
    SDL_Log("");
    SDL_Log("Examples:");
    SDL_Log("  %s city.svg", programName);
    SDL_Log("  %s --seed 12345 --size large city.svg", programName);
    SDL_Log("  %s --cells 50 --seed 42 --coast city.svg", programName);
}

// Generate one town per settlement; write content-space GeoJSON layouts that
// the engine and world_preview consume like roads.geojson.
static int runBatchMode(const std::string& settlementsPath, const std::string& outputDir) {
    std::ifstream file(settlementsPath);
    if (!file.is_open()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to open settlements file: %s", settlementsPath.c_str());
        return 1;
    }

    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse %s: %s",
                     settlementsPath.c_str(), e.what());
        return 1;
    }

    int failures = 0;
    size_t generated = 0;

    for (const auto& s : j.value("settlements", nlohmann::json::array())) {
        uint32_t id = s.value("id", 0u);
        std::string type = s.value("type", "Village");
        double x = s.value("x", 0.0);
        double z = s.value("z", 0.0);
        double radius = s.value("radius", 100.0);

        int cells = 10;
        if (type == "Town") cells = 40;
        else if (type == "Village") cells = 18;
        else if (type == "Fishing Village") cells = 12;

        bool coastal = false;
        for (const auto& f : s.value("features", nlohmann::json::array())) {
            if (f.get<std::string>() == "coastal") coastal = true;
        }

        try {
            // Seed derives from the settlement id so layouts are stable
            int seed = static_cast<int>(id) * 31337 + 7;
            town_generator::building::City model(cells, seed);
            model.coastNeeded = coastal;
            model.build();

            auto bounds = town_generator::geojson::GeoJSONWriter::buildingBounds(model);
            double cityRadius = std::max(bounds.width(), bounds.height()) * 0.5;
            if (cityRadius <= 0.0) cityRadius = 1.0;

            // Fit the built-up area into the settlement radius, but never
            // shrink buildings below a realistic size: the median building
            // short side (wall-to-wall width) should come out around 6m.
            // Large layouts grow past the nominal radius instead of
            // compressing; the extent is exported for vegetation suppression.
            double fitScale = radius / cityRadius;
            double medianSize = town_generator::geojson::GeoJSONWriter::medianBuildingSize(model);
            const double targetBuildingSize = 7.0;
            double sizeScale = medianSize > 0.0 ? targetBuildingSize / medianSize : fitScale;

            town_generator::geojson::Placement placement;
            placement.centerX = x;
            placement.centerY = z;
            placement.scale = std::max(fitScale, sizeScale);
            placement.extentRadius = cityRadius * placement.scale;
            placement.settlementId = id;
            placement.settlementType = type;
            placement.seed = seed;

            std::string outPath = outputDir + "/town_" + std::to_string(id) + ".geojson";
            if (!town_generator::geojson::GeoJSONWriter::write(model, placement, outPath)) {
                failures++;
                continue;
            }
            generated++;
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to generate town for settlement %u: %s", id, e.what());
            failures++;
        }
    }

    // Meta file marks batch completion for the build graph
    std::ofstream meta(outputDir + "/towns.meta");
    meta << "settlements=" << settlementsPath << "\n"
         << "generated=" << generated << "\n";

    SDL_Log("Batch town generation: %zu towns written to %s (%d failures)",
            generated, outputDir.c_str(), failures);
    return failures == 0 ? 0 : 1;
}

int main(int argc, char* argv[]) {
    // Default values
    int seed = -1;
    int cells = 30;  // Medium city
    std::string outputFile;
    std::string geojsonFile;
    std::string settlementsPath;
    std::string outputDir;
    int coastOverride = -1;  // -1 = use random, 0 = no coast, 1 = force coast

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--settlements") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --settlements requires a value");
                return 1;
            }
            settlementsPath = argv[++i];
        } else if (arg == "--output-dir") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --output-dir requires a value");
                return 1;
            }
            outputDir = argv[++i];
        } else if (arg == "--geojson") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --geojson requires a value");
                return 1;
            }
            geojsonFile = argv[++i];
        } else if (arg == "--seed") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --seed requires a value");
                return 1;
            }
            seed = std::atoi(argv[++i]);
        } else if (arg == "--size") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --size requires a value");
                return 1;
            }
            std::string size = argv[++i];
            if (size == "small") {
                cells = 15;
            } else if (size == "medium") {
                cells = 30;
            } else if (size == "large") {
                cells = 60;
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown size '%s'. Use small, medium, or large.", size.c_str());
                return 1;
            }
        } else if (arg == "--cells") {
            if (i + 1 >= argc) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --cells requires a value");
                return 1;
            }
            cells = std::atoi(argv[++i]);
            if (cells < 5 || cells > 200) {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: cells must be between 5 and 200");
                return 1;
            }
        } else if (arg == "--coast") {
            coastOverride = 1;
        } else if (arg == "--no-coast") {
            coastOverride = 0;
        } else if (arg[0] == '-') {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Unknown option '%s'", arg.c_str());
            printUsage(argv[0]);
            return 1;
        } else {
            outputFile = arg;
        }
    }

    // Batch mode: one town per settlement
    if (!settlementsPath.empty()) {
        if (outputDir.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --settlements requires --output-dir");
            return 1;
        }
        return runBatchMode(settlementsPath, outputDir);
    }

    if (outputFile.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: No output file specified");
        printUsage(argv[0]);
        return 1;
    }

    // Generate seed if not provided
    if (seed == -1) {
        seed = static_cast<int>(std::time(nullptr));
    }

    SDL_Log("Generating city with %d cells, seed %d", cells, seed);

    // Generate the city
    try {
        town_generator::building::City model(cells, seed);

        // Apply coast override if specified
        if (coastOverride == 1) {
            model.coastNeeded = true;
        } else if (coastOverride == 0) {
            model.coastNeeded = false;
        }

        model.build();

        // Write SVG output
        if (town_generator::svg::SVGWriter::write(model, outputFile)) {
            SDL_Log("City generated successfully: %s", outputFile.c_str());
            SDL_Log("Seed: %d (use this seed to regenerate the same city)", seed);
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: Failed to write output file '%s'", outputFile.c_str());
            return 1;
        }

        // Optional GeoJSON output (town-local placement: origin center, 1:1 scale)
        if (!geojsonFile.empty()) {
            town_generator::geojson::Placement placement;
            placement.scale = 1.0;
            placement.seed = seed;
            if (!town_generator::geojson::GeoJSONWriter::write(model, placement, geojsonFile)) {
                return 1;
            }
        }
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error generating city: %s", e.what());
        return 1;
    }

    return 0;
}
