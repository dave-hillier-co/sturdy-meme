#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <optional>
#include <cassert>
#include <cstdint>
#include <string>
#include "scene/Transform.h"

class Mesh;
class Texture;

// Material ID type - use MaterialRegistry to convert to descriptor sets
using MaterialId = uint32_t;
constexpr MaterialId INVALID_MATERIAL_ID = ~0u;

// Forward declaration for PBRProperties
namespace ecs { struct PBRProperties; }

// A fully-configured renderable object - can only be created via RenderableBuilder
struct Renderable {
    glm::mat4 transform;
    Mesh* mesh;
    Texture* texture;  // For debug/inspection. Use materialId for rendering.
    MaterialId materialId = INVALID_MATERIAL_ID;  // Used for descriptor set lookup during rendering

    // PBR properties - consolidated from individual fields
    // Authoritative source is ECS PBRProperties component; these are for GPU upload
    float roughness = 0.5f;
    float metallic = 0.0f;
    float emissiveIntensity = 0.0f;
    glm::vec3 emissiveColor = glm::vec3(1.0f);
    uint32_t pbrFlags = 0;  // Bitmask indicating which PBR textures are bound
    float alphaTestThreshold = 0.0f;  // Alpha test threshold (0 = disabled)

    bool castsShadow = true;
    float opacity = 1.0f;  // For camera occlusion fading (1.0 = fully visible)

    // Tree-specific properties - authoritative source is ECS TreeData component
    // These fields exist for rendering system access; set via ECS components
    std::string barkType = "oak";  // Bark texture type for trees (oak, pine, birch, willow)
    std::string leafType = "oak";  // Leaf texture type for trees (oak, ash, aspen, pine)
    int leafInstanceIndex = -1;  // Index into TreeSystem::leafDrawInfoPerTree_ for instanced leaf rendering
    int treeInstanceIndex = -1;  // Index into treeInstances_ for LOD lookups
    glm::vec3 leafTint = glm::vec3(1.0f);  // Leaf color tint
    float autumnHueShift = 0.0f;  // Autumn hue shift (0=summer, 1=full autumn)

    float hueShift = 0.0f;  // General hue shift in radians (for NPC tinting)

    // Helper to set all PBR properties from ECS component
    void setPBRProperties(float r, float m, float ei, const glm::vec3& ec, float at, uint32_t flags) {
        roughness = r;
        metallic = m;
        emissiveIntensity = ei;
        emissiveColor = ec;
        alphaTestThreshold = at;
        pbrFlags = flags;
    }

    // Helper to set all tree properties from ECS TreeData component
    void setTreeProperties(const std::string& bark, const std::string& leaf,
                           int leafIdx, int treeIdx, const glm::vec3& tint, float autumn) {
        barkType = bark;
        leafType = leaf;
        leafInstanceIndex = leafIdx;
        treeInstanceIndex = treeIdx;
        leafTint = tint;
        autumnHueShift = autumn;
    }

    // Check if this renderable represents a tree
    [[nodiscard]] bool isTree() const {
        return treeInstanceIndex >= 0 || leafInstanceIndex >= 0;
    }

private:
    friend class RenderableBuilder;
    Renderable() = default;
};

// Builder class that ensures a Renderable cannot be created without required fields
class RenderableBuilder {
public:
    RenderableBuilder() = default;

    // Required: Set the mesh for this renderable
    RenderableBuilder& withMesh(Mesh* mesh);

    // Required: Set the texture for this renderable
    RenderableBuilder& withTexture(Texture* texture);

    // Optional: Set material ID (for MaterialRegistry-based rendering)
    RenderableBuilder& withMaterialId(MaterialId id);

    // Required: Set the world transform
    RenderableBuilder& withTransform(const glm::mat4& transform);
    RenderableBuilder& withTransform(const Transform& transform);

    // Optional: Set PBR roughness (default: 0.5)
    RenderableBuilder& withRoughness(float roughness);

    // Optional: Set PBR metallic (default: 0.0)
    RenderableBuilder& withMetallic(float metallic);

    // Optional: Set emissive intensity (default: 0.0, no emission)
    RenderableBuilder& withEmissiveIntensity(float intensity);

    // Optional: Set emissive color (default: white)
    RenderableBuilder& withEmissiveColor(const glm::vec3& color);

    // Optional: Set whether object casts shadows (default: true)
    RenderableBuilder& withCastsShadow(bool casts);

    // Optional: Set alpha test threshold (default: 0.0 = disabled)
    // Pixels with alpha < threshold will be discarded
    RenderableBuilder& withAlphaTest(float threshold);

    // Optional: Set PBR flags bitmask (indicates which PBR textures are bound)
    RenderableBuilder& withPBRFlags(uint32_t flags);

    // Optional: Set all PBR properties at once from ECS PBRProperties component
    // This is the preferred method - individual setters exist for backward compatibility
    RenderableBuilder& withPBRProperties(float roughness, float metallic,
                                          float emissiveIntensity, const glm::vec3& emissiveColor,
                                          float alphaTestThreshold, uint32_t pbrFlags);

    // Optional: Set bark texture type for trees (oak, pine, birch, willow)
    RenderableBuilder& withBarkType(const std::string& type);

    // Optional: Set leaf texture type for trees (oak, ash, aspen, pine)
    RenderableBuilder& withLeafType(const std::string& type);

    // Optional: Set leaf color tint
    RenderableBuilder& withLeafTint(const glm::vec3& tint);

    // Optional: Set autumn hue shift (0=summer green, 1=full autumn colors)
    RenderableBuilder& withAutumnHueShift(float shift);

    // Optional: Set hue shift in radians (for NPC tinting, 0 to 2*PI)
    RenderableBuilder& withHueShift(float shift);

    // Optional: Set tree instance index for LOD lookups
    RenderableBuilder& withTreeInstanceIndex(int index);

    // Optional: Set leaf instance index for instanced leaf rendering
    RenderableBuilder& withLeafInstanceIndex(int index);

    // Optional: Set all tree properties at once from ECS TreeData component
    // This is the preferred method for trees - individual setters exist for backward compatibility
    RenderableBuilder& withTreeData(const std::string& barkType, const std::string& leafType,
                                     int leafInstanceIndex, int treeInstanceIndex,
                                     const glm::vec3& leafTint, float autumnHueShift);

    // Convenience: Set position only (creates translation matrix)
    RenderableBuilder& atPosition(const glm::vec3& position);

    // Build the renderable - asserts if required fields are missing
    // Returns a fully configured Renderable
    Renderable build() const;

    // Check if all required fields are set
    bool isValid() const;

private:
    std::optional<glm::mat4> transform_;
    Mesh* mesh_ = nullptr;
    Texture* texture_ = nullptr;
    MaterialId materialId_ = INVALID_MATERIAL_ID;
    float roughness_ = 0.5f;
    float metallic_ = 0.0f;
    float emissiveIntensity_ = 0.0f;
    glm::vec3 emissiveColor_ = glm::vec3(1.0f);
    uint32_t pbrFlags_ = 0;
    float alphaTestThreshold_ = 0.0f;
    bool castsShadow_ = true;
    std::string barkType_ = "oak";
    std::string leafType_ = "oak";
    int treeInstanceIndex_ = -1;
    int leafInstanceIndex_ = -1;
    glm::vec3 leafTint_ = glm::vec3(1.0f);
    float autumnHueShift_ = 0.0f;
    float hueShift_ = 0.0f;
};
