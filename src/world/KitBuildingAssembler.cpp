#include "KitBuildingAssembler.h"
#include "FootprintRing.h"

#include <SDL3/SDL_log.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <utility>

namespace {
constexpr float kModule = 2.0f;      // Wall bay width (metres)
constexpr float kWallHeight = 3.0f;  // Eaves height of one storey
constexpr int kMaxStoreys = 6;

// Per-edge bay stretch stays in this range so wood trim, openings, and door
// leaves never look visibly squashed or widened.
constexpr float kMinStretch = 0.7f;
constexpr float kMaxStretch = 1.35f;
// Edges shorter than this get a plain textured quad instead of a kit bay.
constexpr float kMinBayEdge = 1.4f;
// Vertices with less turn than this are treated as collinear (no corner post).
constexpr float kMinCornerTurn = glm::radians(10.0f);

// Small deterministic hash: same inputs -> same style choices on every run.
uint32_t hashCombine(uint32_t h, uint32_t v) {
    h ^= v + 0x9e3779b9u + (h << 6) + (h >> 2);
    return h;
}

uint32_t hashU(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t h = hashCombine(hashCombine(hashCombine(0x811c9dc5u, a), b), c);
    // Final avalanche (murmur3 finalizer)
    h ^= h >> 16; h *= 0x85ebca6bu;
    h ^= h >> 13; h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return h;
}

float hash01(uint32_t a, uint32_t b, uint32_t c) {
    return static_cast<float>(hashU(a, b, c) & 0xFFFFFFu) / 16777216.0f;
}

// Bay count for an edge of length len whose stretch stays within bounds.
// Returns 0 when no count fits (caller falls back to a plain quad).
int bayCountForEdge(float len) {
    int n0 = std::max(1, static_cast<int>(std::lround(len / kModule)));
    for (int n : {n0, n0 + 1, std::max(1, n0 - 1)}) {
        float s = len / (kModule * n);
        if (s >= kMinStretch && s <= kMaxStretch) return n;
    }
    return 0;
}

// Facade styles for the grammar. `groundEndL/R` cap the ground-floor brick
// skirt at edge ends (plaster only); `upperSolid` is the storey band above.
struct WallStyle {
    const char* ground;      // Ground-storey solid bay (mid-run)
    const char* groundEndL;  // Ground bay at the +X (first) end of an edge
    const char* groundEndR;  // Ground bay at the -X (last) end of an edge
    const char* upperSolid;  // Upper-storey solid bay
    const char* window;      // Upper-storey bay with window opening
    const char* door;        // Ground-storey bay with door opening
    const char* plainMat;    // Material for short-edge fallback quads
    bool jetty;              // Upper storeys cantilever outward
    bool shutters;           // Windows may get open shutters
};

// Timber-framed cottage: brick ground floor, jettied wood-grid upper storeys.
constexpr WallStyle kTimber{
    "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Straight",
    "Wall_Plaster_WoodGrid", "Wall_Plaster_Window_Wide_Flat", "Wall_UnevenBrick_Door_Flat",
    "MI_UnevenBrick", /*jetty*/ true, /*shutters*/ true};
// Plaster house: brick skirt with cornered ends, plain plaster uppers.
constexpr WallStyle kPlaster{
    "Wall_Plaster_Straight_Base", "Wall_Plaster_Straight_L", "Wall_Plaster_Straight_R",
    "Wall_Plaster_Straight", "Wall_Plaster_Window_Wide_Flat", "Wall_Plaster_Door_Flat",
    "MI_Plaster", /*jetty*/ false, /*shutters*/ true};
// Brick barn/workshop: uniform uneven brick, sparse windows, no garnish.
constexpr WallStyle kBrick{
    "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Straight",
    "Wall_UnevenBrick_Straight", "Wall_UnevenBrick_Window_Wide_Flat", "Wall_UnevenBrick_Door_Flat",
    "MI_UnevenBrick", /*jetty*/ false, /*shutters*/ false};

// Symmetric window columns: bays at an odd "distance from centre" step of
// `period` get windows, mirrored about the edge centre and identical on every
// upper storey so windows align vertically.
bool windowAt(int k, int bays, int period) {
    int centreDist = std::abs(2 * k + 1 - bays) / 2;
    return centreDist % period == 0;
}

// Pre-assembled gable roof pieces: Roof_RoundTiles_WxD covers W x D metres
// with ~0.75m eave overhang on every side; the ridge runs along local Z.
// Ridge height above the wall top, per width span.
struct RoofFit {
    int w = 0, d = 0;
    float ridgeHeight = 0.0f;
};

RoofFit fitRoofPiece(float across, float along) {
    static constexpr struct { int w, d; } kPieces[] = {
        {4, 4}, {4, 6}, {4, 8},
        {6, 4}, {6, 6}, {6, 8}, {6, 10}, {6, 12}, {6, 14},
        {8, 8}, {8, 10}, {8, 12}, {8, 14},
    };
    RoofFit fit;
    int w = std::clamp(static_cast<int>(std::lround(across / 2.0f)) * 2, 4, 8);
    int d = std::clamp(static_cast<int>(std::lround(along / 2.0f)) * 2, 4, 14);
    // The overhang absorbs up to ~1.2m of mismatch per axis; walls sticking
    // out past the eaves mean no piece fits.
    if (across > w + 1.2f || along > d + 1.2f) return fit;
    for (const auto& p : kPieces) {
        if (p.w == w && p.d == d) {
            fit.w = w;
            fit.d = d;
            fit.ridgeHeight = (w == 4) ? 3.73f : (w == 6) ? 4.89f : 6.0f;
            return fit;
        }
    }
    return fit;
}

// Interpolate all vertex attributes for a clip-plane crossing.
Vertex lerpVertex(const Vertex& a, const Vertex& b, float t) {
    Vertex out;
    out.position = glm::mix(a.position, b.position, t);
    out.normal = glm::normalize(glm::mix(a.normal, b.normal, t));
    out.texCoord = glm::mix(a.texCoord, b.texCoord, t);
    out.tangent = glm::vec4(
        glm::normalize(glm::mix(glm::vec3(a.tangent), glm::vec3(b.tangent), t)), a.tangent.w);
    out.color = glm::mix(a.color, b.color, t);
    return out;
}

// Even-metre roof-length quantization, rounding up so the piece always
// reaches (overshoot gets chopped).
int evenCeil(float x) {
    return 2 * static_cast<int>(std::ceil(x / 2.0f - 1e-4f));
}

// Crossing-number point-in-polygon test (local 2D coords).
bool pointInRing(const std::vector<glm::vec2>& ring, const glm::vec2& p) {
    bool inside = false;
    for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++) {
        const glm::vec2 &a = ring[i], &b = ring[j];
        if ((a.y > p.y) != (b.y > p.y) &&
            p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x) {
            inside = !inside;
        }
    }
    return inside;
}

