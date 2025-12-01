#include "VoronoiDiagram.h"
#include <algorithm>
#include <cmath>
#include <limits>

float VoronoiDiagram::hash(glm::vec2 p) {
    return glm::fract(std::sin(glm::dot(p, glm::vec2(127.1f, 311.7f))) * 43758.5453f);
}

glm::vec2 VoronoiDiagram::hash2(glm::vec2 p) {
    return glm::vec2(
        hash(p),
        hash(p + glm::vec2(47.0f, 13.0f))
    );
}

void VoronoiDiagram::generate(int numCells, const glm::vec2& min, const glm::vec2& max, uint32_t seed) {
    boundsMin = min;
    boundsMax = max;

    // Generate random seed points using hash function
    sites.clear();
    sites.reserve(numCells);

    glm::vec2 size = max - min;
    glm::vec2 seedOffset(static_cast<float>(seed), static_cast<float>(seed * 7));

    for (int i = 0; i < numCells; ++i) {
        glm::vec2 cellId(static_cast<float>(i), static_cast<float>(i * 13 + seed));
        glm::vec2 jitter = hash2(cellId + seedOffset);
        glm::vec2 point = min + jitter * size;
        sites.push_back(point);
    }

    computeCellsFromSites();
    computeEdges();
}

void VoronoiDiagram::generateFromSeeds(const std::vector<glm::vec2>& seeds,
                                        const glm::vec2& min, const glm::vec2& max) {
    boundsMin = min;
    boundsMax = max;
    sites = seeds;

    computeCellsFromSites();
    computeEdges();
}

void VoronoiDiagram::relax(int iterations) {
    for (int iter = 0; iter < iterations; ++iter) {
        // Move each site to its cell's centroid (Lloyd relaxation)
        for (size_t i = 0; i < sites.size(); ++i) {
            if (!cells[i].isBoundary && cells[i].vertices.size() >= 3) {
                sites[i] = computeCellCentroid(cells[i]);
            }
        }
        computeCellsFromSites();
    }
    computeEdges();
}

void VoronoiDiagram::computeCellsFromSites() {
    cells.clear();
    cells.resize(sites.size());

    // For each site, compute its Voronoi cell using half-plane intersection
    for (size_t i = 0; i < sites.size(); ++i) {
        VoronoiCell& cell = cells[i];
        cell.site = sites[i];
        cell.isBoundary = false;
        cell.neighbors.clear();

        // Start with bounding box as initial polygon
        cell.vertices = {
            boundsMin,
            glm::vec2(boundsMax.x, boundsMin.y),
            boundsMax,
            glm::vec2(boundsMin.x, boundsMax.y)
        };

        // Clip by half-plane for each other site
        for (size_t j = 0; j < sites.size(); ++j) {
            if (i == j) continue;

            // Half-plane: points closer to site[i] than site[j]
            glm::vec2 mid = (sites[i] + sites[j]) * 0.5f;
            glm::vec2 normal = glm::normalize(sites[i] - sites[j]);

            // Clip polygon by this half-plane
            std::vector<glm::vec2> clipped;
            size_t n = cell.vertices.size();

            for (size_t k = 0; k < n; ++k) {
                glm::vec2 curr = cell.vertices[k];
                glm::vec2 next = cell.vertices[(k + 1) % n];

                float currDist = glm::dot(curr - mid, normal);
                float nextDist = glm::dot(next - mid, normal);

                if (currDist >= 0) {
                    clipped.push_back(curr);
                }

                // Check for edge crossing
                if ((currDist >= 0) != (nextDist >= 0)) {
                    // Compute intersection point
                    float t = currDist / (currDist - nextDist);
                    glm::vec2 intersection = curr + t * (next - curr);
                    clipped.push_back(intersection);

                    // This site is a neighbor
                    if (std::find(cell.neighbors.begin(), cell.neighbors.end(), static_cast<uint32_t>(j)) == cell.neighbors.end()) {
                        cell.neighbors.push_back(static_cast<uint32_t>(j));
                    }
                }
            }

            cell.vertices = clipped;

            if (cell.vertices.size() < 3) {
                break; // Degenerate cell
            }
        }

        clipCellToBounds(cell);
        cell.area = computeCellArea(cell);
    }
}

void VoronoiDiagram::clipCellToBounds(VoronoiCell& cell) {
    // Check if any vertex is on the boundary
    const float epsilon = 0.001f;
    for (const auto& v : cell.vertices) {
        if (std::abs(v.x - boundsMin.x) < epsilon ||
            std::abs(v.x - boundsMax.x) < epsilon ||
            std::abs(v.y - boundsMin.y) < epsilon ||
            std::abs(v.y - boundsMax.y) < epsilon) {
            cell.isBoundary = true;
            break;
        }
    }
}

