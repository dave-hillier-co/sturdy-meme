#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <array>
#include <vector>

// Shared geometry helpers for building-footprint rings (world-space XZ
// polygons). Used by the settlement blockout prisms and the kit-wall
// assembler so both produce identical band/cap geometry.
namespace footprint {

// Positive when the XZ ring is counter-clockwise in math orientation
// (interior on the left of each directed edge)
float ringSignedArea(const std::vector<glm::vec2>& ring);

// Ear-clip a CCW (positive-area) simple polygon into index triples
std::vector<std::array<size_t, 3>> triangulateRing(const std::vector<glm::vec2>& ring);

// Drop the GeoJSON closing vertex and orient the ring CCW (positive signed
// area). Returns false when the ring is degenerate.
bool normalizeRing(std::vector<glm::vec2>& ring);

// Emit one outward-facing wall quad for the directed edge p0->p1 over
// [baseY, topY]. Outward normal is the right side of the directed edge.
// UVs are world-anchored (0.5/m -> texture repeats every 2m) so tiling is
// continuous along a wall and consistent across all buildings.
void appendEdgeBand(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                    const glm::vec2& p0, const glm::vec2& p1, float baseY, float topY);

// Emit the outward-facing walls of one horizontal band [baseY, topY] of a
// cleaned CCW ring (appendEdgeBand over every edge).
void appendBandWalls(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                     const std::vector<glm::vec2>& ring, float baseY, float topY);

// Emit the outward-facing walls of one band like appendBandWalls, but map V
// to a fixed [vTop, vBottom] range instead of anchoring it to world Y. For
// trim-sheet textures (e.g. T_RockTrim's brick-course strip) that only tile
// horizontally.
void appendBandStrip(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                     const std::vector<glm::vec2>& ring, float baseY, float topY,
                     float vBottom, float vTop);

// Cap a cleaned CCW ring with a flat roof at topY. uvScale is repeats per
// metre (0.5 -> the texture tiles every 2m, matching the wall convention).
void appendRoofCap(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                   const std::vector<glm::vec2>& ring, float topY, float uvScale);

} // namespace footprint
