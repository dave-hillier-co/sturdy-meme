#pragma once

#include "core/Mesh.h"
#include <glm/glm.hpp>

namespace GeneratedMeshUtil {

// Append an oriented box to geo. center is world-space; axisX/axisZ span the
// horizontal footprint (half extents included in the axis lengths), halfY is
// the vertical half extent. Vertical faces get UVs; color defaults to white.
inline void appendOrientedBox(MeshGeometry& geo, const glm::vec3& center,
                              const glm::vec3& axisX, const glm::vec3& axisZ,
                              float halfY, const glm::vec4& color = glm::vec4(1.0f)) {
    const glm::vec3 axisY(0.0f, halfY, 0.0f);
    const glm::vec3 nX = glm::normalize(axisX);
    const glm::vec3 nZ = glm::normalize(axisZ);
    const glm::vec3 nY(0.0f, 1.0f, 0.0f);

    struct Face {
        glm::vec3 normal;
        glm::vec3 u, v;   // Half-extent vectors spanning the face
        glm::vec3 offset; // Face center offset from box center
    };
    const Face faces[6] = {
        {nY, axisX, axisZ, axisY},        // top
        {-nY, axisX, -axisZ, -axisY},     // bottom
        {nX, axisZ, axisY, axisX},        // +X side
        {-nX, -axisZ, axisY, -axisX},     // -X side
        {nZ, -axisX, axisY, axisZ},       // +Z side
        {-nZ, axisX, axisY, -axisZ},      // -Z side
    };

    for (const Face& f : faces) {
        uint32_t base = static_cast<uint32_t>(geo.vertices.size());
        const glm::vec2 uvs[4] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
        const glm::vec3 corners[4] = {
            center + f.offset - f.u - f.v,
            center + f.offset + f.u - f.v,
            center + f.offset + f.u + f.v,
            center + f.offset - f.u + f.v,
        };
        glm::vec3 tangent = glm::normalize(f.u);
        for (int i = 0; i < 4; ++i) {
            Vertex v{};
            v.position = corners[i];
            v.normal = f.normal;
            v.texCoord = uvs[i];
            v.tangent = glm::vec4(tangent, 1.0f);
            v.color = color;
            geo.vertices.push_back(v);
        }
        geo.indices.insert(geo.indices.end(),
                           {base, base + 2, base + 1, base, base + 3, base + 2});
    }
}

// Append a horizontal disc (triangle fan) at center, facing up. Used for
// simple lake water surfaces.
inline void appendDisc(MeshGeometry& geo, const glm::vec3& center, float radius,
                       int segments, const glm::vec4& color = glm::vec4(1.0f)) {
    if (segments < 3) segments = 3;
    uint32_t base = static_cast<uint32_t>(geo.vertices.size());

    Vertex centerVert{};
    centerVert.position = center;
    centerVert.normal = glm::vec3(0.0f, 1.0f, 0.0f);
    centerVert.texCoord = glm::vec2(0.5f, 0.5f);
    centerVert.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    centerVert.color = color;
    geo.vertices.push_back(centerVert);

    for (int i = 0; i <= segments; ++i) {
        float angle = static_cast<float>(i) / segments * 6.2831853f;
        glm::vec2 dir(std::cos(angle), std::sin(angle));
        Vertex v{};
        v.position = center + glm::vec3(dir.x * radius, 0.0f, dir.y * radius);
        v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
        v.texCoord = glm::vec2(0.5f) + glm::vec2(dir.x, dir.y) * 0.5f;
        v.tangent = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
        v.color = color;
        geo.vertices.push_back(v);
    }
    for (int i = 0; i < segments; ++i) {
        geo.indices.insert(geo.indices.end(),
                           {base, base + 1 + static_cast<uint32_t>(i) + 1,
                            base + 1 + static_cast<uint32_t>(i)});
    }
}

} // namespace GeneratedMeshUtil
