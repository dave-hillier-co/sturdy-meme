#pragma once

#include "Mesh.h"
#include "TownGenerator.h"
#include <glm/glm.hpp>
#include <vector>

// Generates procedural meshes for medieval-style buildings
class BuildingMeshGenerator {
public:
    BuildingMeshGenerator() = default;
    ~BuildingMeshGenerator() = default;

    // Generate mesh for a building type
    // Returns vertices and indices that can be used with Mesh::setCustomGeometry
    void generateBuilding(BuildingType type, const glm::vec3& dimensions, float randomSeed,
                          std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

    // Generate a simple road segment mesh (flat quad along path)
    void generateRoadSegment(const glm::vec3& start, const glm::vec3& end, float width,
                             std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices);

private:
    // Building component generators
    void generateBox(const glm::vec3& min, const glm::vec3& max, float uvScale,
                     std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generatePeakedRoof(const glm::vec3& baseMin, const glm::vec3& baseMax, float peakHeight,
                            float overhang, std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateHippedRoof(const glm::vec3& baseMin, const glm::vec3& baseMax, float peakHeight,
                            float overhang, std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateCylindricalBase(const glm::vec3& center, float radius, float height, int segments,
                                 std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateConicalRoof(const glm::vec3& center, float baseRadius, float height, int segments,
                             std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    // Specific building generators
    void generateSmallHouse(const glm::vec3& dims, float seed,
                            std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateMediumHouse(const glm::vec3& dims, float seed,
                             std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateTavern(const glm::vec3& dims, float seed,
                        std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateWorkshop(const glm::vec3& dims, float seed,
                          std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateChurch(const glm::vec3& dims, float seed,
                        std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateWatchTower(const glm::vec3& dims, float seed,
                            std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateWell(const glm::vec3& dims, float seed,
                      std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateMarket(const glm::vec3& dims, float seed,
                        std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateBarn(const glm::vec3& dims, float seed,
                      std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void generateWindmill(const glm::vec3& dims, float seed,
                          std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    // Utility
    void addQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                 const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                 std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    void addTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                     const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
                     std::vector<Vertex>& verts, std::vector<uint32_t>& inds);

    glm::vec3 computeNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2);

    float hash(float seed, float offset) const;
};
