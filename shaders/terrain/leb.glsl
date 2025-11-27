/* leb.glsl - Longest Edge Bisection library for Vulkan GLSL
 * Based on Jonathan Dupuy's libleb
 * Adapted for Vulkan/SPIR-V
 *
 * Requires cbt.glsl to be included first
 */

// Data structures
struct leb_DiamondParent {
    cbt_Node base, top;
};

// Forward declarations
leb_DiamondParent leb_DecodeDiamondParent(in const cbt_Node node);
leb_DiamondParent leb_DecodeDiamondParent_Square(in const cbt_Node node);
void leb_SplitNode(in const cbt_Node node);
void leb_SplitNode_Square(in const cbt_Node node);
void leb_MergeNode(in const cbt_Node node, in const leb_DiamondParent diamond);
void leb_MergeNode_Square(in const cbt_Node node, in const leb_DiamondParent diamond);

vec3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray_mat3(in const cbt_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray_mat4(in const cbt_Node node, in const mat4x3 data);

vec3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const vec3 data);
mat2x3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const mat2x3 data);
mat3x3 leb_DecodeNodeAttributeArray_Square_mat3(in const cbt_Node node, in const mat3x3 data);
mat4x3 leb_DecodeNodeAttributeArray_Square_mat4(in const cbt_Node node, in const mat4x3 data);

// -----------------------------------------------------------------------------
// Implementation
// -----------------------------------------------------------------------------

struct leb__SameDepthNeighborIDs {
    uint left, right, edge, node;
};

leb__SameDepthNeighborIDs leb__CreateSameDepthNeighborIDs(uint left, uint right, uint edge, uint node) {
    leb__SameDepthNeighborIDs neighborIDs;
    neighborIDs.left = left;
    neighborIDs.right = right;
    neighborIDs.edge = edge;
    neighborIDs.node = node;
    return neighborIDs;
}

leb_DiamondParent leb__CreateDiamondParent(in const cbt_Node base, in const cbt_Node top) {
    leb_DiamondParent diamond;
    diamond.base = base;
    diamond.top = top;
    return diamond;
}

uint leb__GetBitValue(const uint bitField, int bitID) {
    return ((bitField >> bitID) & 1u);
}

// Branchless version of split node IDs update
leb__SameDepthNeighborIDs leb__SplitNodeIDs(in const leb__SameDepthNeighborIDs nodeIDs, uint splitBit) {
    uint b = splitBit;
    uint c = splitBit ^ 1u;
    bool cb = bool(c);
    uvec4 idArray = uvec4(nodeIDs.left, nodeIDs.right, nodeIDs.edge, nodeIDs.node);
    return leb__CreateSameDepthNeighborIDs(
        (idArray[2 + b] << 1u) | uint(cb && bool(idArray[2 + b])),
        (idArray[2 + c] << 1u) | uint(cb && bool(idArray[2 + c])),
        (idArray[b    ] << 1u) | uint(cb && bool(idArray[b    ])),
        (idArray[3    ] << 1u) | b
    );
}

leb__SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs(in const cbt_Node node) {
    leb__SameDepthNeighborIDs nodeIDs = leb__CreateSameDepthNeighborIDs(0u, 0u, 0u, 1u);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}

leb__SameDepthNeighborIDs leb_DecodeSameDepthNeighborIDs_Square(in const cbt_Node node) {
    uint b = leb__GetBitValue(node.id, max(0, node.depth - 1));
    leb__SameDepthNeighborIDs nodeIDs = leb__CreateSameDepthNeighborIDs(0u, 0u, 3u - b, 2u + b);

    for (int bitID = node.depth - 2; bitID >= 0; --bitID) {
        nodeIDs = leb__SplitNodeIDs(nodeIDs, leb__GetBitValue(node.id, bitID));
    }

    return nodeIDs;
}

