// Medieval Fantasy City Generator
// Faithful port of watabou's TownGeneratorOS from Haxe to C++
// Command-line tool for procedural city generation

#include <SDL3/SDL.h>
#include <cstring>
#include <ctime>
#include <string>

#include "utils/Random.h"
#include "building/Model.h"
#include "mapping/Palette.h"
#include "SvgExporter.h"

using namespace towngenerator;
using mapping::Palette;

void printUsage(const char* programName) {
    SDL_Log("Medieval Fantasy City Generator");
    SDL_Log("Faithful port of watabou's TownGeneratorOS");
    SDL_Log("");
    SDL_Log("Usage: %s [options]", programName);
    SDL_Log("Options:");
    SDL_Log("  --seed <int>      Random seed (default: current time)");
    SDL_Log("  --patches <int>   Number of patches (default: 15)");
    SDL_Log("  --walls           Enable city walls (default: true)");
    SDL_Log("  --no-walls        Disable city walls");
    SDL_Log("  --citadel         Enable citadel/castle (default: false)");
    SDL_Log("  --plaza           Enable central plaza (default: true)");
    SDL_Log("  --temple          Enable temple (default: true)");
    SDL_Log("  --output <path>   Output SVG file path (default: city.svg)");
    SDL_Log("  --palette <name>  Color palette (default, blueprint, bw, ink, night, ancient, colour, simple)");
    SDL_Log("  --help            Show this usage message");
}

int main(int argc, char* argv[]) {
    // Default parameters
    int seed = static_cast<int>(std::time(nullptr));
    int numPatches = 15;
    bool enableWalls = true;
    bool enableCitadel = false;
    bool enablePlaza = true;
    bool enableTemple = true;
    std::string outputPath = "city.svg";
    std::string paletteName = "default";

    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0) {
            printUsage(argv[0]);
            return 0;
        } else if (std::strcmp(argv[i], "--seed") == 0) {
            if (i + 1 < argc) {
                seed = std::atoi(argv[++i]);
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --seed requires an argument");
                printUsage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--patches") == 0) {
            if (i + 1 < argc) {
                numPatches = std::atoi(argv[++i]);
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --patches requires an argument");
                printUsage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--walls") == 0) {
            enableWalls = true;
        } else if (std::strcmp(argv[i], "--no-walls") == 0) {
            enableWalls = false;
        } else if (std::strcmp(argv[i], "--citadel") == 0) {
            enableCitadel = true;
        } else if (std::strcmp(argv[i], "--plaza") == 0) {
            enablePlaza = true;
        } else if (std::strcmp(argv[i], "--temple") == 0) {
            enableTemple = true;
        } else if (std::strcmp(argv[i], "--output") == 0) {
            if (i + 1 < argc) {
                outputPath = argv[++i];
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --output requires an argument");
                printUsage(argv[0]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--palette") == 0) {
            if (i + 1 < argc) {
                paletteName = argv[++i];
            } else {
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Error: --palette requires an argument");
                printUsage(argv[0]);
                return 1;
            }
        } else {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Unknown argument: %s", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Log parameters
    SDL_Log("Generating medieval city with the following parameters:");
    SDL_Log("  Seed: %d", seed);
    SDL_Log("  Patches: %d", numPatches);
    SDL_Log("  Walls: %s", enableWalls ? "enabled" : "disabled");
    SDL_Log("  Citadel: %s", enableCitadel ? "enabled" : "disabled");
    SDL_Log("  Plaza: %s", enablePlaza ? "enabled" : "disabled");
    SDL_Log("  Temple: %s", enableTemple ? "enabled" : "disabled");
    SDL_Log("  Output: %s", outputPath.c_str());
    SDL_Log("  Palette: %s", paletteName.c_str());

    // Initialize random number generator
    utils::Random::reset(seed);

    // Create model with parameters
    // Note: Model constructor calls generate() automatically
    building::Model model(numPatches, seed);
    // Parameters can be set before construction for full control
    // For now, using defaults from constructor

    SDL_Log("City generation complete");
    SDL_Log("  Patches: %zu", model.patches.size());
    SDL_Log("  Inner patches: %zu", model.inner.size());

    // Select palette
    Palette palette = Palette::DEFAULT();
    if (paletteName == "blueprint") {
        palette = Palette::BLUEPRINT();
    } else if (paletteName == "bw") {
        palette = Palette::BW();
    } else if (paletteName == "ink") {
        palette = Palette::INK();
    } else if (paletteName == "night") {
        palette = Palette::NIGHT();
    } else if (paletteName == "ancient") {
        palette = Palette::ANCIENT();
    } else if (paletteName == "colour") {
        palette = Palette::COLOUR();
    } else if (paletteName == "simple") {
        palette = Palette::SIMPLE();
    } else if (paletteName != "default") {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown palette '%s', using default", paletteName.c_str());
    }

    // Export to SVG
    SvgExporter exporter(model, palette);
    if (exporter.exportToFile(outputPath)) {
        SDL_Log("SVG exported to: %s", outputPath.c_str());
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to export SVG");
        return 1;
    }

    return 0;
}
