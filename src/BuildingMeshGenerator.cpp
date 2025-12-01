#include "BuildingMeshGenerator.h"
#include <cmath>

float BuildingMeshGenerator::hash(float seed, float offset) const {
    return glm::fract(std::sin(seed + offset) * 43758.5453f);
}

glm::vec3 BuildingMeshGenerator::computeNormal(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2) {
    glm::vec3 e1 = p1 - p0;
    glm::vec3 e2 = p2 - p0;
    return glm::normalize(glm::cross(e1, e2));
}

void BuildingMeshGenerator::addQuad(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3,
                                     const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2, const glm::vec2& uv3,
                                     std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    glm::vec3 normal = computeNormal(p0, p1, p2);

    // Calculate tangent from UV direction
    glm::vec3 edge1 = p1 - p0;
    glm::vec3 edge2 = p2 - p0;
    glm::vec2 dUV1 = uv1 - uv0;
    glm::vec2 dUV2 = uv2 - uv0;

    float f = 1.0f / (dUV1.x * dUV2.y - dUV2.x * dUV1.y + 0.0001f);
    glm::vec3 tangent = glm::normalize(f * (dUV2.y * edge1 - dUV1.y * edge2));

    Vertex v0 = {p0, normal, uv0, glm::vec4(tangent, 1.0f)};
    Vertex v1 = {p1, normal, uv1, glm::vec4(tangent, 1.0f)};
    Vertex v2 = {p2, normal, uv2, glm::vec4(tangent, 1.0f)};
    Vertex v3 = {p3, normal, uv3, glm::vec4(tangent, 1.0f)};

    verts.push_back(v0);
    verts.push_back(v1);
    verts.push_back(v2);
    verts.push_back(v3);

    // Two triangles: 0-1-2 and 0-2-3
    inds.push_back(base + 0);
    inds.push_back(base + 1);
    inds.push_back(base + 2);
    inds.push_back(base + 0);
    inds.push_back(base + 2);
    inds.push_back(base + 3);
}

void BuildingMeshGenerator::addTriangle(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2,
                                         const glm::vec2& uv0, const glm::vec2& uv1, const glm::vec2& uv2,
                                         std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    uint32_t base = static_cast<uint32_t>(verts.size());
    glm::vec3 normal = computeNormal(p0, p1, p2);

    glm::vec3 edge1 = p1 - p0;
    glm::vec2 dUV1 = uv1 - uv0;
    glm::vec3 tangent = glm::normalize(edge1);

    Vertex v0 = {p0, normal, uv0, glm::vec4(tangent, 1.0f)};
    Vertex v1 = {p1, normal, uv1, glm::vec4(tangent, 1.0f)};
    Vertex v2 = {p2, normal, uv2, glm::vec4(tangent, 1.0f)};

    verts.push_back(v0);
    verts.push_back(v1);
    verts.push_back(v2);

    inds.push_back(base + 0);
    inds.push_back(base + 1);
    inds.push_back(base + 2);
}

void BuildingMeshGenerator::generateBox(const glm::vec3& min, const glm::vec3& max, float uvScale,
                                         std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    glm::vec3 size = max - min;

    // Front face (Z+)
    addQuad(
        glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z),
        glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z),
        glm::vec2(0, 0), glm::vec2(size.x * uvScale, 0),
        glm::vec2(size.x * uvScale, size.y * uvScale), glm::vec2(0, size.y * uvScale),
        verts, inds);

    // Back face (Z-)
    addQuad(
        glm::vec3(max.x, min.y, min.z), glm::vec3(min.x, min.y, min.z),
        glm::vec3(min.x, max.y, min.z), glm::vec3(max.x, max.y, min.z),
        glm::vec2(0, 0), glm::vec2(size.x * uvScale, 0),
        glm::vec2(size.x * uvScale, size.y * uvScale), glm::vec2(0, size.y * uvScale),
        verts, inds);

    // Right face (X+)
    addQuad(
        glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, min.y, min.z),
        glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z),
        glm::vec2(0, 0), glm::vec2(size.z * uvScale, 0),
        glm::vec2(size.z * uvScale, size.y * uvScale), glm::vec2(0, size.y * uvScale),
        verts, inds);

    // Left face (X-)
    addQuad(
        glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z),
        glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, max.y, min.z),
        glm::vec2(0, 0), glm::vec2(size.z * uvScale, 0),
        glm::vec2(size.z * uvScale, size.y * uvScale), glm::vec2(0, size.y * uvScale),
        verts, inds);

    // Top face (Y+)
    addQuad(
        glm::vec3(min.x, max.y, max.z), glm::vec3(max.x, max.y, max.z),
        glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z),
        glm::vec2(0, 0), glm::vec2(size.x * uvScale, 0),
        glm::vec2(size.x * uvScale, size.z * uvScale), glm::vec2(0, size.z * uvScale),
        verts, inds);

    // Bottom face (Y-)
    addQuad(
        glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z),
        glm::vec3(max.x, min.y, max.z), glm::vec3(min.x, min.y, max.z),
        glm::vec2(0, 0), glm::vec2(size.x * uvScale, 0),
        glm::vec2(size.x * uvScale, size.z * uvScale), glm::vec2(0, size.z * uvScale),
        verts, inds);
}