cbt_Node leb__EdgeNeighbor(in const cbt_Node node) {
    uint nodeID = leb_DecodeSameDepthNeighborIDs(node).edge;
    return cbt_CreateNode_Explicit(nodeID, (nodeID == 0u) ? 0 : node.depth);
}

cbt_Node leb__EdgeNeighbor_Square(in const cbt_Node node) {
    uint nodeID = leb_DecodeSameDepthNeighborIDs_Square(node).edge;
    return cbt_CreateNode_Explicit(nodeID, (nodeID == 0u) ? 0 : node.depth);
}

void leb_SplitNode(in const cbt_Node node) {
    if (!cbt_IsCeilNode(node)) {
        const uint minNodeID = 1u;
        cbt_Node nodeIterator = node;

        cbt_SplitNode(nodeIterator);
        nodeIterator = leb__EdgeNeighbor(nodeIterator);

        while (nodeIterator.id > minNodeID) {
            cbt_SplitNode(nodeIterator);
            nodeIterator = cbt_ParentNode_Fast(nodeIterator);
            cbt_SplitNode(nodeIterator);
            nodeIterator = leb__EdgeNeighbor(nodeIterator);
        }
    }
}

void leb_SplitNode_Square(in const cbt_Node node) {
    if (!cbt_IsCeilNode(node)) {
        const uint minNodeID = 1u;
        cbt_Node nodeIterator = node;

        cbt_SplitNode(nodeIterator);
        nodeIterator = leb__EdgeNeighbor_Square(nodeIterator);

        while (nodeIterator.id > minNodeID) {
            cbt_SplitNode(nodeIterator);
            nodeIterator = cbt_ParentNode_Fast(nodeIterator);

            if (nodeIterator.id > minNodeID) {
                cbt_SplitNode(nodeIterator);
                nodeIterator = leb__EdgeNeighbor_Square(nodeIterator);
            }
        }
    }
}

leb_DiamondParent leb_DecodeDiamondParent(in const cbt_Node node) {
    cbt_Node parentNode = cbt_ParentNode_Fast(node);
    uint edgeNeighborID = leb_DecodeSameDepthNeighborIDs(parentNode).edge;
    cbt_Node edgeNeighborNode = cbt_CreateNode_Explicit(
        edgeNeighborID > 0u ? edgeNeighborID : parentNode.id,
        parentNode.depth
    );

    return leb__CreateDiamondParent(parentNode, edgeNeighborNode);
}

leb_DiamondParent leb_DecodeDiamondParent_Square(in const cbt_Node node) {
    cbt_Node parentNode = cbt_ParentNode_Fast(node);
    uint edgeNeighborID = leb_DecodeSameDepthNeighborIDs_Square(parentNode).edge;
    cbt_Node edgeNeighborNode = cbt_CreateNode_Explicit(
        edgeNeighborID > 0u ? edgeNeighborID : parentNode.id,
        parentNode.depth
    );

    return leb__CreateDiamondParent(parentNode, edgeNeighborNode);
}

bool leb__HasDiamondParent(in const leb_DiamondParent diamondParent) {
    bool canMergeBase = cbt_HeapRead(diamondParent.base) <= 2u;
    bool canMergeTop  = cbt_HeapRead(diamondParent.top) <= 2u;
    return canMergeBase && canMergeTop;
}

void leb_MergeNode(in const cbt_Node node, in const leb_DiamondParent diamondParent) {
    if (!cbt_IsRootNode(node) && leb__HasDiamondParent(diamondParent)) {
        cbt_MergeNode(node);
    }
}

void leb_MergeNode_Square(in const cbt_Node node, in const leb_DiamondParent diamondParent) {
    if ((node.depth > 1) && leb__HasDiamondParent(diamondParent)) {
        cbt_MergeNode(node);
    }
}

// Splitting matrix for LEB
mat3 leb__SplittingMatrix(uint splitBit) {
    float b = float(splitBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c   , b   , 0.0f,
        0.5f, 0.0f, 0.5f,
        0.0f,    c,    b
    ));
}

