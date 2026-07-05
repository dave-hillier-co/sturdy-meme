#include "BiomeMap.h"
#include "WorldCoords.h"

#include <SDL3/SDL_log.h>
#include <lodepng.h>
#include <algorithm>

bool BiomeMap::loadFromPng(const std::string& path, float terrainSize) {
    unsigned w = 0, h = 0;
    unsigned error = lodepng::decode(rgba_, w, h, path, LCT_RGBA, 8);
    if (error) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "BiomeMap: Failed to load %s: %s",
                     path.c_str(), lodepng_error_text(error));
        rgba_.clear();
        return false;
    }

    width_ = w;
    height_ = h;
    terrainSize_ = terrainSize;
    SDL_Log("BiomeMap: Loaded %ux%u biome map from %s", w, h, path.c_str());
    return true;
}

size_t BiomeMap::texelIndex(float worldX, float worldZ) const {
    glm::vec2 content = WorldCoords::worldToContent(glm::vec2(worldX, worldZ));
    float u = glm::clamp(content.x / terrainSize_, 0.0f, 1.0f);
    float v = glm::clamp(content.y / terrainSize_, 0.0f, 1.0f);
    uint32_t x = std::min(static_cast<uint32_t>(u * width_), width_ - 1);
    uint32_t y = std::min(static_cast<uint32_t>(v * height_), height_ - 1);
    return (static_cast<size_t>(y) * width_ + x) * 4;
}

BiomeMap::Zone BiomeMap::zoneAtWorld(float worldX, float worldZ) const {
    if (!isLoaded()) return Zone::Unknown;
    uint8_t zone = rgba_[texelIndex(worldX, worldZ) + 0];
    return zone <= static_cast<uint8_t>(Zone::Woodland)
               ? static_cast<Zone>(zone)
               : Zone::Unknown;
}

float BiomeMap::settlementProximityAtWorld(float worldX, float worldZ) const {
    if (!isLoaded()) return 1.0f;
    return rgba_[texelIndex(worldX, worldZ) + 2] / 255.0f;
}
