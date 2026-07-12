#include "KitMeshLoader.h"

#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <SDL3/SDL_log.h>

#include <filesystem>
#include <unordered_map>

namespace KitMeshLoader {

std::vector<KitPart> load(const std::string& path) {
    std::vector<KitPart> parts;

    std::filesystem::path filePath(path);
    if (!std::filesystem::exists(filePath)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "KitMeshLoader: file not found: %s",
                     path.c_str());
        return parts;
    }

    auto data = fastgltf::GltfDataBuffer::FromPath(filePath);
    if (data.error() != fastgltf::Error::None) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "KitMeshLoader: failed to read %s",
                     path.c_str());
        return parts;
    }

    // Load buffers (.bin) but skip external images — textures are provided by
    // the engine's material registry, not the glTF.
    constexpr auto options = fastgltf::Options::LoadExternalBuffers |
                             fastgltf::Options::GenerateMeshIndices;
    fastgltf::Parser parser;
    auto asset = parser.loadGltf(data.get(), filePath.parent_path(), options);
    if (asset.error() != fastgltf::Error::None) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "KitMeshLoader: parse failed for %s (%d)",
                     path.c_str(), static_cast<int>(asset.error()));
        return parts;
    }

    // Accumulate primitives sharing a material name into one part
    std::unordered_map<std::string, size_t> partByMaterial;
    auto partFor = [&](const std::string& matName) -> KitPart& {
        auto it = partByMaterial.find(matName);
        if (it != partByMaterial.end()) return parts[it->second];
        partByMaterial[matName] = parts.size();
        parts.push_back(KitPart{matName, {}, {}});
        return parts.back();
    };

    for (const auto& mesh : asset->meshes) {
        for (const auto& primitive : mesh.primitives) {
            if (primitive.type != fastgltf::PrimitiveType::Triangles) continue;

            auto* positionIt = primitive.findAttribute("POSITION");
            if (positionIt == primitive.attributes.end()) continue;

            std::string matName = "MI_Default";
            if (primitive.materialIndex.has_value()) {
                matName = std::string(asset->materials[primitive.materialIndex.value()].name);
            }
            KitPart& part = partFor(matName);
            const uint32_t vertexOffset = static_cast<uint32_t>(part.vertices.size());

            const auto& posAccessor = asset->accessors[positionIt->accessorIndex];
            const size_t vertexCount = posAccessor.count;
            part.vertices.resize(vertexOffset + vertexCount);

            fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(), posAccessor,
                [&](glm::vec3 p, size_t idx) {
                    part.vertices[vertexOffset + idx].position = p;
                });

            if (auto* it = primitive.findAttribute("NORMAL"); it != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec3>(asset.get(),
                    asset->accessors[it->accessorIndex],
                    [&](glm::vec3 n, size_t idx) {
                        part.vertices[vertexOffset + idx].normal = n;
                    });
            }

            if (auto* it = primitive.findAttribute("TEXCOORD_0"); it != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec2>(asset.get(),
                    asset->accessors[it->accessorIndex],
                    [&](glm::vec2 uv, size_t idx) {
                        part.vertices[vertexOffset + idx].texCoord = uv;
                    });
            }

            if (auto* it = primitive.findAttribute("TANGENT"); it != primitive.attributes.end()) {
                fastgltf::iterateAccessorWithIndex<glm::vec4>(asset.get(),
                    asset->accessors[it->accessorIndex],
                    [&](glm::vec4 t, size_t idx) {
                        part.vertices[vertexOffset + idx].tangent = t;
                    });
            }

            if (!primitive.indicesAccessor.has_value()) continue;
            const auto& idxAccessor = asset->accessors[primitive.indicesAccessor.value()];
            part.indices.reserve(part.indices.size() + idxAccessor.count);
            fastgltf::iterateAccessor<uint32_t>(asset.get(), idxAccessor,
                [&](uint32_t index) {
                    part.indices.push_back(vertexOffset + index);
                });
        }
    }

    return parts;
}

} // namespace KitMeshLoader