// Offset a CCW ring outward by dist along mitered vertex bisectors (clamped
// so near-U-turn vertices don't spike). Vertex count and order are preserved.
std::vector<glm::vec2> offsetRing(const std::vector<glm::vec2>& ring, float dist) {
    const size_t n = ring.size();
    std::vector<glm::vec2> out(ring);
    for (size_t v = 0; v < n; ++v) {
        const glm::vec2 dIn = glm::normalize(ring[v] - ring[(v + n - 1) % n]);
        const glm::vec2 dOut = glm::normalize(ring[(v + 1) % n] - ring[v]);
        glm::vec2 bisector = glm::vec2(dIn.y, -dIn.x) + glm::vec2(dOut.y, -dOut.x);
        float len2 = glm::dot(bisector, bisector);
        if (len2 < 1e-6f) bisector = glm::vec2(dIn.y, -dIn.x);
        else bisector *= 2.0f / len2;  // Miter: |offset| = 1/cos(half turn)
        if (glm::dot(bisector, bisector) > 9.0f) bisector = glm::normalize(bisector) * 3.0f;
        out[v] += bisector * dist;
    }
    return out;
}

} // namespace

KitBuildingAssembler::KitBuildingAssembler(std::string modelsDir)
    : modelsDir_(std::move(modelsDir)) {}

const std::vector<KitPart>& KitBuildingAssembler::piece(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second;
    auto parts = KitMeshLoader::load(modelsDir_ + "/" + name + ".gltf");
    if (parts.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "KitBuildingAssembler: piece '%s' failed to load", name.c_str());
    }
    return cache_.emplace(name, std::move(parts)).first->second;
}

