#pragma once

#include <glm/glm.hpp>
#include <cstdint>

// Uniforms for cell culling compute shader
struct TreeCellCullUniforms {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float maxDrawDistance;
    uint32_t numCells;
    uint32_t treesPerWorkgroup;
    uint32_t _pad0;
};

// Uniforms for tree filter compute shader
struct TreeFilterUniforms {
    glm::vec4 cameraPosition;
    glm::vec4 frustumPlanes[6];
    float maxDrawDistance;
    uint32_t maxTreesPerCell;
    uint32_t _pad0;
    uint32_t _pad1;
};

// Params structs for shader-specific data (separate from shared CullingUniforms)
struct CellCullParams {
    uint32_t numCells;
    uint32_t treesPerWorkgroup;
    uint32_t _pad0;
    uint32_t _pad1;
};

struct TreeFilterParams {
    uint32_t maxTreesPerCell;
    uint32_t maxVisibleTrees;  // Buffer capacity for bounds checking
    uint32_t _pad0;
    uint32_t _pad1;
};

// Params for phase 3 leaf culling (matches shader LeafCullP3Params)
struct LeafCullP3Params {
    uint32_t maxLeavesPerType;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};

// Per-tree culling data (stored in SSBO, one entry per tree)
struct TreeCullData {
    glm::mat4 treeModel;
    uint32_t inputFirstInstance;
    uint32_t inputInstanceCount;
    uint32_t treeIndex;
    uint32_t leafTypeIndex;
    float lodBlendFactor;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(TreeCullData) == 96, "TreeCullData must be 96 bytes for std430 layout");

// Visible tree data (output from tree filtering, input to leaf culling)
struct VisibleTreeData {
    uint32_t originalTreeIndex;
    uint32_t leafFirstInstance;
    uint32_t leafInstanceCount;
    uint32_t leafTypeIndex;
    float lodBlendFactor;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(VisibleTreeData) == 32, "VisibleTreeData must be 32 bytes for std430 layout");

// World-space leaf instance data (output from compute, input to vertex shader)
struct WorldLeafInstanceGPU {
    glm::vec4 worldPosition;
    glm::vec4 worldOrientation;
    uint32_t treeIndex;
    uint32_t _pad0;
    uint32_t _pad1;
    uint32_t _pad2;
};
static_assert(sizeof(WorldLeafInstanceGPU) == 48, "WorldLeafInstanceGPU must be 48 bytes for std430 layout");

// Per-tree render data (stored in SSBO, indexed by treeIndex in vertex shader)
struct TreeRenderDataGPU {
    glm::mat4 model;
    glm::vec4 tintAndParams;
    glm::vec4 windOffsetAndLOD;
};
