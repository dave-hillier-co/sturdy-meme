#include "FootprintRing.h"

#include <algorithm>

namespace footprint {

namespace {

float cross2(const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

bool pointInTriangle(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b,
                     const glm::vec2& c) {
    float d1 = cross2(a, b, p);
    float d2 = cross2(b, c, p);
    float d3 = cross2(c, a, p);
    bool hasNeg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
    bool hasPos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
    return !(hasNeg && hasPos);
}

} // namespace

float ringSignedArea(const std::vector<glm::vec2>& ring) {
    float area = 0.0f;
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2& a = ring[i];
        const glm::vec2& b = ring[(i + 1) % ring.size()];
        area += a.x * b.y - b.x * a.y;
    }
    return area * 0.5f;
}

std::vector<std::array<size_t, 3>> triangulateRing(const std::vector<glm::vec2>& ring) {
    std::vector<std::array<size_t, 3>> tris;
    std::vector<size_t> idx(ring.size());
    for (size_t i = 0; i < idx.size(); ++i) idx[i] = i;

    while (idx.size() > 3) {
        bool clipped = false;
        for (size_t i = 0; i < idx.size(); ++i) {
            size_t ia = idx[(i + idx.size() - 1) % idx.size()];
            size_t ib = idx[i];
            size_t ic = idx[(i + 1) % idx.size()];
            if (cross2(ring[ia], ring[ib], ring[ic]) <= 1e-6f) continue;  // Reflex/degenerate

            bool earClear = true;
            for (size_t j : idx) {
                if (j == ia || j == ib || j == ic) continue;
                if (pointInTriangle(ring[j], ring[ia], ring[ib], ring[ic])) {
                    earClear = false;
                    break;
                }
            }
            if (!earClear) continue;

            tris.push_back({ia, ib, ic});
            idx.erase(idx.begin() + i);
            clipped = true;
            break;
        }
        if (!clipped) {
            // Degenerate polygon: fall back to a fan so we never loop forever
            for (size_t i = 1; i + 1 < idx.size(); ++i) {
                tris.push_back({idx[0], idx[i], idx[i + 1]});
            }
            return tris;
        }
    }
    if (idx.size() == 3) tris.push_back({idx[0], idx[1], idx[2]});
    return tris;
}

bool normalizeRing(std::vector<glm::vec2>& ring) {
    if (ring.size() >= 2 && glm::distance(ring.front(), ring.back()) < 1e-3f) {
        ring.pop_back();
    }
    if (ring.size() < 3) return false;
    if (ringSignedArea(ring) < 0.0f) std::reverse(ring.begin(), ring.end());
    return true;
}

void appendEdgeBand(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                    const glm::vec2& p0, const glm::vec2& p1, float baseY, float topY) {
    glm::vec2 edge = p1 - p0;
    float len = glm::length(edge);
    if (len < 1e-4f) return;
    glm::vec2 d = edge / len;
    glm::vec3 normal(d.y, 0.0f, -d.x);
    glm::vec4 tangent(-d.x, 0.0f, -d.y, 1.0f);

    const float uvScale = 0.5f;
    float u0 = glm::dot(p1, -d) * uvScale;  // Along the +U (tangent) direction
    float u1 = glm::dot(p0, -d) * uvScale;
    float vBase = -baseY * uvScale;
    float vTop = -topY * uvScale;

    uint32_t base = static_cast<uint32_t>(verts.size());
    verts.push_back({{p1.x, baseY, p1.y}, normal, {u0, vBase}, tangent});
    verts.push_back({{p0.x, baseY, p0.y}, normal, {u1, vBase}, tangent});
    verts.push_back({{p0.x, topY, p0.y}, normal, {u1, vTop}, tangent});
    verts.push_back({{p1.x, topY, p1.y}, normal, {u0, vTop}, tangent});
    inds.insert(inds.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
}

void appendBandWalls(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                     const std::vector<glm::vec2>& ring, float baseY, float topY) {
    for (size_t i = 0; i < ring.size(); ++i) {
        appendEdgeBand(verts, inds, ring[i], ring[(i + 1) % ring.size()], baseY, topY);
    }
}

void appendBandStrip(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                     const std::vector<glm::vec2>& ring, float baseY, float topY,
                     float vBottom, float vTop) {
    for (size_t i = 0; i < ring.size(); ++i) {
        const glm::vec2& p0 = ring[i];
        const glm::vec2& p1 = ring[(i + 1) % ring.size()];
        glm::vec2 edge = p1 - p0;
        float len = glm::length(edge);
        if (len < 1e-4f) continue;
        glm::vec2 d = edge / len;
        glm::vec3 normal(d.y, 0.0f, -d.x);
        glm::vec4 tangent(-d.x, 0.0f, -d.y, 1.0f);

        const float uvScale = 0.5f;
        float u0 = glm::dot(p1, -d) * uvScale;
        float u1 = glm::dot(p0, -d) * uvScale;

        uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back({{p1.x, baseY, p1.y}, normal, {u0, vBottom}, tangent});
        verts.push_back({{p0.x, baseY, p0.y}, normal, {u1, vBottom}, tangent});
        verts.push_back({{p0.x, topY, p0.y}, normal, {u1, vTop}, tangent});
        verts.push_back({{p1.x, topY, p1.y}, normal, {u0, vTop}, tangent});
        inds.insert(inds.end(), {base, base + 1, base + 2, base + 2, base + 3, base});
    }
}

void appendRoofCap(std::vector<Vertex>& verts, std::vector<uint32_t>& inds,
                   const std::vector<glm::vec2>& ring, float topY, float uvScale) {
    uint32_t roofBase = static_cast<uint32_t>(verts.size());
    for (const auto& p : ring) {
        verts.push_back({{p.x, topY, p.y}, {0.0f, 1.0f, 0.0f},
                         {p.x * uvScale, p.y * uvScale}, {1.0f, 0.0f, 0.0f, 1.0f}});
    }
    for (const auto& tri : triangulateRing(ring)) {
        // CCW-from-above needs reversed order relative to the math-CCW ring
        inds.push_back(roofBase + static_cast<uint32_t>(tri[2]));
        inds.push_back(roofBase + static_cast<uint32_t>(tri[1]));
        inds.push_back(roofBase + static_cast<uint32_t>(tri[0]));
    }
}

} // namespace footprint
