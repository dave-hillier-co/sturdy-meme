#pragma once

#include <glm/glm.hpp>
#include <array>
#include <cstdint>

// Algorithm selection
enum class TreeAlgorithm {
    Recursive,          // Original recursive branching algorithm
    SpaceColonisation   // Space colonisation algorithm
};

// Volume shapes for space colonisation
enum class VolumeShape {
    Sphere,
    Hemisphere,
    Cone,
    Cylinder,
    Ellipsoid,
    Box
};

// Bark texture types (matches ez-tree)
enum class BarkType {
    Oak = 0,
    Birch = 1,
    Pine = 2,
    Willow = 3
};

// Leaf texture types (matches ez-tree)
enum class LeafType {
    Oak = 0,
    Ash = 1,
    Aspen = 2,
    Pine = 3
};

// Billboard rendering mode for leaves
enum class BillboardMode {
    Single,     // Single-sided quad
    Double      // Two perpendicular quads for 3D effect
};

// Tree type (affects terminal branch behavior)
enum class TreeType {
    Deciduous,  // Terminal branch extends from parent end
    Evergreen   // No terminal branch, cone-like shape
};

// Per-level branch parameters (ez-tree style)
struct BranchLevelParams {
    float angle = 60.0f;        // Angle from parent branch (degrees)
    int children = 5;           // Number of child branches
    float gnarliness = 0.1f;    // Random twist/curve amount (radians, keep small!)
    float length = 10.0f;       // Length of branches at this level
    float radius = 0.7f;        // Radius at this level
    int sections = 8;           // Segments along branch length
    int segments = 6;           // Radial segments
    float start = 0.3f;         // Where children start on parent (0-1)
    float taper = 0.7f;         // Taper from base to tip (0-1)
    float twist = 0.0f;         // Axial twist amount
};

// Space colonisation specific parameters (scaled for meters)
struct SpaceColonisationParams {
    // Crown volume shape and size
    VolumeShape crownShape = VolumeShape::Sphere;
    float crownRadius = 3.0f;           // Base radius of crown volume (~3m)
    float crownHeight = 4.0f;           // Height for non-spherical shapes
    glm::vec3 crownOffset = glm::vec3(0.0f, 0.0f, 0.0f);  // Offset from trunk top
    glm::vec3 crownScale = glm::vec3(1.0f, 1.0f, 1.0f);   // For ellipsoid scaling
    float crownExclusionRadius = 0.0f;  // Inner hollow zone (no points here)

    // Attraction point parameters
    int attractionPointCount = 500;     // Number of attraction points
    bool uniformDistribution = true;    // Uniform vs clustered distribution

    // Algorithm parameters
    float attractionDistance = 2.0f;    // Max distance for point to influence node (di)
    float killDistance = 0.3f;          // Distance at which point is removed (dk)
    float segmentLength = 0.2f;         // Length of each growth step (D)
    int maxIterations = 200;            // Safety limit for iterations
    float branchAngleLimit = 45.0f;     // Max angle change per segment (degrees)

    // Tropism (directional growth influence)
    glm::vec3 tropismDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float tropismStrength = 0.1f;       // How much tropism affects growth

    // Initial trunk before branching
    int trunkSegments = 3;              // Number of trunk segments before crown
    float trunkHeight = 2.5f;           // Height of trunk before crown starts (~2.5m)

    // Root system
    bool generateRoots = false;
    VolumeShape rootShape = VolumeShape::Hemisphere;
    float rootRadius = 1.5f;
    float rootDepth = 1.0f;
    int rootAttractionPointCount = 200;
    float rootTropismStrength = 0.3f;   // Downward tropism for roots

    // Branch thickness calculation
    float baseThickness = 0.2f;         // Trunk base thickness (~20cm)
    float thicknessPower = 2.0f;        // Exponent for pipe model (da Vinci's rule)
    float minThickness = 0.01f;         // Minimum branch thickness (~1cm)

    // Geometry quality settings
    int radialSegments = 8;             // Segments around circumference
    int curveSubdivisions = 3;          // Subdivisions per branch for smooth curves
    float smoothingStrength = 0.5f;     // How much to smooth branch curves (0-1)
};

// Tree generation parameters similar to ez-tree
struct TreeParameters {
    // Algorithm selection
    TreeAlgorithm algorithm = TreeAlgorithm::Recursive;

    // Space colonisation specific parameters
    SpaceColonisationParams spaceColonisation;

    // Seed for reproducibility
    uint32_t seed = 12345;

    // Tree type
    TreeType treeType = TreeType::Deciduous;

    // Bark parameters
    BarkType barkType = BarkType::Oak;
    glm::vec3 barkTint = glm::vec3(1.0f);       // Tint color for bark
    bool barkTextured = true;                    // Whether to apply bark texture
    glm::vec2 barkTextureScale = glm::vec2(1.0f); // UV scale for bark texture
    bool barkFlatShading = false;                // Use flat shading for bark

    // Per-level branch parameters (ez-tree style, levels 0-3)
    // Based on oak_medium.json preset (lengths/radius scaled by 0.3)
    // Level 0 radius is absolute, level 1-3 radius are multipliers on parent
    std::array<BranchLevelParams, 4> branchParams = {{
        //   angle, children, gnarliness, length, radius, sections, segments, start, taper, twist
        { 0.0f,  6, 0.0f,  11.17f, 0.423f, 8, 7, 0.0f,  0.73f, -0.23f },  // trunk (37.24 * 0.3)
        { 54.0f, 4, 0.1f,  3.32f,  0.9f,   6, 5, 0.49f, 0.42f, 0.42f },   // level 1 (11.08 * 0.3)
        { 58.0f, 3, 0.15f, 3.72f,  0.69f,  3, 3, 0.06f, 0.69f, 0.0f },    // level 2 (12.39 * 0.3)
        { 32.0f, 0, 0.09f, 2.15f,  1.19f,  1, 3, 0.12f, 0.75f, 0.0f }     // level 3 (7.16 * 0.3)
    }};

    // Number of branch recursion levels (0-3)
    int branchLevels = 3;

    // Growth direction influence (external force)
    // oak_medium: direction (0,1,0), strength -0.01
    glm::vec3 growthDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float growthInfluence = -0.01f;      // Negative creates natural drooping

    // Leaf parameters (from ez-tree options.js defaults)
    LeafType leafType = LeafType::Oak;
    glm::vec3 leafTint = glm::vec3(1.0f);
    BillboardMode leafBillboard = BillboardMode::Double;
    float leafAlphaTest = 0.5f;
    bool generateLeaves = true;
    float leafSize = 0.75f;              // ez-tree 2.5 at 0.3 scale
    float leafSizeVariance = 0.7f;
    int leavesPerBranch = 1;
    float leafAngle = 10.0f;
    float leafStart = 0.0f;
    int leafStartLevel = 2;
};
