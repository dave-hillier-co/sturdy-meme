#pragma once

#include "KitMeshLoader.h"
#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// Assembles modular Quaternius kit pieces into merged, per-material meshes.
// Each stamped piece's geometry is transformed into world space and appended to
// the buffer for its material, so an entire settlement bakes down to a handful
// of draw calls (one per material) rather than one per piece.
class KitBuildingAssembler {
public:
    // modelsDir: directory containing the kit .gltf/.bin files.
    explicit KitBuildingAssembler(std::string modelsDir);

    // Stamp a kit piece (glTF filename without extension, e.g.
    // "Wall_Plaster_Straight") at the given world transform, keeping the
    // piece's native texture coordinates. Non-uniform scale is supported
    // (normals use the inverse-transpose).
    void stamp(const std::string& pieceName, const glm::mat4& transform);

    // Oriented bounding box of a footprint (world XZ), used to fit roofs.
    struct OrientedBox {
        glm::vec2 center{0.0f};
        float width = 0.0f;   // Extent along the box's local X (yaw direction)
        float depth = 0.0f;   // Extent along the box's local Z
        float yaw = 0.0f;     // Rotation of local +X onto the world width axis
    };

    // Trace a normalized CCW footprint ring (world XZ, from
    // footprint::normalizeRing) into a kit building: facade-grammar wall
    // shell (2m bays, styles, aligned windows, door, corner posts), a
    // rock-trim foundation plinth down to plinthBottomY, and a pitched kit
    // roof fitted to `box` (flat tile cap when no roof piece fits). Storey
    // count is round(eavesHeight / 3m), clamped to [1, 6]. buildingSeed
    // drives all per-building style choices.
    void addFootprintShell(const std::vector<glm::vec2>& ccwRing,
                           const OrientedBox& box, float baseY,
                           float eavesHeight, float plinthBottomY,
                           uint32_t buildingSeed);

    // Wall-top height the shell will use for a given eaves height (module
    // quantization applied). Colliders should match this, not eavesHeight.
    static float shellTopHeight(float eavesHeight);

    // Per-material merged geometry, ready for SceneBuilder::addGeneratedMesh.
    struct MaterialMesh {
        std::string materialName;
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
    };
    // Direct access to a material's buffer (for appending plain quads that
    // should merge with kit geometry of the same material).
    MaterialMesh& meshForMaterial(const std::string& materialName);

    // Move the accumulated meshes out, resetting the buffers but keeping the
    // piece cache so subsequent buildings reuse loaded glTFs.
    std::vector<MaterialMesh> takeMaterialMeshes();

    const std::vector<MaterialMesh>& materialMeshes() const { return meshes_; }
    size_t vertexCount() const { return vertexCount_; }
    bool empty() const { return meshes_.empty(); }

    // Load (and cache) a piece without stamping it — lets callers verify the
    // kit assets are usable before committing to kit-built geometry.
    const std::vector<KitPart>& probe(const std::string& pieceName) { return piece(pieceName); }

private:
    const std::vector<KitPart>& piece(const std::string& name);

    // Stamp a piece with its triangles clipped against a world-space plane,
    // keeping the half-space dot(plane.xyz, p) + plane.w <= 0. Used to chop
    // the secondary roof of an L-shaped building at the primary ridge.
    void stampClipped(const std::string& pieceName, const glm::mat4& transform,
                      const glm::vec4& plane);

    // Two-gable roof over a rectilinear L-shaped footprint: one roof per arm,
    // secondary chopped at the primary's ridge plane. Returns false when the
    // ring isn't a rectilinear L or no piece combination fits.
    bool tryLShapedRoof(const std::vector<glm::vec2>& ring, const OrientedBox& box,
                        float topY, bool wantChimney, uint32_t buildingSeed);

    std::string modelsDir_;
    std::unordered_map<std::string, std::vector<KitPart>> cache_;
    std::unordered_map<std::string, size_t> meshIndex_;
    std::vector<MaterialMesh> meshes_;
    size_t vertexCount_ = 0;
};
