#pragma once

#include "RoadSpline.h"
#include "RoadPathfinder.h"
#include <glm/glm.hpp>
#include <vector>
#include <cstdint>
#include <functional>
#include <random>
#include <string>

namespace RoadGen {

// Street types within a settlement (narrower than inter-settlement roads)
enum class StreetType : uint8_t {
    MainStreet = 0,   // 8m wide - entry to center
    Street = 1,       // 5m wide - major branches
    Lane = 2,         // 3.5m wide - infill cross-streets
    Alley = 3         // 2m wide - rear access
};

inline float getStreetWidth(StreetType type) {
    switch (type) {
        case StreetType::MainStreet: return 8.0f;
        case StreetType::Street:     return 5.0f;
        case StreetType::Lane:       return 3.5f;
        case StreetType::Alley:      return 2.0f;
        default:                     return 3.5f;
    }
}

inline const char* getStreetTypeName(StreetType type) {
    switch (type) {
        case StreetType::MainStreet: return "main_street";
        case StreetType::Street:     return "street";
        case StreetType::Lane:       return "lane";
        case StreetType::Alley:      return "alley";
        default:                     return "unknown";
    }
}

// Entry point where external road meets settlement
struct SettlementEntry {
    glm::vec2 position;      // Where road crosses boundary
    glm::vec2 direction;     // Inward direction (toward center)
    RoadType roadType;       // Importance of incoming road
    uint32_t roadId;         // Reference to external road
};

// Key building types that act as attractors
struct KeyBuilding {
    enum class Type : uint8_t {
        Church,
        Market,
        Inn,
        Well,
        Green
    };

    Type type;
    glm::vec2 position;
    float radius;           // Footprint radius for collision
    float attractorWeight;  // Importance for street growth
};

inline const char* getKeyBuildingTypeName(KeyBuilding::Type type) {
    switch (type) {
        case KeyBuilding::Type::Church: return "church";
        case KeyBuilding::Type::Market: return "market";
        case KeyBuilding::Type::Inn:    return "inn";
        case KeyBuilding::Type::Well:   return "well";
        case KeyBuilding::Type::Green:  return "green";
        default:                        return "unknown";
    }
}

// Node in the street skeleton
struct StreetNode {
    uint32_t id;
    glm::vec2 position;
    uint32_t parentId;              // For tree structure (UINT32_MAX = root)
    std::vector<uint32_t> children;
    int depth;                      // Distance from entry
    bool isKeyBuilding = false;
    KeyBuilding::Type buildingType = KeyBuilding::Type::Church;
    bool deleted = false;
};

// Segment connecting two street nodes
struct StreetSegment {
    uint32_t id;
    uint32_t fromNode;
    uint32_t toNode;
    float length;
    StreetType type = StreetType::Street;
    bool isInfill = false;          // Added during block infill phase
    bool deleted = false;
};

// A block (enclosed region between streets)
struct Block {
    uint32_t id;
    std::vector<glm::vec2> boundary;  // CCW polygon
    std::vector<uint32_t> adjacentSegments;
    float area;
    float perimeter;
    bool isExterior;  // Touches settlement boundary
};

// Analysis of whether a block needs subdivision
struct BlockAnalysis {
    bool needsSubdivision;
    glm::vec2 splitStart;
    glm::vec2 splitEnd;
    glm::vec2 splitDirection;
};

// Zone type for lots
enum class LotZone : uint8_t {
    Residential,
    Commercial,
    Religious,
    Civic,
    Agricultural
};

inline const char* getLotZoneName(LotZone zone) {
    switch (zone) {
        case LotZone::Residential:  return "residential";
        case LotZone::Commercial:   return "commercial";
        case LotZone::Religious:    return "religious";
        case LotZone::Civic:        return "civic";
        case LotZone::Agricultural: return "agricultural";
        default:                    return "unknown";
    }
}

// A lot (building plot within a block)
struct Lot {
    uint32_t id;
    std::vector<glm::vec2> boundary;  // CCW polygon
    glm::vec2 frontageStart;
    glm::vec2 frontageEnd;
    float frontageWidth;
    float depth;
    bool isCorner;
    uint32_t adjacentStreetId;
    LotZone zone = LotZone::Residential;
};

// Street frontage edge of a block
struct Frontage {
    glm::vec2 start;
    glm::vec2 end;
    uint32_t streetId;
    float length;
};

// Configuration for skeleton generation (Phase 3)
struct SkeletonConfig {
    float segmentLength = 20.0f;
    float killRadius = 12.0f;
    float attractionRadius = 80.0f;
    int maxBranches = 4;
    float minBranchAngle = 45.0f;   // Degrees
    float maxBranchAngle = 120.0f;  // Degrees
    float maxSlope = 0.15f;
    float slopeCostMultiplier = 3.0f;
    int maxIterations = 100;
};

// Configuration for block infill (Phase 5)
struct InfillConfig {
    float targetBlockWidth = 40.0f;
    float targetBlockDepth = 60.0f;
    float blockSizeVariation = 0.15f;
    float maxBlockPerimeter = 200.0f;
    float maxBlockArea = 3000.0f;
    float minIntersectionAngle = 70.0f;
    float intersectionMergeRadius = 5.0f;
};

// Configuration for lot subdivision (Phase 7)
struct LotConfig {
    float minFrontage = 6.0f;
    float maxFrontage = 15.0f;
    float targetDepth = 35.0f;
    float minDepth = 20.0f;
    float cornerBonus = 1.5f;
    float streetSetback = 2.0f;
    float rearSetback = 3.0f;
};

// Complete configuration for street generation
struct StreetGenConfig {
    SkeletonConfig skeleton;
    InfillConfig infill;
    LotConfig lot;
    uint32_t seed = 12345;
};

// Output of street generation
struct StreetNetwork {
    glm::vec2 center;
    float radius;
    float terrainSize;