void BuildingMeshGenerator::generatePeakedRoof(const glm::vec3& baseMin, const glm::vec3& baseMax,
                                                float peakHeight, float overhang,
                                                std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float midZ = (baseMin.z + baseMax.z) * 0.5f;
    float roofTop = baseMax.y + peakHeight;

    // Extend base for overhang
    glm::vec3 oMin = baseMin - glm::vec3(overhang, 0, overhang);
    glm::vec3 oMax = baseMax + glm::vec3(overhang, 0, overhang);

    // Ridge points
    glm::vec3 ridgeStart(oMin.x, roofTop, midZ);
    glm::vec3 ridgeEnd(oMax.x, roofTop, midZ);

    // Front slope
    addQuad(
        glm::vec3(oMin.x, baseMax.y, oMax.z), glm::vec3(oMax.x, baseMax.y, oMax.z),
        ridgeEnd, ridgeStart,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1),
        verts, inds);

    // Back slope
    addQuad(
        glm::vec3(oMax.x, baseMax.y, oMin.z), glm::vec3(oMin.x, baseMax.y, oMin.z),
        ridgeStart, ridgeEnd,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, 1), glm::vec2(0, 1),
        verts, inds);

    // Gable ends (triangles)
    // Left gable
    addTriangle(
        glm::vec3(oMin.x, baseMax.y, oMin.z),
        glm::vec3(oMin.x, baseMax.y, oMax.z),
        ridgeStart,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);

    // Right gable
    addTriangle(
        glm::vec3(oMax.x, baseMax.y, oMax.z),
        glm::vec3(oMax.x, baseMax.y, oMin.z),
        ridgeEnd,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);
}

void BuildingMeshGenerator::generateHippedRoof(const glm::vec3& baseMin, const glm::vec3& baseMax,
                                                float peakHeight, float overhang,
                                                std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float roofTop = baseMax.y + peakHeight;
    glm::vec3 center((baseMin.x + baseMax.x) * 0.5f, roofTop, (baseMin.z + baseMax.z) * 0.5f);

    glm::vec3 oMin = baseMin - glm::vec3(overhang, 0, overhang);
    glm::vec3 oMax = baseMax + glm::vec3(overhang, 0, overhang);

    // Four triangular faces meeting at center point
    // Front
    addTriangle(
        glm::vec3(oMin.x, baseMax.y, oMax.z),
        glm::vec3(oMax.x, baseMax.y, oMax.z),
        center,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);

    // Back
    addTriangle(
        glm::vec3(oMax.x, baseMax.y, oMin.z),
        glm::vec3(oMin.x, baseMax.y, oMin.z),
        center,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);

    // Right
    addTriangle(
        glm::vec3(oMax.x, baseMax.y, oMax.z),
        glm::vec3(oMax.x, baseMax.y, oMin.z),
        center,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);

    // Left
    addTriangle(
        glm::vec3(oMin.x, baseMax.y, oMin.z),
        glm::vec3(oMin.x, baseMax.y, oMax.z),
        center,
        glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(0.5f, 1),
        verts, inds);
}

void BuildingMeshGenerator::generateCylindricalBase(const glm::vec3& center, float radius, float height,
                                                     int segments, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float y0 = center.y;
    float y1 = center.y + height;

    for (int i = 0; i < segments; ++i) {
        float a0 = (static_cast<float>(i) / segments) * 6.28318f;
        float a1 = (static_cast<float>(i + 1) / segments) * 6.28318f;

        float x0 = center.x + std::cos(a0) * radius;
        float z0 = center.z + std::sin(a0) * radius;
        float x1 = center.x + std::cos(a1) * radius;
        float z1 = center.z + std::sin(a1) * radius;

        float u0 = static_cast<float>(i) / segments;
        float u1 = static_cast<float>(i + 1) / segments;

        // Side quad
        addQuad(
            glm::vec3(x0, y0, z0), glm::vec3(x1, y0, z1),
            glm::vec3(x1, y1, z1), glm::vec3(x0, y1, z0),
            glm::vec2(u0, 0), glm::vec2(u1, 0), glm::vec2(u1, 1), glm::vec2(u0, 1),
            verts, inds);
    }
}

