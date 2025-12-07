#include "MaterialLibrary.h"
#include <SDL3/SDL_log.h>
#include <filesystem>

namespace VirtualTexture {

namespace {

// Sub-zone material information for each biome zone
const std::array<SubZoneMaterialInfo, MaterialLibrary::NUM_ZONES> subZoneInfoTable = {{
    // Sea (not rendered via VT, but included for completeness)
    {{"deep_water", "shallow_water", "sea_foam", "sea_surface"}},
    // Beach
    {{"wet_sand", "pebbles", "driftwood", "seaweed"}},
    // ChalkCliff
    {{"exposed_chalk", "grass_topped", "eroded_chalk", "flint"}},
    // SaltMarsh
    {{"mudflat", "saltpan", "cordgrass", "creek"}},
    // River (handled by spline rasterization, but included)
    {{"river_gravel", "river_stones", "river_sand", "river_mud"}},
    // Wetland
    {{"marsh_grass", "reeds", "muddy", "flooded"}},
    // Grassland (Chalk downs)
    {{"open_down", "wildflower", "gorse", "chalk_scrape"}},
    // Agricultural
    {{"ploughed", "pasture", "crop", "fallow"}},
    // Woodland
    {{"beech_floor", "oak_fern", "clearing", "coppice"}}
}};

// Zone material names
const std::array<const char*, MaterialLibrary::NUM_ZONES> zoneMaterialNames = {{
    "sea",
    "beach_sand",
    "chalk_cliff",
    "salt_marsh",
    "river",
    "wetland",
    "grassland",
    "agricultural",
    "woodland"
}};

// Road type names
const std::array<const char*, MaterialLibrary::NUM_ROAD_TYPES> roadTypeNames = {{
    "footpath",
    "bridleway",
    "lane",
    "road",
    "main_road"
}};

} // anonymous namespace

MaterialLibrary::MaterialLibrary() {
    setupDefaultMaterials();
}

bool MaterialLibrary::init(const MaterialLibraryConfig& cfg) {
    config = cfg;

    // Resolve all paths relative to base path
    for (auto& mat : zoneMaterials) {
        if (!mat.albedoPath.empty())
            mat.albedoPath = resolvePath(mat.albedoPath);
        if (!mat.normalPath.empty())
            mat.normalPath = resolvePath(mat.normalPath);
        if (!mat.roughnessPath.empty())
            mat.roughnessPath = resolvePath(mat.roughnessPath);
    }

    for (auto& zoneSubMats : subZoneMaterials) {
        for (auto& mat : zoneSubMats) {
            if (!mat.albedoPath.empty())
                mat.albedoPath = resolvePath(mat.albedoPath);
            if (!mat.normalPath.empty())
                mat.normalPath = resolvePath(mat.normalPath);
            if (!mat.roughnessPath.empty())
                mat.roughnessPath = resolvePath(mat.roughnessPath);
        }
    }

    if (!cliffMaterial.albedoPath.empty())
        cliffMaterial.albedoPath = resolvePath(cliffMaterial.albedoPath);
    if (!cliffMaterial.normalPath.empty())
        cliffMaterial.normalPath = resolvePath(cliffMaterial.normalPath);

    for (auto& mat : roadMaterials) {
        if (!mat.albedoPath.empty())
            mat.albedoPath = resolvePath(mat.albedoPath);
        if (!mat.normalPath.empty())
            mat.normalPath = resolvePath(mat.normalPath);
    }

    if (!riverbedMaterial.centerAlbedoPath.empty())
        riverbedMaterial.centerAlbedoPath = resolvePath(riverbedMaterial.centerAlbedoPath);
    if (!riverbedMaterial.edgeAlbedoPath.empty())
        riverbedMaterial.edgeAlbedoPath = resolvePath(riverbedMaterial.edgeAlbedoPath);

    SDL_Log("MaterialLibrary initialized with base path: %s", config.basePath.c_str());
    return true;
}

void MaterialLibrary::setupDefaultMaterials() {
    // Sea material (placeholder - sea is rendered differently)
    zoneMaterials[static_cast<size_t>(BiomeZone::Sea)] = {
        "sea",
        "terrain/sea/albedo.png",
        "",
        "",
        1.0f,
        0.3f
    };

    // Beach material - sandy beach
    zoneMaterials[static_cast<size_t>(BiomeZone::Beach)] = {
        "beach_sand",
        "terrain/beach/sand_albedo.png",
        "terrain/beach/sand_normal.png",
        "",
        config.defaultTilingScale,
        0.9f
    };

    // Beach sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::Beach)] = {{
        {"wet_sand", "terrain/beach/wet_sand_albedo.png", "", "", config.defaultTilingScale, 0.7f},
        {"pebbles", "terrain/beach/pebbles_albedo.png", "terrain/beach/pebbles_normal.png", "", config.defaultTilingScale, 0.85f},
        {"driftwood", "terrain/beach/driftwood_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"seaweed", "terrain/beach/seaweed_albedo.png", "", "", config.defaultTilingScale, 0.75f}
    }};