void VoronoiDiagram::computeEdges() {
    edges.clear();

    // Build edges from cell vertices
    for (size_t i = 0; i < cells.size(); ++i) {
        const VoronoiCell& cell = cells[i];
        size_t n = cell.vertices.size();

        for (size_t k = 0; k < n; ++k) {
            glm::vec2 start = cell.vertices[k];
            glm::vec2 end = cell.vertices[(k + 1) % n];

            // Find which neighbor shares this edge
            int32_t rightCell = -1;
            glm::vec2 edgeMid = (start + end) * 0.5f;

            for (uint32_t neighborIdx : cell.neighbors) {
                const VoronoiCell& neighbor = cells[neighborIdx];

                // Check if neighbor contains this edge
                for (size_t m = 0; m < neighbor.vertices.size(); ++m) {
                    glm::vec2 nStart = neighbor.vertices[m];
                    glm::vec2 nEnd = neighbor.vertices[(m + 1) % neighbor.vertices.size()];

                    // Check if edges match (in opposite direction)
                    float d1 = glm::length(start - nEnd) + glm::length(end - nStart);
                    if (d1 < 0.01f) {
                        rightCell = static_cast<int32_t>(neighborIdx);
                        break;
                    }
                }
                if (rightCell >= 0) break;
            }

            // Only add edge if we haven't added it from the other cell
            if (rightCell < 0 || static_cast<size_t>(rightCell) > i) {
                VoronoiEdge edge;
                edge.start = start;
                edge.end = end;
                edge.leftCell = static_cast<int32_t>(i);
                edge.rightCell = rightCell;
                edges.push_back(edge);
            }
        }
    }
}

float VoronoiDiagram::computeCellArea(const VoronoiCell& cell) const {
    if (cell.vertices.size() < 3) return 0.0f;

    // Shoelace formula
    float area = 0.0f;
    size_t n = cell.vertices.size();
    for (size_t i = 0; i < n; ++i) {
        const glm::vec2& curr = cell.vertices[i];
        const glm::vec2& next = cell.vertices[(i + 1) % n];
        area += curr.x * next.y - next.x * curr.y;
    }
    return std::abs(area) * 0.5f;
}

glm::vec2 VoronoiDiagram::computeCellCentroid(const VoronoiCell& cell) const {
    if (cell.vertices.empty()) return cell.site;
    if (cell.vertices.size() < 3) {
        glm::vec2 sum(0.0f);
        for (const auto& v : cell.vertices) sum += v;
        return sum / static_cast<float>(cell.vertices.size());
    }

    // Centroid of polygon
    glm::vec2 centroid(0.0f);
    float signedArea = 0.0f;
    size_t n = cell.vertices.size();

    for (size_t i = 0; i < n; ++i) {
        const glm::vec2& curr = cell.vertices[i];
        const glm::vec2& next = cell.vertices[(i + 1) % n];
        float cross = curr.x * next.y - next.x * curr.y;
        signedArea += cross;
        centroid += (curr + next) * cross;
    }

    signedArea *= 0.5f;
    if (std::abs(signedArea) < 0.0001f) return cell.site;

    centroid /= (6.0f * signedArea);
    return centroid;
}

int VoronoiDiagram::findCellContaining(const glm::vec2& point) const {
    // Simple brute force - find nearest site
    float minDist = std::numeric_limits<float>::max();
    int nearest = -1;

    for (size_t i = 0; i < sites.size(); ++i) {
        float dist = glm::length(point - sites[i]);
        if (dist < minDist) {
            minDist = dist;
            nearest = static_cast<int>(i);
        }
    }

    return nearest;
}

float VoronoiDiagram::distanceToNearestEdge(const glm::vec2& point) const {
    float minDist = std::numeric_limits<float>::max();

    for (const auto& edge : edges) {
        // Distance from point to line segment
        glm::vec2 ab = edge.end - edge.start;
        glm::vec2 ap = point - edge.start;

        float t = glm::clamp(glm::dot(ap, ab) / glm::dot(ab, ab), 0.0f, 1.0f);
        glm::vec2 closest = edge.start + t * ab;
        float dist = glm::length(point - closest);

        minDist = std::min(minDist, dist);
    }

    return minDist;
}

bool VoronoiDiagram::isNearEdge(const glm::vec2& point, float threshold) const {
    return distanceToNearestEdge(point) < threshold;
}