void BuildingMeshGenerator::generateConicalRoof(const glm::vec3& center, float baseRadius, float height,
                                                 int segments, std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float y0 = center.y;
    glm::vec3 apex(center.x, center.y + height, center.z);

    for (int i = 0; i < segments; ++i) {
        float a0 = (static_cast<float>(i) / segments) * 6.28318f;
        float a1 = (static_cast<float>(i + 1) / segments) * 6.28318f;

        float x0 = center.x + std::cos(a0) * baseRadius;
        float z0 = center.z + std::sin(a0) * baseRadius;
        float x1 = center.x + std::cos(a1) * baseRadius;
        float z1 = center.z + std::sin(a1) * baseRadius;

        float u0 = static_cast<float>(i) / segments;
        float u1 = static_cast<float>(i + 1) / segments;

        addTriangle(
            glm::vec3(x0, y0, z0),
            glm::vec3(x1, y0, z1),
            apex,
            glm::vec2(u0, 0), glm::vec2(u1, 0), glm::vec2(0.5f, 1),
            verts, inds);
    }
}

void BuildingMeshGenerator::generateBuilding(BuildingType type, const glm::vec3& dimensions, float randomSeed,
                                              std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    switch (type) {
        case BuildingType::SmallHouse:
            generateSmallHouse(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::MediumHouse:
            generateMediumHouse(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Tavern:
            generateTavern(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Workshop:
            generateWorkshop(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Church:
            generateChurch(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::WatchTower:
            generateWatchTower(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Well:
            generateWell(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Market:
            generateMarket(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Barn:
            generateBarn(dimensions, randomSeed, outVertices, outIndices);
            break;
        case BuildingType::Windmill:
            generateWindmill(dimensions, randomSeed, outVertices, outIndices);
            break;
    }
}

void BuildingMeshGenerator::generateSmallHouse(const glm::vec3& dims, float seed,
                                                std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float wallHeight = dims.y * 0.7f;

    // Main box (walls)
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd), 0.5f, verts, inds);

    // Peaked roof
    generatePeakedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd),
                       dims.y * 0.3f, 0.3f, verts, inds);
}

void BuildingMeshGenerator::generateMediumHouse(const glm::vec3& dims, float seed,
                                                 std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float wallHeight = dims.y * 0.65f;

    // Main structure
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd), 0.5f, verts, inds);

    // Second floor slight overhang (Tudor style)
    float overhang = 0.3f;
    generateBox(glm::vec3(-hw - overhang, wallHeight * 0.5f, -hd - overhang),
                glm::vec3(hw + overhang, wallHeight, hd + overhang), 0.5f, verts, inds);

    // Peaked roof
    generatePeakedRoof(glm::vec3(-hw - overhang, 0, -hd - overhang),
                       glm::vec3(hw + overhang, wallHeight, hd + overhang),
                       dims.y * 0.35f, 0.4f, verts, inds);
}

void BuildingMeshGenerator::generateTavern(const glm::vec3& dims, float seed,
                                            std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float wallHeight = dims.y * 0.6f;

    // Main building
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd), 0.5f, verts, inds);

    // Second floor
    generateBox(glm::vec3(-hw - 0.4f, wallHeight * 0.55f, -hd - 0.4f),
                glm::vec3(hw + 0.4f, wallHeight, hd + 0.4f), 0.5f, verts, inds);

    // Main roof
    generatePeakedRoof(glm::vec3(-hw - 0.4f, 0, -hd - 0.4f),
                       glm::vec3(hw + 0.4f, wallHeight, hd + 0.4f),
                       dims.y * 0.4f, 0.5f, verts, inds);

    // Small chimney
    float cx = hw * 0.3f;
    float cz = -hd * 0.3f;
    generateBox(glm::vec3(cx - 0.3f, wallHeight, cz - 0.3f),
                glm::vec3(cx + 0.3f, wallHeight + dims.y * 0.5f, cz + 0.3f), 1.0f, verts, inds);
}

void BuildingMeshGenerator::generateWorkshop(const glm::vec3& dims, float seed,
                                              std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float wallHeight = dims.y * 0.7f;

    // Main workshop area
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd), 0.5f, verts, inds);

    // Lean-to extension
    float extendX = hw * 0.4f;
    generateBox(glm::vec3(hw, 0, -hd * 0.6f), glm::vec3(hw + extendX, wallHeight * 0.7f, hd * 0.6f),
                0.5f, verts, inds);

    // Main peaked roof
    generatePeakedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd),
                       dims.y * 0.3f, 0.3f, verts, inds);
}