    // Chalk cliff material
    zoneMaterials[static_cast<size_t>(BiomeZone::ChalkCliff)] = {
        "chalk_cliff",
        "terrain/cliff/chalk_albedo.png",
        "terrain/cliff/chalk_normal.png",
        "",
        config.defaultTilingScale,
        0.7f
    };

    // Chalk cliff sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::ChalkCliff)] = {{
        {"exposed_chalk", "terrain/cliff/exposed_chalk_albedo.png", "", "", config.defaultTilingScale, 0.65f},
        {"grass_topped", "terrain/cliff/grass_topped_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"eroded_chalk", "terrain/cliff/eroded_chalk_albedo.png", "terrain/cliff/eroded_chalk_normal.png", "", config.defaultTilingScale, 0.7f},
        {"flint", "terrain/cliff/flint_albedo.png", "terrain/cliff/flint_normal.png", "", config.defaultTilingScale, 0.6f}
    }};

    // Salt marsh material
    zoneMaterials[static_cast<size_t>(BiomeZone::SaltMarsh)] = {
        "salt_marsh",
        "terrain/marsh/muddy_grass_albedo.png",
        "terrain/marsh/muddy_grass_normal.png",
        "",
        config.defaultTilingScale,
        0.85f
    };

    // Salt marsh sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::SaltMarsh)] = {{
        {"mudflat", "terrain/marsh/mudflat_albedo.png", "", "", config.defaultTilingScale, 0.9f},
        {"saltpan", "terrain/marsh/saltpan_albedo.png", "", "", config.defaultTilingScale, 0.75f},
        {"cordgrass", "terrain/marsh/cordgrass_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"creek", "terrain/marsh/creek_albedo.png", "", "", config.defaultTilingScale, 0.7f}
    }};

    // River material (placeholder - handled by spline rasterization)
    zoneMaterials[static_cast<size_t>(BiomeZone::River)] = {
        "river",
        "terrain/river/gravel_albedo.png",
        "terrain/river/gravel_normal.png",
        "",
        config.defaultTilingScale,
        0.85f
    };

    // River sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::River)] = {{
        {"river_gravel", "terrain/river/gravel_albedo.png", "terrain/river/gravel_normal.png", "", config.defaultTilingScale, 0.85f},
        {"river_stones", "terrain/river/stones_albedo.png", "terrain/river/stones_normal.png", "", config.defaultTilingScale, 0.8f},
        {"river_sand", "terrain/river/sand_albedo.png", "", "", config.defaultTilingScale, 0.9f},
        {"river_mud", "terrain/river/mud_albedo.png", "", "", config.defaultTilingScale, 0.95f}
    }};

    // Wetland material
    zoneMaterials[static_cast<size_t>(BiomeZone::Wetland)] = {
        "wetland",
        "terrain/wetland/wet_grass_albedo.png",
        "terrain/wetland/wet_grass_normal.png",
        "",
        config.defaultTilingScale,
        0.85f
    };

    // Wetland sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::Wetland)] = {{
        {"marsh_grass", "terrain/wetland/marsh_grass_albedo.png", "", "", config.defaultTilingScale, 0.85f},
        {"reeds", "terrain/wetland/reeds_albedo.png", "", "", config.defaultTilingScale, 0.75f},
        {"muddy", "terrain/wetland/muddy_albedo.png", "", "", config.defaultTilingScale, 0.95f},
        {"flooded", "terrain/wetland/flooded_albedo.png", "", "", config.defaultTilingScale, 0.5f}
    }};

    // Grassland material (chalk downs)
    zoneMaterials[static_cast<size_t>(BiomeZone::Grassland)] = {
        "grassland",
        "terrain/grassland/chalk_grass_albedo.png",
        "terrain/grassland/chalk_grass_normal.png",
        "",
        config.defaultTilingScale,
        0.8f
    };

    // Grassland sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::Grassland)] = {{
        {"open_down", "terrain/grassland/open_down_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"wildflower", "terrain/grassland/wildflower_albedo.png", "", "", config.defaultTilingScale, 0.75f},
        {"gorse", "terrain/grassland/gorse_albedo.png", "", "", config.defaultTilingScale, 0.7f},
        {"chalk_scrape", "terrain/grassland/chalk_scrape_albedo.png", "", "", config.defaultTilingScale, 0.65f}
    }};

    // Agricultural material
    zoneMaterials[static_cast<size_t>(BiomeZone::Agricultural)] = {
        "agricultural",
        "terrain/agricultural/ploughed_albedo.png",
        "terrain/agricultural/ploughed_normal.png",
        "",
        config.defaultTilingScale,
        0.9f
    };

    // Agricultural sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::Agricultural)] = {{
        {"ploughed", "terrain/agricultural/ploughed_albedo.png", "terrain/agricultural/ploughed_normal.png", "", config.defaultTilingScale, 0.9f},
        {"pasture", "terrain/agricultural/pasture_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"crop", "terrain/agricultural/crop_albedo.png", "", "", config.defaultTilingScale, 0.75f},
        {"fallow", "terrain/agricultural/fallow_albedo.png", "", "", config.defaultTilingScale, 0.85f}
    }};

    // Woodland material
    zoneMaterials[static_cast<size_t>(BiomeZone::Woodland)] = {
        "woodland",
        "terrain/woodland/forest_floor_albedo.png",
        "terrain/woodland/forest_floor_normal.png",
        "",
        config.defaultTilingScale,
        0.85f
    };

    // Woodland sub-zones
    subZoneMaterials[static_cast<size_t>(BiomeZone::Woodland)] = {{
        {"beech_floor", "terrain/woodland/beech_floor_albedo.png", "", "", config.defaultTilingScale, 0.85f},
        {"oak_fern", "terrain/woodland/oak_fern_albedo.png", "", "", config.defaultTilingScale, 0.8f},
        {"clearing", "terrain/woodland/clearing_albedo.png", "", "", config.defaultTilingScale, 0.75f},
        {"coppice", "terrain/woodland/coppice_albedo.png", "", "", config.defaultTilingScale, 0.8f}
    }};

    // Cliff material (for steep slopes)
    cliffMaterial = {
        "cliff",
        "terrain/cliff/rock_albedo.png",
        "terrain/cliff/rock_normal.png",
        "",
        config.defaultTilingScale,
        0.7f
    };

    // Road materials
    roadMaterials[static_cast<size_t>(RoadType::Footpath)] = {
        "roads/footpath_albedo.png",
        "",
        0.85f,
        0.5f,
        1.0f
    };

    roadMaterials[static_cast<size_t>(RoadType::Bridleway)] = {
        "roads/bridleway_albedo.png",
        "roads/bridleway_normal.png",
        0.8f,
        0.5f,
        1.0f
    };

    roadMaterials[static_cast<size_t>(RoadType::Lane)] = {
        "roads/lane_albedo.png",
        "roads/lane_normal.png",
        0.75f,
        1.0f,
        1.0f
    };

    roadMaterials[static_cast<size_t>(RoadType::Road)] = {
        "roads/road_albedo.png",
        "roads/road_normal.png",
        0.7f,
        2.0f,
        1.0f
    };

    roadMaterials[static_cast<size_t>(RoadType::MainRoad)] = {
        "roads/main_road_albedo.png",
        "roads/main_road_normal.png",
        0.65f,
        2.0f,
        1.0f
    };

    // Riverbed material
    riverbedMaterial = {
        "rivers/gravel_albedo.png",
        "rivers/mud_albedo.png",
        1.3f,
        0.9f
    };
}

