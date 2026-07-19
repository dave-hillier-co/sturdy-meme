#pragma once

// Road network loader for runtime use
// Loads pre-generated road data from GeoJSON format

#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <cstdint>

// Road types with their associated widths (in meters)
enum class RoadType : uint8_t {
    Footpath = 0,       // 1.5m wide
    Bridleway = 1,      // 3m wide
    Lane = 2,           // 4m wide
    Road = 3,           // 6m wide
    MainRoad = 4        // 8m wide
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

// A single control point along a road spline
struct RoadControlPoint {
    glm::vec2 position;     // World XZ coordinates
    float widthOverride;    // Override width (0 = use default from RoadType)
};

// A road spline connecting two settlements
struct RoadSpline {
    std::vector<RoadControlPoint> controlPoints;
    RoadType type = RoadType::Lane;
    uint32_t fromSettlementId = 0;
    uint32_t toSettlementId = 0;

    // Get width at a control point (uses override if set, else default)
    float getWidthAt(size_t index) const {
        if (index >= controlPoints.size()) return getRoadWidth(type);
        float override = controlPoints[index].widthOverride;
        return override > 0.0f ? override : getRoadWidth(type);
    }
};

// A point where a road crosses a river (from the road generator's crossing
// detection). Fords are baked into the terrain texture; bridges get a deck
// mesh at runtime.
struct WaterCrossing {
    glm::vec2 position;         // Content-space XZ (same space as roads)
    glm::vec2 direction;        // Road direction at the crossing (normalized)
    float span = 0.0f;          // Water span along the road in meters
    bool isBridge = false;      // false = ford
    RoadType roadType = RoadType::Lane;
};

// Collection of all roads in the network
struct RoadNetwork {
    std::vector<RoadSpline> roads;
    std::vector<WaterCrossing> crossings;
    float terrainSize = 16384.0f;
};

// Road network loader - loads pre-generated road data
class RoadNetworkLoader {
public:
    RoadNetworkLoader() = default;
    ~RoadNetworkLoader() = default;

    // Load roads from GeoJSON file
    bool loadFromGeoJson(const std::string& path);

    // Get the loaded road network
    const RoadNetwork& getRoadNetwork() const { return roadNetwork; }
    RoadNetwork& getRoadNetwork() { return roadNetwork; }

    // Check if data is loaded
    bool isLoaded() const { return loaded; }

    // Get standard path for road data
    static std::string getRoadsPath(const std::string& cacheDir);

private:
    RoadNetwork roadNetwork;
    bool loaded = false;
};