void BuildingMeshGenerator::generateChurch(const glm::vec3& dims, float seed,
                                            std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float naveHeight = dims.y * 0.5f;

    // Main nave
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, naveHeight, hd * 0.6f), 0.5f, verts, inds);

    // Peaked roof over nave
    generatePeakedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, naveHeight, hd * 0.6f),
                       dims.y * 0.25f, 0.4f, verts, inds);

    // Bell tower
    float towerW = hw * 0.5f;
    generateBox(glm::vec3(-towerW, 0, hd * 0.3f), glm::vec3(towerW, dims.y * 0.8f, hd), 0.5f, verts, inds);

    // Tower roof (pointed)
    generateHippedRoof(glm::vec3(-towerW, 0, hd * 0.3f), glm::vec3(towerW, dims.y * 0.8f, hd),
                       dims.y * 0.2f, 0.2f, verts, inds);
}

void BuildingMeshGenerator::generateWatchTower(const glm::vec3& dims, float seed,
                                                std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float mainHeight = dims.y * 0.75f;

    // Main tower body
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, mainHeight, hd), 0.5f, verts, inds);

    // Crenellations at top (simple raised sections)
    float crenH = dims.y * 0.1f;
    generateBox(glm::vec3(-hw, mainHeight, -hd), glm::vec3(-hw * 0.3f, mainHeight + crenH, -hd * 0.3f),
                1.0f, verts, inds);
    generateBox(glm::vec3(hw * 0.3f, mainHeight, -hd), glm::vec3(hw, mainHeight + crenH, -hd * 0.3f),
                1.0f, verts, inds);
    generateBox(glm::vec3(-hw, mainHeight, hd * 0.3f), glm::vec3(-hw * 0.3f, mainHeight + crenH, hd),
                1.0f, verts, inds);
    generateBox(glm::vec3(hw * 0.3f, mainHeight, hd * 0.3f), glm::vec3(hw, mainHeight + crenH, hd),
                1.0f, verts, inds);

    // Pointed roof
    generateHippedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, mainHeight + crenH, hd),
                       dims.y * 0.15f, 0.1f, verts, inds);
}

void BuildingMeshGenerator::generateWell(const glm::vec3& dims, float seed,
                                          std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float radius = dims.x * 0.4f;
    float wallHeight = dims.y * 0.4f;

    // Cylindrical base (8 segments)
    generateCylindricalBase(glm::vec3(0, 0, 0), radius, wallHeight, 8, verts, inds);

    // Support posts
    float postR = 0.1f;
    float postH = dims.y;
    generateBox(glm::vec3(-radius - postR, 0, -postR), glm::vec3(-radius + postR, postH, postR),
                1.0f, verts, inds);
    generateBox(glm::vec3(radius - postR, 0, -postR), glm::vec3(radius + postR, postH, postR),
                1.0f, verts, inds);

    // Roof beam
    generateBox(glm::vec3(-radius - postR, postH - 0.1f, -0.15f),
                glm::vec3(radius + postR, postH + 0.1f, 0.15f), 1.0f, verts, inds);

    // Small peaked roof over well
    generatePeakedRoof(glm::vec3(-radius - 0.2f, 0, -radius - 0.2f),
                       glm::vec3(radius + 0.2f, postH, radius + 0.2f),
                       dims.y * 0.3f, 0.1f, verts, inds);
}

void BuildingMeshGenerator::generateMarket(const glm::vec3& dims, float seed,
                                            std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;

    // Counter/table
    generateBox(glm::vec3(-hw, 0, -hd * 0.3f), glm::vec3(hw, dims.y * 0.35f, hd * 0.3f),
                0.5f, verts, inds);

    // Support posts at corners
    float postR = 0.15f;
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(-hw + postR * 2, dims.y * 0.9f, -hd + postR * 2),
                1.0f, verts, inds);
    generateBox(glm::vec3(hw - postR * 2, 0, -hd), glm::vec3(hw, dims.y * 0.9f, -hd + postR * 2),
                1.0f, verts, inds);
    generateBox(glm::vec3(-hw, 0, hd - postR * 2), glm::vec3(-hw + postR * 2, dims.y * 0.9f, hd),
                1.0f, verts, inds);
    generateBox(glm::vec3(hw - postR * 2, 0, hd - postR * 2), glm::vec3(hw, dims.y * 0.9f, hd),
                1.0f, verts, inds);

    // Canopy roof
    generatePeakedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, dims.y * 0.9f, hd),
                       dims.y * 0.3f, 0.5f, verts, inds);
}