    std::vector<StreetNode> nodes;
    std::vector<StreetSegment> segments;
    std::vector<Block> blocks;
    std::vector<Lot> lots;
    std::vector<KeyBuilding> keyBuildings;
    std::vector<SettlementEntry> entries;

    // Helper methods
    uint32_t addNode(glm::vec2 position);
    uint32_t addSegment(uint32_t fromNode, uint32_t toNode, StreetType type, bool isInfill = false);
    StreetNode* findNode(uint32_t id);
    StreetSegment* findSegment(uint32_t fromNode, uint32_t toNode);
    void redirectConnections(uint32_t oldNodeId, uint32_t newNodeId);

    // Statistics
    float getTotalStreetLength() const;
    size_t countByType(StreetType type) const;
};

// Main street generator class
class StreetGenerator {
public:
    using ProgressCallback = std::function<void(float progress, const std::string& status)>;

    StreetGenerator() = default;

    // Initialize with terrain data (reuses RoadPathfinder infrastructure)
    void init(const TerrainData& terrain, float terrainSize);

    // Generate complete street network for a settlement
    bool generate(
        glm::vec2 center,
        float radius,
        SettlementType settlementType,
        const RoadNetwork& externalRoads,
        uint32_t settlementId,
        const StreetGenConfig& config,
        StreetNetwork& outNetwork,
        ProgressCallback callback = nullptr
    );

private:
    TerrainData terrain;
    float terrainSize = 16384.0f;
    std::mt19937 rng;

    // Phase 1: Find entry points
    std::vector<SettlementEntry> findEntryPoints(
        glm::vec2 center,
        float radius,
        const RoadNetwork& externalRoads,
        uint32_t settlementId
    );

    // Phase 2: Place key buildings
    std::vector<KeyBuilding> placeKeyBuildings(
        SettlementType settlementType,
        glm::vec2 center,
        float radius,
        const std::vector<SettlementEntry>& entries
    );

    // Phase 3: Generate organic skeleton
    bool generateSkeleton(
        const std::vector<SettlementEntry>& entries,
        const std::vector<KeyBuilding>& keyBuildings,
        glm::vec2 center,
        float radius,
        const SkeletonConfig& config,
        StreetNetwork& network
    );

    // Phase 4: Identify blocks
    std::vector<Block> identifyBlocks(
        const StreetNetwork& network,
        glm::vec2 center,
        float radius
    );

    // Phase 5: Add infill streets to oversized blocks
    void subdivideBlocks(
        std::vector<Block>& blocks,
        StreetNetwork& network,
        const InfillConfig& config
    );

    // Phase 6: Assign street hierarchy
    void assignHierarchy(
        StreetNetwork& network,
        const std::vector<SettlementEntry>& entries,
        const std::vector<KeyBuilding>& keyBuildings
    );

    // Phase 7: Subdivide blocks into lots
    std::vector<Lot> subdivideLots(
        const std::vector<Block>& blocks,
        const StreetNetwork& network,
        const LotConfig& config
    );

    // Helpers
    glm::vec2 findHighPoint(glm::vec2 center, float radius);
    glm::vec2 avoidCollision(glm::vec2 pos, const std::vector<KeyBuilding>& existing, float minDist);
    float angleBetween(glm::vec2 a, glm::vec2 b);
    std::vector<glm::vec2> createCirclePolygon(glm::vec2 center, float radius, int segments);
    float computePolygonArea(const std::vector<glm::vec2>& polygon);
    float computePolygonPerimeter(const std::vector<glm::vec2>& polygon);
    glm::vec2 computeCentroid(const std::vector<glm::vec2>& polygon);
    bool pointInPolygon(glm::vec2 point, const std::vector<glm::vec2>& polygon);
    std::pair<std::vector<glm::vec2>, std::vector<glm::vec2>> splitPolygon(
        const std::vector<glm::vec2>& polygon,
        glm::vec2 lineStart,
        glm::vec2 lineEnd
    );
};

// Save street network to GeoJSON
bool saveStreetNetworkGeoJson(const std::string& path, const StreetNetwork& network);

// Save lots to GeoJSON
bool saveLotsGeoJson(const std::string& path, const StreetNetwork& network);

// Save debug SVG visualization
bool saveStreetsSVG(const std::string& path, const StreetNetwork& network);

} // namespace RoadGen
