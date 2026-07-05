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
    // Albedo sources. The dedicated terrain/* art set was never added to the
    // repo, so the megatexture baked flat gray (sampleMaterialTriplanar's
    // missing-texture fallback). Point each biome at a real 512x512 PNG that
    // ships under assets/textures/downloads/ instead. Paths are relative to the
    // --materials base path (assets/textures). Only albedoPath is sampled by the
    // tile compositor; normal/roughness maps are left empty here.
    const std::string GRASS   = "downloads/Grass001_2K-JPG/Grass001.png";
    const std::string DIRT    = "downloads/Ground037_2K-JPG/Ground037.png";
    const std::string FOREST  = "downloads/Ground048_2K-JPG/Ground048.png";
    const std::string MUD     = "downloads/Ground042_2K-JPG/Ground042.png";
    const std::string MARSH   = "downloads/Ground054_2K-JPG/Ground054.png";
    const std::string SAND    = "downloads/Ground078_2K-JPG/Ground078.png";
    const std::string GRAVEL  = "downloads/Gravel022_2K-JPG/Gravel022.png";
    const std::string CHALK   = "downloads/Rock027_2K-JPG/Rock027.png";
    const std::string ROCK    = "downloads/Rock030_2K-JPG/Rock030.png";
    const std::string PAVING  = "downloads/PavingStones070_2K-JPG/PavingStones070.png";

    const float ts = config.defaultTilingScale;

    // Helper-free assignment: {name, albedo, normal, roughnessMap, tiling, roughness}
    // Sea (not rendered via VT, but kept for completeness)
    zoneMaterials[static_cast<size_t>(BiomeZone::Sea)]          = {"sea", MARSH, "", "", 1.0f, 0.3f};
    zoneMaterials[static_cast<size_t>(BiomeZone::Beach)]        = {"beach_sand", SAND, "", "", ts, 0.9f};
    zoneMaterials[static_cast<size_t>(BiomeZone::ChalkCliff)]   = {"chalk_cliff", CHALK, "", "", ts, 0.7f};
    zoneMaterials[static_cast<size_t>(BiomeZone::SaltMarsh)]    = {"salt_marsh", MARSH, "", "", ts, 0.85f};
    zoneMaterials[static_cast<size_t>(BiomeZone::River)]        = {"river", GRAVEL, "", "", ts, 0.85f};
    zoneMaterials[static_cast<size_t>(BiomeZone::Wetland)]      = {"wetland", MUD, "", "", ts, 0.85f};
    zoneMaterials[static_cast<size_t>(BiomeZone::Grassland)]    = {"grassland", GRASS, "", "", ts, 0.8f};
    zoneMaterials[static_cast<size_t>(BiomeZone::Agricultural)] = {"agricultural", DIRT, "", "", ts, 0.9f};
    zoneMaterials[static_cast<size_t>(BiomeZone::Woodland)]     = {"woodland", FOREST, "", "", ts, 0.85f};

    // Sub-zones add roughness variety; albedo follows the parent biome so the
    // megatexture reads as coherent ground rather than four clashing textures.
    auto fillSubZones = [&](BiomeZone zone, const std::string& albedo,
                            float r0, float r1, float r2, float r3) {
        auto& sub = subZoneMaterials[static_cast<size_t>(zone)];
        sub[0] = {"sub0", albedo, "", "", ts, r0};
        sub[1] = {"sub1", albedo, "", "", ts, r1};
        sub[2] = {"sub2", albedo, "", "", ts, r2};
        sub[3] = {"sub3", albedo, "", "", ts, r3};
    };

    fillSubZones(BiomeZone::Sea,          MARSH,  0.3f, 0.3f, 0.3f, 0.3f);
    fillSubZones(BiomeZone::Beach,        SAND,   0.7f, 0.85f, 0.8f, 0.75f);
    fillSubZones(BiomeZone::ChalkCliff,   CHALK,  0.65f, 0.8f, 0.7f, 0.6f);
    fillSubZones(BiomeZone::SaltMarsh,    MARSH,  0.9f, 0.75f, 0.8f, 0.7f);
    fillSubZones(BiomeZone::River,        GRAVEL, 0.85f, 0.8f, 0.9f, 0.95f);
    fillSubZones(BiomeZone::Wetland,      MUD,    0.85f, 0.75f, 0.95f, 0.5f);
    fillSubZones(BiomeZone::Grassland,    GRASS,  0.8f, 0.75f, 0.7f, 0.65f);
    fillSubZones(BiomeZone::Agricultural, DIRT,   0.9f, 0.8f, 0.75f, 0.85f);
    fillSubZones(BiomeZone::Woodland,     FOREST, 0.85f, 0.8f, 0.75f, 0.8f);

    // Cliff material (for steep slopes)
    cliffMaterial = {"cliff", ROCK, "", "", ts, 0.7f};

    // Road materials: {albedoPath, normalPath, roughness, tilingScale, opacity}
    roadMaterials[static_cast<size_t>(RoadType::Footpath)]  = {GRAVEL, "", 0.85f, 0.5f, 1.0f};
    roadMaterials[static_cast<size_t>(RoadType::Bridleway)] = {GRAVEL, "", 0.8f, 0.5f, 1.0f};
    roadMaterials[static_cast<size_t>(RoadType::Lane)]      = {GRAVEL, "", 0.75f, 1.0f, 1.0f};
    roadMaterials[static_cast<size_t>(RoadType::Road)]      = {PAVING, "", 0.7f, 2.0f, 1.0f};
    roadMaterials[static_cast<size_t>(RoadType::MainRoad)]  = {PAVING, "", 0.65f, 2.0f, 1.0f};

    // Riverbed material: {centerAlbedoPath, edgeAlbedoPath, tilingScale, opacity}
    riverbedMaterial = {GRAVEL, MUD, 1.3f, 0.9f};
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