KitBuildingAssembler::MaterialMesh& KitBuildingAssembler::meshForMaterial(
    const std::string& materialName) {
    auto it = meshIndex_.find(materialName);
    if (it != meshIndex_.end()) return meshes_[it->second];
    meshIndex_[materialName] = meshes_.size();
    meshes_.push_back(MaterialMesh{materialName, {}, {}});
    return meshes_.back();
}

std::vector<KitBuildingAssembler::MaterialMesh> KitBuildingAssembler::takeMaterialMeshes() {
    meshIndex_.clear();
    vertexCount_ = 0;
    return std::exchange(meshes_, {});
}

void KitBuildingAssembler::stamp(const std::string& pieceName, const glm::mat4& transform) {
    // Inverse-transpose keeps normals correct under the non-uniform bay stretch
    const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(transform)));
    for (const auto& part : piece(pieceName)) {
        MaterialMesh& mesh = meshForMaterial(part.materialName);
        const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
        for (const auto& v : part.vertices) {
            Vertex out = v;
            out.position = glm::vec3(transform * glm::vec4(v.position, 1.0f));
            out.normal = glm::normalize(normalMat * v.normal);
            out.tangent = glm::vec4(glm::normalize(normalMat * glm::vec3(v.tangent)), v.tangent.w);
            mesh.vertices.push_back(out);
        }
        for (uint32_t idx : part.indices) mesh.indices.push_back(base + idx);
        vertexCount_ += part.vertices.size();
    }
}

void KitBuildingAssembler::stampClipped(const std::string& pieceName,
                                        const glm::mat4& transform,
                                        const glm::vec4& plane) {
    const glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(transform)));
    const glm::vec3 pn(plane);
    for (const auto& part : piece(pieceName)) {
        MaterialMesh& mesh = meshForMaterial(part.materialName);

        // Transform to world first, then clip each triangle.
        std::vector<Vertex> world(part.vertices.size());
        for (size_t i = 0; i < part.vertices.size(); ++i) {
            const Vertex& v = part.vertices[i];
            Vertex& out = world[i];
            out = v;
            out.position = glm::vec3(transform * glm::vec4(v.position, 1.0f));
            out.normal = glm::normalize(normalMat * v.normal);
            out.tangent =
                glm::vec4(glm::normalize(normalMat * glm::vec3(v.tangent)), v.tangent.w);
        }

        const size_t vertsBefore = mesh.vertices.size();
        for (size_t t = 0; t + 2 < part.indices.size(); t += 3) {
            const Vertex* tri[3] = {&world[part.indices[t]], &world[part.indices[t + 1]],
                                    &world[part.indices[t + 2]]};
            // Sutherland-Hodgman against one plane: keep dot(n,p)+w <= 0
            Vertex poly[4];
            int count = 0;
            for (int i = 0; i < 3; ++i) {
                const Vertex& a = *tri[i];
                const Vertex& b = *tri[(i + 1) % 3];
                const float da = glm::dot(pn, a.position) + plane.w;
                const float db = glm::dot(pn, b.position) + plane.w;
                if (da <= 0.0f) poly[count++] = a;
                if ((da < 0.0f && db > 0.0f) || (da > 0.0f && db < 0.0f)) {
                    poly[count++] = lerpVertex(a, b, da / (da - db));
                }
            }
            if (count < 3) continue;
            const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
            for (int i = 0; i < count; ++i) mesh.vertices.push_back(poly[i]);
            for (int i = 1; i + 1 < count; ++i) {
                mesh.indices.insert(mesh.indices.end(), {base, base + static_cast<uint32_t>(i),
                                                         base + static_cast<uint32_t>(i + 1)});
            }
        }
        vertexCount_ += mesh.vertices.size() - vertsBefore;
    }
}