void BuildingMeshGenerator::generateBarn(const glm::vec3& dims, float seed,
                                          std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float hw = dims.x * 0.5f;
    float hd = dims.z * 0.5f;
    float wallHeight = dims.y * 0.6f;

    // Main barn structure
    generateBox(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd), 0.5f, verts, inds);

    // Gambrel-style roof (approximated with peaked)
    generatePeakedRoof(glm::vec3(-hw, 0, -hd), glm::vec3(hw, wallHeight, hd),
                       dims.y * 0.4f, 0.4f, verts, inds);
}

void BuildingMeshGenerator::generateWindmill(const glm::vec3& dims, float seed,
                                              std::vector<Vertex>& verts, std::vector<uint32_t>& inds) {
    float radius = dims.x * 0.4f;
    float height = dims.y * 0.7f;

    // Cylindrical tower
    generateCylindricalBase(glm::vec3(0, 0, 0), radius, height, 12, verts, inds);

    // Conical cap
    generateConicalRoof(glm::vec3(0, height, 0), radius * 1.2f, dims.y * 0.3f, 12, verts, inds);

    // Simplified blade hub (box)
    float hubZ = radius * 1.1f;
    generateBox(glm::vec3(-0.2f, height * 0.7f, hubZ - 0.2f),
                glm::vec3(0.2f, height * 0.9f, hubZ + 0.2f), 1.0f, verts, inds);

    // Simple blade representations (4 flat quads)
    float bladeLen = dims.y * 0.4f;
    float bladeW = 0.3f;

    // Vertical blades
    addQuad(
        glm::vec3(-bladeW * 0.5f, height * 0.8f + bladeLen, hubZ + 0.3f),
        glm::vec3(bladeW * 0.5f, height * 0.8f + bladeLen, hubZ + 0.3f),
        glm::vec3(bladeW * 0.5f, height * 0.8f, hubZ + 0.3f),
        glm::vec3(-bladeW * 0.5f, height * 0.8f, hubZ + 0.3f),
        glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0), glm::vec2(0, 0),
        verts, inds);

    addQuad(
        glm::vec3(-bladeW * 0.5f, height * 0.8f, hubZ + 0.3f),
        glm::vec3(bladeW * 0.5f, height * 0.8f, hubZ + 0.3f),
        glm::vec3(bladeW * 0.5f, height * 0.8f - bladeLen, hubZ + 0.3f),
        glm::vec3(-bladeW * 0.5f, height * 0.8f - bladeLen, hubZ + 0.3f),
        glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0), glm::vec2(0, 0),
        verts, inds);

    // Horizontal blades
    addQuad(
        glm::vec3(-bladeLen, height * 0.8f - bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(-bladeLen, height * 0.8f + bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(0, height * 0.8f + bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(0, height * 0.8f - bladeW * 0.5f, hubZ + 0.3f),
        glm::vec2(0, 0), glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0),
        verts, inds);

    addQuad(
        glm::vec3(0, height * 0.8f - bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(0, height * 0.8f + bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(bladeLen, height * 0.8f + bladeW * 0.5f, hubZ + 0.3f),
        glm::vec3(bladeLen, height * 0.8f - bladeW * 0.5f, hubZ + 0.3f),
        glm::vec2(0, 0), glm::vec2(0, 1), glm::vec2(1, 1), glm::vec2(1, 0),
        verts, inds);
}

void BuildingMeshGenerator::generateRoadSegment(const glm::vec3& start, const glm::vec3& end, float width,
                                                 std::vector<Vertex>& outVertices, std::vector<uint32_t>& outIndices) {
    outVertices.clear();
    outIndices.clear();

    // Direction along road
    glm::vec3 dir = end - start;
    float length = glm::length(dir);
    if (length < 0.01f) return;

    dir = glm::normalize(dir);

    // Perpendicular direction (in XZ plane)
    glm::vec3 perp = glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)));
    float hw = width * 0.5f;

    // Slight elevation above terrain to prevent z-fighting
    float elevate = 0.02f;

    glm::vec3 p0 = start - perp * hw + glm::vec3(0, elevate, 0);
    glm::vec3 p1 = start + perp * hw + glm::vec3(0, elevate, 0);
    glm::vec3 p2 = end + perp * hw + glm::vec3(0, elevate, 0);
    glm::vec3 p3 = end - perp * hw + glm::vec3(0, elevate, 0);

    addQuad(p0, p1, p2, p3,
            glm::vec2(0, 0), glm::vec2(1, 0), glm::vec2(1, length / width), glm::vec2(0, length / width),
            outVertices, outIndices);
}
