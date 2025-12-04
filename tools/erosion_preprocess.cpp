// Standalone erosion and sea level preprocessing tool
// Generates flow accumulation, river/lake detection, and water placement data

#include "../src/ErosionSimulator.h"
#include <SDL3/SDL_log.h>
#include <iostream>
#include <string>
#include <cstdlib>

void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " <heightmap.png> <cache_directory> [options]\n"
              << "\n"
              << "Options:\n"
              << "  --num-droplets <value>        Number of water droplets to simulate (default: 500000)\n"
              << "  --max-lifetime <value>        Max steps per droplet (default: 512)\n"
              << "  --output-resolution <value>   Flow map resolution (default: 4096)\n"
              << "  --river-threshold <value>     Min normalized flow to be river [0-1] (default: 0.15)\n"
              << "  --river-min-width <value>     Minimum river width in world units (default: 5.0)\n"
              << "  --river-max-width <value>     Maximum river width in world units (default: 80.0)\n"
              << "  --lake-min-area <value>       Minimum lake area in world units squared (default: 500.0)\n"
              << "  --lake-min-depth <value>      Minimum depression depth for lakes (default: 2.0)\n"
              << "  --sea-level <value>           Height below which is sea (default: 0.0)\n"
              << "  --terrain-size <value>        World size of terrain (default: 16384.0)\n"
              << "  --min-altitude <value>        Min altitude in heightmap (default: 0.0)\n"
              << "  --max-altitude <value>        Max altitude in heightmap (default: 200.0)\n"
              << "  --help                        Show this help message\n"
              << "\n"
              << "Example:\n"
              << "  " << programName << " terrain.png ./terrain_cache --sea-level 23 --terrain-size 16384\n";
}

int main(int argc, char* argv[]) {
    // Check for help flag first
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            printUsage(argv[0]);
            return 0;
        }
    }

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    ErosionConfig config{};
    config.sourceHeightmapPath = argv[1];
    config.cacheDirectory = argv[2];
    config.numDroplets = 500000;
    config.maxDropletLifetime = 512;
    config.inertia = 0.3f;
    config.gravity = 10.0f;
    config.evaporationRate = 0.02f;
    config.minWater = 0.001f;
    config.outputResolution = 4096;
    config.riverFlowThreshold = 0.15f;
    config.riverMinWidth = 5.0f;
    config.riverMaxWidth = 80.0f;
    config.splineSimplifyTolerance = 5.0f;
    config.lakeMinArea = 500.0f;
    config.lakeMinDepth = 2.0f;
    config.seaLevel = 0.0f;
    config.terrainSize = 16384.0f;
    config.minAltitude = 0.0f;
    config.maxAltitude = 200.0f;

    // Parse optional arguments
    for (int i = 3; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--num-droplets" && i + 1 < argc) {
            config.numDroplets = std::stoul(argv[++i]);
        } else if (arg == "--max-lifetime" && i + 1 < argc) {
            config.maxDropletLifetime = std::stoul(argv[++i]);
        } else if (arg == "--output-resolution" && i + 1 < argc) {
            config.outputResolution = std::stoul(argv[++i]);
        } else if (arg == "--river-threshold" && i + 1 < argc) {
            config.riverFlowThreshold = std::stof(argv[++i]);
        } else if (arg == "--river-min-width" && i + 1 < argc) {
            config.riverMinWidth = std::stof(argv[++i]);
        } else if (arg == "--river-max-width" && i + 1 < argc) {
            config.riverMaxWidth = std::stof(argv[++i]);
        } else if (arg == "--lake-min-area" && i + 1 < argc) {
            config.lakeMinArea = std::stof(argv[++i]);
        } else if (arg == "--lake-min-depth" && i + 1 < argc) {
            config.lakeMinDepth = std::stof(argv[++i]);
        } else if (arg == "--sea-level" && i + 1 < argc) {
            config.seaLevel = std::stof(argv[++i]);
        } else if (arg == "--terrain-size" && i + 1 < argc) {
            config.terrainSize = std::stof(argv[++i]);
        } else if (arg == "--min-altitude" && i + 1 < argc) {
            config.minAltitude = std::stof(argv[++i]);
        } else if (arg == "--max-altitude" && i + 1 < argc) {
            config.maxAltitude = std::stof(argv[++i]);
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    SDL_Log("Erosion & Water Placement Preprocessor");
    SDL_Log("======================================");
    SDL_Log("Source: %s", config.sourceHeightmapPath.c_str());
    SDL_Log("Cache: %s", config.cacheDirectory.c_str());
    SDL_Log("Droplets: %u (max lifetime: %u)", config.numDroplets, config.maxDropletLifetime);
    SDL_Log("Output resolution: %u", config.outputResolution);
    SDL_Log("River flow threshold: %.2f", config.riverFlowThreshold);
    SDL_Log("River width: %.1f - %.1f", config.riverMinWidth, config.riverMaxWidth);
    SDL_Log("Lake min area: %.1f, min depth: %.1f", config.lakeMinArea, config.lakeMinDepth);
    SDL_Log("Sea level: %.1f", config.seaLevel);
    SDL_Log("Terrain size: %.1f", config.terrainSize);
    SDL_Log("Altitude range: %.1f to %.1f", config.minAltitude, config.maxAltitude);

    ErosionSimulator simulator;

    SDL_Log("Running erosion simulation...");

    bool success = simulator.simulate(config, [](float progress, const std::string& status) {
        SDL_Log("[%3.0f%%] %s", progress * 100.0f, status.c_str());
    });

    if (success) {
        const auto& waterData = simulator.getWaterData();
        SDL_Log("Simulation complete!");
        SDL_Log("Results:");
        SDL_Log("  Rivers detected: %zu", waterData.rivers.size());
        SDL_Log("  Lakes detected: %zu", waterData.lakes.size());
        SDL_Log("  Sea level: %.1f", waterData.seaLevel);
        SDL_Log("  Flow map: %ux%u", waterData.flowMapWidth, waterData.flowMapHeight);
        SDL_Log("  Max flow value: %.4f", waterData.maxFlowValue);
        SDL_Log("Preview image saved to: %s/erosion_preview.png", config.cacheDirectory.c_str());
        return 0;
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Simulation failed!");
        return 1;
    }
}