std::string MaterialLibrary::resolvePath(const std::string& relativePath) const {
    if (config.basePath.empty()) {
        return relativePath;
    }
    std::filesystem::path basePath(config.basePath);
    std::filesystem::path relPath(relativePath);
    return (basePath / relPath).string();
}

const TerrainMaterial& MaterialLibrary::getZoneMaterial(BiomeZone zone) const {
    size_t index = static_cast<size_t>(zone);
    if (index >= NUM_ZONES) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Invalid BiomeZone index: %zu, returning default", index);
        return zoneMaterials[static_cast<size_t>(BiomeZone::Grassland)];
    }
    return zoneMaterials[index];
}

const TerrainMaterial& MaterialLibrary::getSubZoneMaterial(BiomeZone zone, BiomeSubZone subZone) const {
    return getSubZoneMaterial(zone, static_cast<uint8_t>(subZone));
}

const TerrainMaterial& MaterialLibrary::getSubZoneMaterial(BiomeZone zone, uint8_t subZoneIndex) const {
    size_t zoneIdx = static_cast<size_t>(zone);
    if (zoneIdx >= NUM_ZONES) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Invalid BiomeZone index: %zu, returning default", zoneIdx);
        return subZoneMaterials[static_cast<size_t>(BiomeZone::Grassland)][0];
    }
    if (subZoneIndex >= NUM_SUB_ZONES) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Invalid sub-zone index: %u, returning default", subZoneIndex);
        return subZoneMaterials[zoneIdx][0];
    }
    return subZoneMaterials[zoneIdx][subZoneIndex];
}

const RoadMaterial& MaterialLibrary::getRoadMaterial(RoadType type) const {
    size_t index = static_cast<size_t>(type);
    if (index >= NUM_ROAD_TYPES) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "Invalid RoadType index: %zu, returning default", index);
        return roadMaterials[static_cast<size_t>(RoadType::Lane)];
    }
    return roadMaterials[index];
}

const char* MaterialLibrary::getZoneMaterialName(BiomeZone zone) {
    size_t index = static_cast<size_t>(zone);
    if (index >= NUM_ZONES) {
        return "unknown";
    }
    return zoneMaterialNames[index];
}

const char* MaterialLibrary::getRoadTypeName(RoadType type) {
    size_t index = static_cast<size_t>(type);
    if (index >= NUM_ROAD_TYPES) {
        return "unknown";
    }
    return roadTypeNames[index];
}

const SubZoneMaterialInfo& MaterialLibrary::getSubZoneInfo(BiomeZone zone) {
    size_t index = static_cast<size_t>(zone);
    if (index >= NUM_ZONES) {
        return subZoneInfoTable[static_cast<size_t>(BiomeZone::Grassland)];
    }
    return subZoneInfoTable[index];
}

} // namespace VirtualTexture
