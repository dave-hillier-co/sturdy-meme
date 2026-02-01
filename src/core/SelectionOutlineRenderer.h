#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <optional>

#include "ecs/World.h"
#include "ecs/Components.h"
#include "ecs/ECSMaterialDemo.h"

class Mesh;
class IDescriptorAllocator;

// =============================================================================
// Selection Outline Renderer
// =============================================================================
// Renders selection outlines around entities with SelectionOutline component.
// Uses vertex extrusion technique for screen-space consistent outlines.
//
// The renderer operates in two passes:
// 1. Stencil write pass: Write to stencil buffer for selected objects
// 2. Outline pass: Draw extruded geometry where stencil != 1
//
// For simplicity, this implementation uses a single-pass extrusion approach:
// - Render back-faces with scaled/extruded vertices
// - Depth test against scene depth buffer
// - Results in a solid outline around occluded portions

// Push constants matching selection_outline.vert.glsl
struct OutlinePushConstants {
    glm::mat4 model;        // offset 0, size 64
    glm::vec4 outlineColor; // offset 64, size 16 (rgb = color, a = alpha)
    float outlineThickness; // offset 80, size 4
    float pulseSpeed;       // offset 84, size 4
    float _pad0;            // offset 88, size 4
    float _pad1;            // offset 92, size 4
};

static_assert(sizeof(OutlinePushConstants) == 96, "OutlinePushConstants must be 96 bytes");

class SelectionOutlineRenderer {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit SelectionOutlineRenderer(ConstructToken) {}

    struct InitInfo {
        const vk::raii::Device* raiiDevice = nullptr;
        vk::Device device;
        vk::PhysicalDevice physicalDevice;
        VmaAllocator allocator;
        vk::RenderPass renderPass;       // HDR render pass (or dedicated outline pass)
        IDescriptorAllocator* descriptorAllocator = nullptr;
        vk::Extent2D extent;
        uint32_t maxFramesInFlight;
        std::string resourcePath;
    };

    static std::unique_ptr<SelectionOutlineRenderer> create(const InitInfo& info);
    ~SelectionOutlineRenderer();

    // Non-copyable, non-movable
    SelectionOutlineRenderer(const SelectionOutlineRenderer&) = delete;
    SelectionOutlineRenderer& operator=(const SelectionOutlineRenderer&) = delete;
    SelectionOutlineRenderer(SelectionOutlineRenderer&&) = delete;
    SelectionOutlineRenderer& operator=(SelectionOutlineRenderer&&) = delete;

    // Update descriptor set with global UBO (call before rendering)
    void updateDescriptorSet(uint32_t frameIndex, vk::Buffer globalUBO);

    // Render selection outlines for entities with SelectionOutline component
    // Must be called within an active render pass
    void render(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                const ecs::World& world);

    // Render specific entities with outlines
    void renderEntities(vk::CommandBuffer cmd, uint32_t frameIndex, float time,
                        const std::vector<ecs::SelectionRenderData>& entities,
                        const ecs::World& world);

    // Update extent on resize
    void setExtent(vk::Extent2D newExtent) { extent_ = newExtent; }

    // Get pipeline layout for descriptor binding
    vk::PipelineLayout getPipelineLayout() const;

private:
    bool initInternal(const InitInfo& info);
    bool createPipeline(const InitInfo& info);
    bool createDescriptorSetLayout();
    bool allocateDescriptorSets(uint32_t maxFramesInFlight);

    const vk::raii::Device* raiiDevice_ = nullptr;
    vk::Device device_;
    vk::PhysicalDevice physicalDevice_;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    IDescriptorAllocator* descriptorAllocator_ = nullptr;
    std::string resourcePath_;
    vk::Extent2D extent_{};
    uint32_t maxFramesInFlight_ = 0;

    // Pipeline
    std::optional<vk::raii::Pipeline> pipeline_;
    std::optional<vk::raii::PipelineLayout> pipelineLayout_;

    // Descriptor sets
    std::optional<vk::raii::DescriptorSetLayout> descriptorSetLayout_;
    std::vector<vk::DescriptorSet> descriptorSets_;
};
