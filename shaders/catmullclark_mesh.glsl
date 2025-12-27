// Catmull-Clark mesh accessor functions

#include "bindings.glsl"

// Mesh data structures (matching CPU-side std430 layout)
struct CCVertex {
    vec3 position;
    float _pad0;       // Padding to align normal to 16 bytes
    vec3 normal;
    float _pad1;       // Padding after normal
    vec2 uv;
    vec2 _pad2;        // Padding to make struct 48 bytes
};

struct CCHalfedge {
    uint vertexID;
    uint nextID;
    uint twinID;
    uint faceID;
};

struct CCFace {
    uint halfedgeID;
    uint valence;
};

// Storage buffers (std430 layout for correct struct alignment)
layout(std430, binding = BINDING_CC_VERTEX_BUFFER) readonly buffer VertexBuffer {
    CCVertex vertices[];
};

layout(std430, binding = BINDING_CC_HALFEDGE_BUFFER) readonly buffer HalfedgeBuffer {
    CCHalfedge halfedges[];
};

layout(std430, binding = BINDING_CC_FACE_BUFFER) readonly buffer FaceBuffer {
    CCFace faces[];
};

// Mesh query functions
int ccm_HalfedgeCount() {
    return halfedges.length();
}

int ccm_HalfedgeNextID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    return int(halfedges[halfedgeID].nextID);
}

int ccm_HalfedgePrevID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    // Walk around the face to find previous
    int current = halfedgeID;
    int next = ccm_HalfedgeNextID(current);
    while (next != halfedgeID && next >= 0) {
        current = next;
        next = ccm_HalfedgeNextID(current);
    }
    return current;
}

int ccm_HalfedgeTwinID(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return -1;
    }
    uint twinID = halfedges[halfedgeID].twinID;
    if (twinID == 0xFFFFFFFFu) {
        return -1;  // Boundary edge
    }
    return int(twinID);
}

// Get vertex position from halfedge
vec3 ccm_HalfedgeVertexPosition(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec3(0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec3(0.0);
    }
    return vertices[vertexID].position;
}

// Get vertex normal from halfedge
vec3 ccm_HalfedgeVertexNormal(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec3(0.0, 1.0, 0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec3(0.0, 1.0, 0.0);
    }
    return vertices[vertexID].normal;
}

// Get vertex UV from halfedge
vec2 ccm_HalfedgeVertexUV(int halfedgeID) {
    if (halfedgeID < 0 || halfedgeID >= halfedges.length()) {
        return vec2(0.0);
    }
    uint vertexID = halfedges[halfedgeID].vertexID;
    if (vertexID >= vertices.length()) {
        return vec2(0.0);
    }
    return vertices[vertexID].uv;
}

// Catmull-Clark settings
int ccs_MaxDepth() {
    return 16;  // Will be passed as uniform/constant
}
