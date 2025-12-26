/*
 * dither_common.glsl - Common dithering utilities
 *
 * Provides Bayer matrix for LOD transitions and transparency dithering.
 * Used by tree.frag, tree_impostor.frag, tree_leaf.frag for smooth LOD blending.
 */

#ifndef DITHER_COMMON_GLSL
#define DITHER_COMMON_GLSL

// 4x4 Bayer dither matrix for ordered dithering
// Values normalized to [0, 1) range
const float bayerMatrix[16] = float[16](
    0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0,
    12.0/16.0, 4.0/16.0, 14.0/16.0,  6.0/16.0,
    3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0,
    15.0/16.0, 7.0/16.0, 13.0/16.0,  5.0/16.0
);

// Get dither threshold for current fragment
// Use with: if (blendFactor > getDitherThreshold()) discard;
float getDitherThreshold(ivec2 pixelCoord) {
    int ditherIndex = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
    return bayerMatrix[ditherIndex];
}

// Convenience overload using gl_FragCoord
float getDitherThreshold() {
    return getDitherThreshold(ivec2(gl_FragCoord.xy));
}

// Returns true if fragment should be discarded for LOD fade-out
// blendFactor: 0 = fully visible, 1 = fully faded out
bool shouldDiscardForLOD(float blendFactor) {
    if (blendFactor < 0.01) return false;
    return blendFactor > getDitherThreshold();
}

// Returns true if fragment should be discarded for LOD fade-in
// blendFactor: 0 = fully faded out, 1 = fully visible
bool shouldDiscardForLODFadeIn(float blendFactor) {
    if (blendFactor > 0.99) return false;
    return blendFactor < getDitherThreshold();
}

#endif // DITHER_COMMON_GLSL
