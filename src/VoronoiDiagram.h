#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <cstdint>

// Voronoi diagram generator for procedural town layout
// Uses simple relaxation-based approach suitable for town generation

struct VoronoiCell {
    glm::vec2 site;                    // Cell center (seed point)
    std::vector<glm::vec2> vertices;   // Polygon vertices in CCW order
    std::vector<uint32_t> neighbors;   // Indices of neighboring cells
    float area;                        // Cell area
    bool isBoundary;                   // True if cell touches diagram boundary
};

struct VoronoiEdge {
    glm::vec2 start;
    glm::vec2 end;
    int32_t leftCell;                  // Cell index on left side (-1 if boundary)
    int32_t rightCell;                 // Cell index on right side (-1 if boundary)
};

class VoronoiDiagram {
public:
    VoronoiDiagram() = default;
    ~VoronoiDiagram() = default;

    // Generate Voronoi diagram from random seeds within bounds
    // numCells: approximate number of cells to generate
    // bounds: min/max corners of the rectangular region
    // seed: random seed for reproducibility
    void generate(int numCells, const glm::vec2& boundsMin, const glm::vec2& boundsMax, uint32_t seed);

    // Generate from explicit seed points
    void generateFromSeeds(const std::vector<glm::vec2>& seeds,
                           const glm::vec2& boundsMin, const glm::vec2& boundsMax);

    // Apply Lloyd relaxation to make cells more uniform
    // iterations: number of relaxation passes
    void relax(int iterations = 3);

    // Accessors
    const std::vector<VoronoiCell>& getCells() const { return cells; }
    const std::vector<VoronoiEdge>& getEdges() const { return edges; }
    const glm::vec2& getBoundsMin() const { return boundsMin; }
    const glm::vec2& getBoundsMax() const { return boundsMax; }

    // Find which cell contains a point
    int findCellContaining(const glm::vec2& point) const;

    // Get distance from point to nearest edge
    float distanceToNearestEdge(const glm::vec2& point) const;

    // Check if point is near any edge (for road detection)
    bool isNearEdge(const glm::vec2& point, float threshold) const;

private:
    void computeCellsFromSites();
    void computeEdges();
    void clipCellToBounds(VoronoiCell& cell);
    float computeCellArea(const VoronoiCell& cell) const;
    glm::vec2 computeCellCentroid(const VoronoiCell& cell) const;

    // Hash function for deterministic randomness
    static float hash(glm::vec2 p);
    static glm::vec2 hash2(glm::vec2 p);

    std::vector<glm::vec2> sites;      // Seed points
    std::vector<VoronoiCell> cells;
    std::vector<VoronoiEdge> edges;
    glm::vec2 boundsMin;
    glm::vec2 boundsMax;
};
