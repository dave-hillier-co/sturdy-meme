#pragma once

#include <string>
#include <vector>
#include <array>
#include <optional>
#include <glm/glm.hpp>
#include "BiomeGenerator.h" 

namespace VirtualTexture {

// Road types with their associated widths
enum class RoadType : uint8_t {
    Footpath = 0,       // 1.5m wide
    Bridleway = 1,      // 3m wide
    Lane = 2,           // 4m wide
    Road = 3,           // 6m wide
    MainRoad = 4,       // 8m wide
    Count
};

// Get road width in meters for a given road type
inline float getRoadWidth(RoadType type) {
    switch (type) {
        case RoadType::Footpath:  return 1.5f;
        case RoadType::Bridleway: return 3.0f;
        case RoadType::Lane:      return 4.0f;
        case RoadType::Road:      return 6.0f;
        case RoadType::MainRoad:  return 8.0f;
        default:                  return 4.0f;
    }
}

// Material definition for terrain textures
struct TerrainMaterial {
    std::string name;
    std::string albedoPath;
    std::string normalPath;      // Optional - empty if not used
    std::string roughnessPath;   // Optional - empty if not used
    float tilingScale = 1.0f;
    float roughnessValue = 0.8f; // Fallback if no roughness texture

    bool hasNormal() const { return !normalPath.empty(); }
    bool hasRoughness() const { return !roughnessPath.empty(); }
};

// Sub-zone material variation names for each biome zone
struct SubZoneMaterialInfo {
    std::array<std::string, 4> names;
};

// Road material definition
struct RoadMaterial {
    std::string albedoPath;
    std::string normalPath;      // Optional
    float roughnessValue = 0.7f;
    float uvScaleAlong = 1.0f;   // UV scale along road length
    float uvScaleAcross = 1.0f;  // UV scale across road width
};

// Riverbed material definition
struct RiverbedMaterial {
    std::string centerAlbedoPath;   // Center of river (gravel/pebbles)
    std::string edgeAlbedoPath;     // Edge of river (mud/wet sand)
    float widthMultiplier = 1.3f;   // How much wider than water surface
    float roughnessValue = 0.9f;
};

// Configuration for the material library
struct MaterialLibraryConfig {
    std::string basePath;           // Base path for all material assets
    float defaultTilingScale = 4.0f;
    float slopeThreshold = 0.7f;    // Threshold for cliff material
};

// The main material library class
class MaterialLibrary {
public:
    static constexpr size_t NUM_ZONES = static_cast<size_t>(BiomeZone::Count);
    static constexpr size_t NUM_SUB_ZONES = 4;
    static constexpr size_t NUM_ROAD_TYPES = static_cast<size_t>(RoadType::Count);

    MaterialLibrary();
    ~MaterialLibrary() = default;

    // Initialize with configuration
    bool init(const MaterialLibraryConfig& config);

    // Zone material access
    const TerrainMaterial& getZoneMaterial(BiomeZone zone) const;
    const TerrainMaterial& getSubZoneMaterial(BiomeZone zone, BiomeSubZone subZone) const;
    const TerrainMaterial& getSubZoneMaterial(BiomeZone zone, uint8_t subZoneIndex) const;
    const TerrainMaterial& getCliffMaterial() const { return cliffMaterial; }

    // Road material access
    const RoadMaterial& getRoadMaterial(RoadType type) const;

    // Riverbed material access
    const RiverbedMaterial& getRiverbedMaterial() const { return riverbedMaterial; }

    // Get slope threshold for cliff blending
    float getSlopeThreshold() const { return config.slopeThreshold; }

    // Get the base path for material assets
    const std::string& getBasePath() const { return config.basePath; }

    // Get names for debugging
    static const char* getZoneMaterialName(BiomeZone zone);
    static const char* getRoadTypeName(RoadType type);
    static const SubZoneMaterialInfo& getSubZoneInfo(BiomeZone zone);

private:
    void setupDefaultMaterials();
    std::string resolvePath(const std::string& relativePath) const;

    MaterialLibraryConfig config;

    // Primary materials for each zone (indexed by BiomeZone)
    std::array<TerrainMaterial, NUM_ZONES> zoneMaterials;

    // Sub-zone variation materials [zone][subZone]
    std::array<std::array<TerrainMaterial, NUM_SUB_ZONES>, NUM_ZONES> subZoneMaterials;

    // Special materials
    TerrainMaterial cliffMaterial;

    // Road materials (indexed by RoadType)
    std::array<RoadMaterial, NUM_ROAD_TYPES> roadMaterials;

    // Riverbed material
    RiverbedMaterial riverbedMaterial;
};

} // namespace VirtualTexture
