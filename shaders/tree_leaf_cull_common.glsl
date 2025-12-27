// Common leaf culling functions and constants shared between:
// - tree_leaf_cull.comp (global dispatch, one thread per leaf)
// - tree_leaf_cull_phase3.comp (per-tree dispatch, one workgroup per visible tree)

#ifndef TREE_LEAF_CULL_COMMON_GLSL
#define TREE_LEAF_CULL_COMMON_GLSL

// Number of leaf types (oak=0, ash=1, aspen=2, pine=3)
const uint NUM_LEAF_TYPES = 4;

// Per-tree culling data structure (shared layout)
struct TreeCullData {
    mat4 treeModel;                // Tree's model matrix
    uint inputFirstInstance;       // Offset into inputInstances for this tree
    uint inputInstanceCount;       // Number of input instances for this tree
    uint treeIndex;                // Index of this tree (for render data lookup)
    uint leafTypeIndex;            // Leaf type (0=oak, 1=ash, 2=aspen, 3=pine)
    float lodBlendFactor;          // LOD blend factor (0=full detail, 1=full impostor)
    uint _pad0;                    // Padding for std430 alignment
    uint _pad1;
    uint _pad2;
};

// Debug flag bit positions (must match TreeLeafCullUniforms in C++)
const uint DEBUG_DISABLE_FRUSTUM_CULLING = 1u;
const uint DEBUG_DISABLE_DISTANCE_CULLING = 2u;
const uint DEBUG_DISABLE_LOD_DROPPING = 4u;
const uint DEBUG_DISABLE_TIER_BUDGET = 8u;

// Cull a single leaf instance against frustum, distance, and LOD
// Returns true if the leaf should be CULLED (not rendered)
bool cullLeaf(
    vec3 leafLocalPos,
    float leafSizeLocal,
    mat4 treeModel,
    float lodBlendFactor,
    vec3 cameraPos,
    vec4 frustumPlanes[6],
    float maxDrawDistance,
    float lodTransitionStart,
    float lodTransitionEnd,
    float maxLodDropRate,
    uint debugFlags,
    out vec4 worldPos,
    out float leafSize,
    out float distToCamera
) {
    // Transform leaf position to world space
    worldPos = treeModel * vec4(leafLocalPos, 1.0);

    // Extract uniform scale from model matrix (assumes uniform scaling)
    float treeScale = length(treeModel[0].xyz);
    leafSize = leafSizeLocal * treeScale;

    // Distance culling (can be disabled for debugging)
    distToCamera = getDistanceToCamera(worldPos.xyz, cameraPos);
    if ((debugFlags & DEBUG_DISABLE_DISTANCE_CULLING) == 0u) {
        if (distToCamera > maxDrawDistance) {
            return true;
        }
    }

    // Frustum culling with margin for leaf billboard rotation (can be disabled for debugging)
    if ((debugFlags & DEBUG_DISABLE_FRUSTUM_CULLING) == 0u) {
        float margin = leafSize * 2.0;
        if (!isInFrustum(frustumPlanes, worldPos.xyz, margin)) {
            return true;
        }
    }

    // LOD blade dropping - use position-based hash for consistent results
    // Incorporate LOD blend factor for smooth transitions
    // The effectiveDropRate already includes lodBlendFactor to increase culling
    // as the tree transitions toward impostor mode (can be disabled for debugging)
    if ((debugFlags & DEBUG_DISABLE_LOD_DROPPING) == 0u) {
        float instanceHash = hash2D(leafLocalPos.xz);
        float effectiveDropRate = maxLodDropRate + lodBlendFactor * (1.0 - maxLodDropRate);
        if (lodCull(distToCamera, lodTransitionStart, lodTransitionEnd,
                    effectiveDropRate, instanceHash)) {
            return true;
        }
    }

    return false;
}

// Transform leaf orientation to world space
vec4 transformLeafOrientation(vec4 localOrientation, mat4 treeModel) {
    mat3 rotationMatrix = mat3(treeModel);
    vec4 treeRotQuat = mat3ToQuat(rotationMatrix);
    return quatMul(treeRotQuat, localOrientation);
}

#endif // TREE_LEAF_CULL_COMMON_GLSL
