#include "RenderableBuilder.h"
#include <SDL3/SDL_log.h>

RenderableBuilder& RenderableBuilder::withMesh(Mesh* mesh) {
    mesh_ = mesh;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTexture(Texture* texture) {
    texture_ = texture;
    return *this;
}

RenderableBuilder& RenderableBuilder::withMaterialId(MaterialId id) {
    materialId_ = id;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTransform(const glm::mat4& transform) {
    transform_ = transform;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTransform(const Transform& transform) {
    transform_ = transform.toMatrix();
    return *this;
}

RenderableBuilder& RenderableBuilder::withRoughness(float roughness) {
    roughness_ = roughness;
    return *this;
}

RenderableBuilder& RenderableBuilder::withMetallic(float metallic) {
    metallic_ = metallic;
    return *this;
}

RenderableBuilder& RenderableBuilder::withEmissiveIntensity(float intensity) {
    emissiveIntensity_ = intensity;
    return *this;
}

RenderableBuilder& RenderableBuilder::withEmissiveColor(const glm::vec3& color) {
    emissiveColor_ = color;
    return *this;
}

RenderableBuilder& RenderableBuilder::withCastsShadow(bool casts) {
    castsShadow_ = casts;
    return *this;
}

RenderableBuilder& RenderableBuilder::withAlphaTest(float threshold) {
    alphaTestThreshold_ = threshold;
    return *this;
}

RenderableBuilder& RenderableBuilder::withPBRFlags(uint32_t flags) {
    pbrFlags_ = flags;
    return *this;
}

RenderableBuilder& RenderableBuilder::withPBRProperties(float roughness, float metallic,
                                                         float emissiveIntensity, const glm::vec3& emissiveColor,
                                                         float alphaTestThreshold, uint32_t pbrFlags) {
    roughness_ = roughness;
    metallic_ = metallic;
    emissiveIntensity_ = emissiveIntensity;
    emissiveColor_ = emissiveColor;
    alphaTestThreshold_ = alphaTestThreshold;
    pbrFlags_ = pbrFlags;
    return *this;
}

RenderableBuilder& RenderableBuilder::withBarkType(const std::string& type) {
    barkType_ = type;
    return *this;
}

RenderableBuilder& RenderableBuilder::withLeafType(const std::string& type) {
    leafType_ = type;
    return *this;
}

RenderableBuilder& RenderableBuilder::withLeafTint(const glm::vec3& tint) {
    leafTint_ = tint;
    return *this;
}

RenderableBuilder& RenderableBuilder::withAutumnHueShift(float shift) {
    autumnHueShift_ = shift;
    return *this;
}

RenderableBuilder& RenderableBuilder::withHueShift(float shift) {
    hueShift_ = shift;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTreeInstanceIndex(int index) {
    treeInstanceIndex_ = index;
    return *this;
}

RenderableBuilder& RenderableBuilder::withLeafInstanceIndex(int index) {
    leafInstanceIndex_ = index;
    return *this;
}

RenderableBuilder& RenderableBuilder::withTreeData(const std::string& barkType, const std::string& leafType,
                                                    int leafInstanceIndex, int treeInstanceIndex,
                                                    const glm::vec3& leafTint, float autumnHueShift) {
    barkType_ = barkType;
    leafType_ = leafType;
    leafInstanceIndex_ = leafInstanceIndex;
    treeInstanceIndex_ = treeInstanceIndex;
    leafTint_ = leafTint;
    autumnHueShift_ = autumnHueShift;
    return *this;
}

RenderableBuilder& RenderableBuilder::atPosition(const glm::vec3& position) {
    transform_ = glm::translate(glm::mat4(1.0f), position);
    return *this;
}

bool RenderableBuilder::isValid() const {
    return mesh_ != nullptr && texture_ != nullptr && transform_.has_value();
}

Renderable RenderableBuilder::build() const {
    // Assert that all required fields are set
    if (!mesh_) {
        SDL_Log("RenderableBuilder::build() failed: mesh is required");
        assert(mesh_ != nullptr && "RenderableBuilder: mesh is required");
    }
    if (!texture_) {
        SDL_Log("RenderableBuilder::build() failed: texture is required");
        assert(texture_ != nullptr && "RenderableBuilder: texture is required");
    }
    if (!transform_.has_value()) {
        SDL_Log("RenderableBuilder::build() failed: transform is required");
        assert(transform_.has_value() && "RenderableBuilder: transform is required");
    }

    Renderable renderable;
    renderable.transform = transform_.value();
    renderable.mesh = mesh_;
    renderable.texture = texture_;
    renderable.materialId = materialId_;
    renderable.roughness = roughness_;
    renderable.metallic = metallic_;
    renderable.emissiveIntensity = emissiveIntensity_;
    renderable.emissiveColor = emissiveColor_;
    renderable.pbrFlags = pbrFlags_;
    renderable.alphaTestThreshold = alphaTestThreshold_;
    renderable.castsShadow = castsShadow_;
    renderable.barkType = barkType_;
    renderable.leafType = leafType_;
    renderable.treeInstanceIndex = treeInstanceIndex_;
    renderable.leafInstanceIndex = leafInstanceIndex_;
    renderable.leafTint = leafTint_;
    renderable.autumnHueShift = autumnHueShift_;
    renderable.hueShift = hueShift_;

    return renderable;
}
