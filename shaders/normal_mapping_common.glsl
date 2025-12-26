/*
 * normal_mapping_common.glsl - Common normal mapping utilities
 *
 * Provides TBN matrix construction and normal map perturbation.
 * Used by shader.frag, tree.frag for consistent normal mapping.
 */

#ifndef NORMAL_MAPPING_COMMON_GLSL
#define NORMAL_MAPPING_COMMON_GLSL

// Build TBN matrix from vertex normal and tangent
// tangent.w contains the handedness for the bitangent
mat3 buildTBN(vec3 N, vec4 tangent) {
    vec3 T = normalize(tangent.xyz);
    // Re-orthogonalize T with respect to N (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    // Compute bitangent using handedness stored in tangent.w
    vec3 B = cross(N, T) * tangent.w;
    return mat3(T, B, N);
}

// Apply normal map to perturb the surface normal
// N: normalized vertex normal
// tangent: vertex tangent with handedness in w component
// normalMapSample: raw sample from normal map texture (RGB)
vec3 perturbNormalFromSample(vec3 N, vec4 tangent, vec3 normalMapSample) {
    mat3 TBN = buildTBN(N, tangent);

    // Convert from [0,1] to [-1,1] range
    vec3 tangentNormal = normalMapSample * 2.0 - 1.0;

    return normalize(TBN * tangentNormal);
}

// Apply normal map using a sampler directly
// N: normalized vertex normal
// tangent: vertex tangent with handedness in w component
// normalMap: normal map sampler
// texcoord: texture coordinates for sampling
#define perturbNormal(N, tangent, normalMap, texcoord) \
    perturbNormalFromSample(N, tangent, texture(normalMap, texcoord).rgb)

#endif // NORMAL_MAPPING_COMMON_GLSL