// Square mapping matrix
mat3 leb__SquareMatrix(uint quadBit) {
    float b = float(quadBit);
    float c = 1.0f - b;

    return transpose(mat3(
        c, 0.0f, b,
        b, c   , b,
        b, 0.0f, c
    ));
}

// Winding correction matrix
mat3 leb__WindingMatrix(uint mirrorBit) {
    float b = float(mirrorBit);
    float c = 1.0f - b;

    return mat3(
        c, 0.0f, b,
        0, 1.0f, 0,
        b, 0.0f, c
    );
}

mat3 leb__DecodeTransformationMatrix(in const cbt_Node node) {
    mat3 xf = mat3(1.0f);

    for (int bitID = node.depth - 1; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    return leb__WindingMatrix(uint(node.depth) & 1u) * xf;
}

mat3 leb__DecodeTransformationMatrix_Square(in const cbt_Node node) {
    int bitID = max(0, node.depth - 1);
    mat3 xf = leb__SquareMatrix(leb__GetBitValue(node.id, bitID));

    for (bitID = node.depth - 2; bitID >= 0; --bitID) {
        xf = leb__SplittingMatrix(leb__GetBitValue(node.id, bitID)) * xf;
    }

    return leb__WindingMatrix((uint(node.depth) ^ 1u) & 1u) * xf;
}

vec3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const vec3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray(in const cbt_Node node, in const mat2x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray_mat3(in const cbt_Node node, in const mat3x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray_mat4(in const cbt_Node node, in const mat4x3 data) {
    return leb__DecodeTransformationMatrix(node) * data;
}

vec3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const vec3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat2x3 leb_DecodeNodeAttributeArray_Square(in const cbt_Node node, in const mat2x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat3x3 leb_DecodeNodeAttributeArray_Square_mat3(in const cbt_Node node, in const mat3x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

mat4x3 leb_DecodeNodeAttributeArray_Square_mat4(in const cbt_Node node, in const mat4x3 data) {
    return leb__DecodeTransformationMatrix_Square(node) * data;
}

// Utility function to decode triangle vertices from a node
// Returns triangle vertices in [0,1]^2 UV space with consistent CCW winding
void leb_DecodeTriangleVertices(in const cbt_Node node, out vec2 v0, out vec2 v1, out vec2 v2) {
    // Determine which half of the unit square this triangle belongs to.
    // The most significant bit selects the base triangle that covers either the
    // lower-left or upper-right half of the square.
    uint quadBit = (node.depth == 0) ? 0u : leb__GetBitValue(node.id, node.depth - 1);
    if (quadBit == 0u) {
        v0 = vec2(0.0, 0.0);
        v1 = vec2(1.0, 0.0);
        v2 = vec2(0.0, 1.0);
    } else {
        v0 = vec2(1.0, 1.0);
        v1 = vec2(0.0, 1.0);
        v2 = vec2(1.0, 0.0);
    }

    // Walk down the tree from the second most significant bit, bisecting the
    // current triangle's longest edge (v1-v2) at each step.
    for (int bitID = node.depth - 2; bitID >= 0; --bitID) {
        vec2 mid = 0.5 * (v1 + v2);
        if (leb__GetBitValue(node.id, bitID) == 0u) {
            v2 = mid;
        } else {
            v1 = mid;
        }
    }

    // Reorder vertices so the longest edge is always v1-v2 (matches LEB
    // assumptions) while keeping counter-clockwise winding.
    float e01 = length(v0 - v1);
    float e12 = length(v1 - v2);
    float e20 = length(v2 - v0);

    if (e01 >= e12 && e01 >= e20) {
        vec2 tmp = v2; v2 = v1; v1 = v0; v0 = tmp;
    } else if (e20 >= e12 && e20 >= e01) {
        vec2 tmp = v1; v1 = v2; v2 = v0; v0 = tmp;
    }

    float area = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (area < 0.0) {
        vec2 tmp = v1; v1 = v2; v2 = tmp;
    }
}