bool KitBuildingAssembler::tryLShapedRoof(const std::vector<glm::vec2>& ring,
                                          const OrientedBox& box, float topY,
                                          bool wantChimney, uint32_t buildingSeed) {
    if (ring.size() != 6) return false;

    // Box-local frame: b = local +X direction, q = local +Z (ridge axes).
    const glm::vec2 b(std::cos(box.yaw), -std::sin(box.yaw));
    const glm::vec2 q(std::sin(box.yaw), std::cos(box.yaw));
    std::vector<glm::vec2> local(6);
    for (size_t i = 0; i < 6; ++i) {
        const glm::vec2 rel = ring[i] - box.center;
        local[i] = glm::vec2(glm::dot(rel, b), glm::dot(rel, q));
    }

    // Rectilinear check and single reflex vertex (the L's inner corner)
    int reflex = -1;
    for (size_t i = 0; i < 6; ++i) {
        const glm::vec2 e = local[(i + 1) % 6] - local[i];
        const float len = glm::length(e);
        if (len < 0.5f) return false;
        if (std::min(std::abs(e.x), std::abs(e.y)) > 0.12f * len) return false;
        const glm::vec2 dIn = local[i] - local[(i + 5) % 6];
        if (dIn.x * e.y - dIn.y * e.x < -1e-4f) {
            if (reflex >= 0) return false;
            reflex = static_cast<int>(i);
        }
    }
    if (reflex < 0) return false;
    const glm::vec2 r = local[reflex];

    // The notch is the bounding-box corner whose quadrant (towards the reflex
    // vertex) lies outside the footprint.
    const float w2 = box.width * 0.5f, d2 = box.depth * 0.5f;
    glm::vec2 notch(0.0f);
    int notchCount = 0;
    for (float cx : {-w2, w2}) {
        for (float cz : {-d2, d2}) {
            if (!pointInRing(local, (glm::vec2(cx, cz) + r) * 0.5f)) {
                notch = glm::vec2(cx, cz);
                notchCount++;
            }
        }
    }
    if (notchCount != 1) return false;

    // Two full-length arms overlapping in the corner opposite the notch.
    // H spans the full width (ridge along local X), V the full depth.
    struct Arm {
        glm::vec2 lo, hi;       // Local rect
        bool ridgeAlongX;
        float across() const { return ridgeAlongX ? hi.y - lo.y : hi.x - lo.x; }
        float along() const { return ridgeAlongX ? hi.x - lo.x : hi.y - lo.y; }
        float ridgeCoord() const {  // Across-axis centre (the ridge line)
            return ridgeAlongX ? (lo.y + hi.y) * 0.5f : (lo.x + hi.x) * 0.5f;
        }
    };
    Arm h{{-w2, notch.y > 0 ? -d2 : r.y}, {w2, notch.y > 0 ? r.y : d2}, true};
    Arm v{{notch.x > 0 ? -w2 : r.x, -d2}, {notch.x > 0 ? r.x : w2, d2}, false};

    // Primary = taller ridge (wider arm); secondary tucks under it.
    const bool hPrimary =
        h.across() > v.across() + 1e-3f ||
        (std::abs(h.across() - v.across()) <= 1e-3f && h.along() >= v.along());
    const Arm& prim = hPrimary ? h : v;
    const Arm& sec = hPrimary ? v : h;

    const RoofFit primFit = fitRoofPiece(prim.across(), prim.along());
    if (primFit.w == 0) return false;

    // Secondary runs from its outer end to the primary ridge; the piece
    // length rounds UP so it always reaches, and the overshoot is chopped.
    const float g = prim.ridgeCoord();  // Along the secondary's long axis
    const float secLo = sec.ridgeAlongX ? sec.lo.x : sec.lo.y;
    const float secHi = sec.ridgeAlongX ? sec.hi.x : sec.hi.y;
    // Outer end: the arm end farther from the primary ridge
    const float outer = (std::abs(secHi - g) > std::abs(secLo - g)) ? secHi : secLo;
    const float sgn = (outer > g) ? 1.0f : -1.0f;
    const float reach = std::abs(outer - g);
    const int secW = std::clamp(
        static_cast<int>(std::lround(sec.across() / 2.0f)) * 2, 4, 8);
    if (sec.across() > secW + 1.2f) return false;
    const int secD = std::max(4, evenCeil(reach - 0.75f));
    if (secD > 14 || fitRoofPiece(static_cast<float>(secW), static_cast<float>(secD)).w == 0) {
        return false;
    }
    if (sec.across() + 1e-3f > prim.across()) return false;  // Must tuck under

    // Local rect -> world transform for a roof whose ridge (piece local +Z)
    // runs along the arm's long axis.
    auto roofTransform = [&](const glm::vec2& localCentre, bool ridgeAlongX) {
        const glm::vec2 world = box.center + b * localCentre.x + q * localCentre.y;
        const float yaw = ridgeAlongX ? box.yaw + glm::half_pi<float>() : box.yaw;
        return glm::translate(glm::mat4(1.0f), glm::vec3(world.x, topY, world.y)) *
               glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    };

    // Primary roof with gables at both ends
    const glm::vec2 primCentre = (prim.lo + prim.hi) * 0.5f;
    const glm::mat4 primXform = roofTransform(primCentre, prim.ridgeAlongX);
    stamp("Roof_RoundTiles_" + std::to_string(primFit.w) + "x" + std::to_string(primFit.d),
          primXform);
    const std::string primFront = "Roof_Front_Brick" + std::to_string(primFit.w);
    const float primHalf = prim.along() * 0.5f;
    stamp(primFront,
          primXform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, primHalf)));
    stamp(primFront,
          primXform * glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -primHalf)) *
              glm::rotate(glm::mat4(1.0f), glm::pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));

    // Secondary roof anchored at its outer end, chopped at the primary ridge
    const float secAcrossCentre =
        sec.ridgeAlongX ? (sec.lo.y + sec.hi.y) * 0.5f : (sec.lo.x + sec.hi.x) * 0.5f;
    const float secAlongCentre = outer - sgn * secD * 0.5f;
    const glm::vec2 secCentre = sec.ridgeAlongX
                                    ? glm::vec2(secAlongCentre, secAcrossCentre)
                                    : glm::vec2(secAcrossCentre, secAlongCentre);
    const glm::mat4 secXform = roofTransform(secCentre, sec.ridgeAlongX);
    // World plane through the primary ridge, perpendicular to the secondary's
    // long axis; keep the outer side.
    const glm::vec2 axis = sec.ridgeAlongX ? b : q;
    const glm::vec4 plane(-sgn * axis.x, 0.0f, -sgn * axis.y,
                          sgn * (glm::dot(axis, box.center) + g));
    stampClipped("Roof_RoundTiles_" + std::to_string(secW) + "x" + std::to_string(secD),
                 secXform, plane);
    stampClipped("Roof_Front_Brick" + std::to_string(secW),
                 secXform *
                     glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, sgn * secD * 0.5f)) *
                     (sgn > 0.0f ? glm::mat4(1.0f)
                                 : glm::rotate(glm::mat4(1.0f), glm::pi<float>(),
                                               glm::vec3(0.0f, 1.0f, 0.0f))),
                 plane);

    // Chimney on the primary ridge
    if (wantChimney && hash01(buildingSeed, 0xC4, 1) < 0.8f) {
        const float t = glm::mix(0.25f, 0.75f, hash01(buildingSeed, 0xC4, 2));
        const float alongPos = glm::mix(prim.ridgeAlongX ? prim.lo.x : prim.lo.y,
                                        prim.ridgeAlongX ? prim.hi.x : prim.hi.y, t);
        const glm::vec2 spotLocal = prim.ridgeAlongX ? glm::vec2(alongPos, g)
                                                     : glm::vec2(g, alongPos);
        const glm::vec2 spot = box.center + b * spotLocal.x + q * spotLocal.y;
        const char* chimney =
            hash01(buildingSeed, 0xC4, 0) < 0.3f ? "Prop_Chimney" : "Prop_Chimney2";
        stamp(chimney,
              glm::translate(glm::mat4(1.0f),
                             glm::vec3(spot.x, topY + primFit.ridgeHeight - 2.4f, spot.y)));
    }
    return true;
}

