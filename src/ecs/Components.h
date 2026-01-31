#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>

// Forward declarations
struct Mesh;
struct Texture;

namespace ecs {

// =============================================================================
// Transform Component
// =============================================================================
// Stores world-space transformation. The matrix is kept in a GPU-friendly
// layout (column-major, std140 compatible) so it can be uploaded directly
// to an SSBO for GPU-driven rendering.

struct Transform {
    glm::mat4 matrix = glm::mat4(1.0f);

    // Convenience constructors
    Transform() = default;
    explicit Transform(const glm::mat4& m) : matrix(m) {}
    Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale);

    // Decompose helpers
    [[nodiscard]] glm::vec3 position() const { return glm::vec3(matrix[3]); }
    void setPosition(const glm::vec3& pos) { matrix[3] = glm::vec4(pos, 1.0f); }

    // Static factory methods
    static Transform fromPosition(const glm::vec3& pos);
    static Transform fromPositionRotation(const glm::vec3& pos, const glm::quat& rot);
    static Transform fromTRS(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale);
};

// =============================================================================
// Mesh Reference Component
// =============================================================================
// Points to a shared Mesh resource. Multiple entities can reference the same
// mesh for instanced rendering.

struct MeshRef {
    Mesh* mesh = nullptr;

    MeshRef() = default;
    explicit MeshRef(Mesh* m) : mesh(m) {}

    [[nodiscard]] bool valid() const { return mesh != nullptr; }
};

// =============================================================================
// Material Reference Component
// =============================================================================
// References a material by ID for descriptor set lookup. Entities with the
// same material can be batched together for efficient rendering.

using MaterialId = uint32_t;
constexpr MaterialId InvalidMaterialId = UINT32_MAX;

struct MaterialRef {
    MaterialId id = InvalidMaterialId;

    MaterialRef() = default;
    explicit MaterialRef(MaterialId matId) : id(matId) {}

    [[nodiscard]] bool valid() const { return id != InvalidMaterialId; }
};

// =============================================================================
// PBR Material Properties Component
// =============================================================================
// Per-entity material overrides. Only entities that need custom PBR values
// should have this component - others use material defaults.

struct PBRProperties {
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    float alphaTestThreshold = 0.0f;
    uint32_t pbrFlags = 0;

    PBRProperties() = default;
};

// =============================================================================
// Render Tag Components (Zero-size markers)
// =============================================================================
// Tag components for controlling render pass participation.
// These are sparse - only entities needing the feature have the tag.

// Entity casts shadows (participates in shadow pass)
struct CastsShadow {};

// Entity is visible (set by culling system, queried by render system)
struct Visible {};

// Entity should be rendered with transparency
struct Transparent {
    float sortKey = 0.0f;  // For back-to-front sorting
};

// Entity participates in reflection rendering
struct Reflective {};

// =============================================================================
// Bounds Components
// =============================================================================
// Used for culling. Only one bounds type needed per entity.

struct BoundingSphere {
    glm::vec3 center = glm::vec3(0.0f);
    float radius = 0.0f;

    BoundingSphere() = default;
    BoundingSphere(const glm::vec3& c, float r) : center(c), radius(r) {}
};

struct BoundingBox {
    glm::vec3 min = glm::vec3(0.0f);
    glm::vec3 max = glm::vec3(0.0f);

    BoundingBox() = default;
    BoundingBox(const glm::vec3& minPt, const glm::vec3& maxPt) : min(minPt), max(maxPt) {}

    [[nodiscard]] glm::vec3 center() const { return (min + max) * 0.5f; }
    [[nodiscard]] glm::vec3 extents() const { return (max - min) * 0.5f; }
};

// =============================================================================
// Visual Effect Components
// =============================================================================
// Sparse components for optional visual effects.

// Hue shift for tinting (used by NPCs)
struct HueShift {
    float value = 0.0f;

    HueShift() = default;
    explicit HueShift(float v) : value(v) {}
};

// Opacity for fade effects (camera occlusion)
struct Opacity {
    float value = 1.0f;

    Opacity() = default;
    explicit Opacity(float v) : value(v) {}
};

// =============================================================================
// Tree-specific Components
// =============================================================================
// Only tree entities have these components.

struct TreeData {
    int leafInstanceIndex = -1;
    int treeInstanceIndex = -1;
    glm::vec3 leafTint = glm::vec3(1.0f);
    float autumnHueShift = 0.0f;
};

struct BarkType {
    uint32_t typeIndex = 0;  // Index into bark texture array
};

struct LeafType {
    uint32_t typeIndex = 0;  // Index into leaf texture array
};

// =============================================================================
// LOD Component
// =============================================================================
// Unified LOD management across all entity types.

struct LODController {
    float thresholds[3] = {50.0f, 150.0f, 500.0f};  // Distance thresholds
    uint8_t currentLevel = 0;                        // 0=high, 1=medium, 2=low
    uint16_t updateInterval = 1;                     // Frames between updates
    uint16_t frameCounter = 0;

    LODController() = default;
    explicit LODController(float near, float mid, float far)
        : thresholds{near, mid, far} {}
};

// =============================================================================
// Physics Component
// =============================================================================
// Links an entity to a physics body.

using PhysicsBodyId = uint32_t;
constexpr PhysicsBodyId InvalidPhysicsBodyId = UINT32_MAX;

struct PhysicsBody {
    PhysicsBodyId bodyId = InvalidPhysicsBodyId;

    PhysicsBody() = default;
    explicit PhysicsBody(PhysicsBodyId id) : bodyId(id) {}

    [[nodiscard]] bool valid() const { return bodyId != InvalidPhysicsBodyId; }
};

// =============================================================================
// Name/Debug Component
// =============================================================================
// Optional debug name for entities (development only).

struct DebugName {
    const char* name = nullptr;

    DebugName() = default;
    explicit DebugName(const char* n) : name(n) {}
};

} // namespace ecs
