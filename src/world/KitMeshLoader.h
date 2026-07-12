#pragma once

#include "core/Mesh.h"

#include <glm/glm.hpp>
#include <string>
#include <vector>

// A contiguous chunk of a modular kit piece sharing one material
// (e.g. "MI_Plaster"). Geometry is in the piece's local space, which for the
// Quaternius Medieval Village kit is metres on a 2 m grid.
struct KitPart {
    std::string materialName;        // glTF material name, e.g. "MI_WoodTrim"
    std::vector<Vertex> vertices;    // Local space (metres)
    std::vector<uint32_t> indices;
};

// Loads a Quaternius kit glTF into per-material parts. Textures are NOT loaded
// here (external images are skipped) — the engine registers the kit materials
// separately by name and looks them up via SceneBuilder::getKitMaterialId().
// Returns an empty vector on failure.
namespace KitMeshLoader {
    std::vector<KitPart> load(const std::string& path);
}