float KitBuildingAssembler::shellTopHeight(float eavesHeight) {
    int storeys = std::clamp(
        static_cast<int>(std::lround(eavesHeight / kWallHeight)), 1, kMaxStoreys);
    return storeys * kWallHeight;
}

void KitBuildingAssembler::addFootprintShell(const std::vector<glm::vec2>& ring,
                                             const OrientedBox& box, float baseY,
                                             float eavesHeight, float plinthBottomY,
                                             uint32_t buildingSeed) {
    const size_t n = ring.size();
    if (n < 3) return;

    const int storeys = std::clamp(
        static_cast<int>(std::lround(eavesHeight / kWallHeight)), 1, kMaxStoreys);
    const float topY = baseY + storeys * kWallHeight;

    // Facade grammar: one style per building drives every choice below so the
    // facade reads designed rather than dice-rolled.
    const float styleRoll = hash01(buildingSeed, 0xB1, 0);
    const WallStyle& style =
        styleRoll < 0.4f ? kTimber : (styleRoll < 0.8f ? kPlaster : kBrick);
    // Window rhythm: every bay or every other bay, mirrored about edge
    // centres and shared by all upper storeys (aligned columns).
    const int windowPeriod = (&style == &kBrick || hash01(buildingSeed, 0x77, 0) < 0.4f) ? 2 : 1;
    const bool shutters = style.shutters && hash01(buildingSeed, 0x5A, 0) < 0.6f;
    // Jettied upper storeys cantilever slightly over the street.
    const bool jetty = style.jetty && storeys >= 2;
    const std::vector<glm::vec2> upperRing = jetty ? offsetRing(ring, 0.25f) : ring;

    // Foundation plinth fills between the terrain and the flat wall base;
    // mitered 3cm outward so its lip sits proud of the walls instead of
    // z-fighting. T_RockTrim is a trim sheet, so V pins to its brick-course
    // strip (only U tiles with distance).
    {
        MaterialMesh& plinth = meshForMaterial("MI_RockTrim");
        footprint::appendBandStrip(plinth.vertices, plinth.indices,
                                   offsetRing(ring, 0.03f),
                                   plinthBottomY, baseY + 0.1f,
                                   /*vBottom*/ 0.98f, /*vTop*/ 0.60f);
        vertexCount_ += 4 * n;
    }
    // Flat cap seals the interior from above; a pitched roof (when a kit
    // piece fits) sits on top of it. 0.5 repeats/m matches wall density.
    {
        MaterialMesh& cap = meshForMaterial("MI_RoundTiles");
        footprint::appendRoofCap(cap.vertices, cap.indices, upperRing, topY, 0.5f);
        vertexCount_ += n;
    }

    // Pitched roof. Rectilinear L-shapes get two gables with the secondary
    // chopped at the primary ridge; other footprints fit one pre-assembled
    // gable roof to the oriented box, ridge along the longer axis. The
    // ~0.75m eave overhang absorbs the box-to-polygon mismatch (and jetty).
    const bool ridgeAlongX = box.width > box.depth;
    const float across = ridgeAlongX ? box.depth : box.width;
    const float along = ridgeAlongX ? box.width : box.depth;
    bool roofed =
        tryLShapedRoof(ring, box, topY, /*wantChimney*/ &style != &kBrick, buildingSeed);
    const RoofFit roof = roofed ? RoofFit{} : fitRoofPiece(across, along);
    if (roof.w > 0) {
        // Roof local Z is the ridge; rotate it onto the box's long axis.
        const float roofYaw =
            ridgeAlongX ? box.yaw + glm::half_pi<float>() : box.yaw;
        const glm::mat4 roofXform =
            glm::translate(glm::mat4(1.0f), glm::vec3(box.center.x, topY, box.center.y)) *
            glm::rotate(glm::mat4(1.0f), roofYaw, glm::vec3(0.0f, 1.0f, 0.0f));
        stamp("Roof_RoundTiles_" + std::to_string(roof.w) + "x" + std::to_string(roof.d),
              roofXform);

        // Framed brick gables close the roof ends at the wall planes.
        const std::string front = "Roof_Front_Brick" + std::to_string(roof.w);
        const float halfAlong = along * 0.5f;
        stamp(front, roofXform * glm::translate(glm::mat4(1.0f),
                                                glm::vec3(0.0f, 0.0f, halfAlong)));
        stamp(front, roofXform *
                         glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -halfAlong)) *
                         glm::rotate(glm::mat4(1.0f), glm::pi<float>(),
                                     glm::vec3(0.0f, 1.0f, 0.0f)));

        // Chimney straddling the ridge (the roof spans the whole box, so any
        // point on the ridge line is over roof surface).
        if (&style != &kBrick && hash01(buildingSeed, 0xC4, 1) < 0.8f) {
            const float t = (hash01(buildingSeed, 0xC4, 2) - 0.5f) * along * 0.5f;
            const glm::vec2 ridgeDir(std::sin(roofYaw), std::cos(roofYaw));
            const glm::vec2 spot = box.center + ridgeDir * t;
            const char* chimney =
                hash01(buildingSeed, 0xC4, 0) < 0.3f ? "Prop_Chimney" : "Prop_Chimney2";
            stamp(chimney,
                  glm::translate(glm::mat4(1.0f),
                                 glm::vec3(spot.x, topY + roof.ridgeHeight - 2.4f, spot.y)));
        }
        roofed = true;
    }
    if (!roofed && &style != &kBrick) {
        // No roof piece fits: chimney on the flat cap. Centroid of the
        // largest cap triangle is guaranteed inside, unlike the ring
        // centroid on L-shapes.
        const auto tris = footprint::triangulateRing(upperRing);
        float bestArea = 0.0f;
        glm::vec2 spot(0.0f);
        for (const auto& t : tris) {
            const glm::vec2 &a = upperRing[t[0]], &b = upperRing[t[1]], &c = upperRing[t[2]];
            float area = std::abs((b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y));
            if (area > bestArea) { bestArea = area; spot = (a + b + c) / 3.0f; }
        }
        if (bestArea > 2.0f) {
            const char* chimney =
                hash01(buildingSeed, 0xC4, 0) < 0.3f ? "Prop_Chimney" : "Prop_Chimney2";
            stamp(chimney, glm::translate(glm::mat4(1.0f),
                                          glm::vec3(spot.x, topY - 0.2f, spot.y)));
        }
    }

    // The door goes in the middle bay of the longest edge
    size_t doorEdge = 0;
    float doorEdgeLen = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float len = glm::distance(ring[i], ring[(i + 1) % n]);
        if (len > doorEdgeLen) { doorEdgeLen = len; doorEdge = i; }
    }

    // Trace one storey band of one ring. Ground (f == 0) walks the base
    // ring; upper storeys walk upperRing so a jetty offsets them together.
    auto traceStorey = [&](const std::vector<glm::vec2>& r, int f) {
        const float y = baseY + f * kWallHeight;
        for (size_t e = 0; e < n; ++e) {
            const glm::vec2& p0 = r[e];
            const glm::vec2& p1 = r[(e + 1) % n];
            const float len = glm::distance(p0, p1);
            if (len < 1e-4f) continue;
            const glm::vec2 d = (p1 - p0) / len;

            const int bays = len < kMinBayEdge ? 0 : bayCountForEdge(len);
            if (bays == 0) {
                // Too short/awkward for kit bays: plain wall quad in the
                // shell's base material so it merges with the kit walls.
                MaterialMesh& plain = meshForMaterial(style.plainMat);
                footprint::appendEdgeBand(plain.vertices, plain.indices, p0, p1,
                                          y, y + kWallHeight);
                vertexCount_ += 4;
                continue;
            }

            // Local +Z -> outward normal (d.y, -d.x); the kit exterior plane
            // (z = 0) then lies exactly on the polygon edge. Local +X points
            // from a bay centre toward p0.
            const float yaw = std::atan2(d.y, -d.x);
            const float stretch = len / (kModule * bays);
            const int door = (f == 0 && e == doorEdge) ? bays / 2 : -1;

            for (int k = 0; k < bays; ++k) {
                const glm::vec2 c = p0 + d * ((k + 0.5f) * len / bays);
                const glm::mat4 xform =
                    glm::translate(glm::mat4(1.0f), glm::vec3(c.x, y, c.y)) *
                    glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                    glm::scale(glm::mat4(1.0f), glm::vec3(stretch, 1.0f, 1.0f));

                const char* pieceName;
                bool window = false;
                if (f == 0) {
                    if (k == door) {
                        pieceName = style.door;
                        stamp("DoorFrame_Flat_WoodDark", xform);
                        // Leaf hinge sits at local x=0; shift so it centres
                        // in the opening.
                        stamp("Door_1_Flat",
                              xform * glm::translate(glm::mat4(1.0f),
                                                     glm::vec3(-0.51f, 0.0f, 0.0f)));
                    } else if (k == 0 && bays > 1) {
                        pieceName = style.groundEndL;  // Skirt cap at the p0 end
                    } else if (k == bays - 1 && bays > 1) {
                        pieceName = style.groundEndR;  // Skirt cap at the p1 end
                    } else {
                        pieceName = style.ground;
                    }
                } else if (windowAt(k, bays, windowPeriod)) {
                    pieceName = style.window;
                    window = true;
                } else {
                    pieceName = style.upperSolid;
                }
                stamp(pieceName, xform);

                if (window) {
                    if (shutters) stamp("WindowShutters_Wide_Flat_Open", xform);
                    // Cheap glass pane just behind the opening (the wall
                    // piece carries its own wood frame). Local +Z faces out.
                    MaterialMesh& glass = meshForMaterial("MI_WindowGlass");
                    const glm::vec3 corners[4] = {
                        {-0.7f, 1.0f, -0.02f}, {0.7f, 1.0f, -0.02f},
                        {0.7f, 2.45f, -0.02f}, {-0.7f, 2.45f, -0.02f}};
                    const glm::mat3 nm(xform);
                    const glm::vec3 nrm = glm::normalize(nm * glm::vec3(0, 0, 1));
                    const glm::vec4 tan(glm::normalize(nm * glm::vec3(1, 0, 0)), 1.0f);
                    const glm::vec2 uvs[4] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
                    const uint32_t base = static_cast<uint32_t>(glass.vertices.size());
                    for (int i = 0; i < 4; ++i) {
                        glass.vertices.push_back(
                            {glm::vec3(xform * glm::vec4(corners[i], 1.0f)), nrm, uvs[i], tan});
                    }
                    glass.indices.insert(glass.indices.end(),
                                         {base, base + 1, base + 2, base + 2, base + 3, base});
                    vertexCount_ += 4;
                }
            }

            // Jettied storey break: a wood trim strip along the upper ring
            // hides the cantilever slit between the two wall planes.
            if (jetty && f == 1) {
                for (int k = 0; k < bays; ++k) {
                    const glm::vec2 c = p0 + d * ((k + 0.5f) * len / bays);
                    stamp("Wall_BottomCover",
                          glm::translate(glm::mat4(1.0f), glm::vec3(c.x, y, c.y)) *
                              glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(stretch, 1.0f, 1.0f)));
                }
            }
        }
    };

    traceStorey(ring, 0);
    for (int f = 1; f < storeys; ++f) traceStorey(upperRing, f);

    // Corner posts hide the wall-slab wedges where edges meet (works at any
    // angle, convex or reflex, because the post is a centred column). Ground
    // posts sit on the base ring, upper posts on the (possibly jettied) ring.
    for (size_t v = 0; v < n; ++v) {
        const glm::vec2& prev = ring[(v + n - 1) % n];
        const glm::vec2& cur = ring[v];
        const glm::vec2& next = ring[(v + 1) % n];
        const glm::vec2 dIn = glm::normalize(cur - prev);
        const glm::vec2 dOut = glm::normalize(next - cur);
        const float turn = std::abs(
            std::atan2(dIn.x * dOut.y - dIn.y * dOut.x, glm::dot(dIn, dOut)));
        if (turn < kMinCornerTurn) continue;

        // Outward-normal bisector; when the two normals cancel (U-turn) fall
        // back to the incoming normal.
        glm::vec2 bisector = glm::vec2(dIn.y, -dIn.x) + glm::vec2(dOut.y, -dOut.x);
        if (glm::dot(bisector, bisector) < 1e-6f) bisector = glm::vec2(dIn.y, -dIn.x);
        const float yaw = std::atan2(bisector.x, bisector.y);

        for (int f = 0; f < storeys; ++f) {
            const glm::vec2& post = (f == 0) ? cur : upperRing[v];
            stamp("Corner_Exterior_Wood",
                  glm::translate(glm::mat4(1.0f),
                                 glm::vec3(post.x, baseY + f * kWallHeight, post.y)) *
                      glm::rotate(glm::mat4(1.0f), yaw, glm::vec3(0.0f, 1.0f, 0.0f)));
        }
    }
}
