// Damage overlay effects common functions
// Provides damage/wear visual effects that complement weathering_common.glsl
// Part of the composable material system (Phase 4.3)
#ifndef DAMAGE_OVERLAY_COMMON_GLSL
#define DAMAGE_OVERLAY_COMMON_GLSL

// ============================================================================
// Damage Effects
// ============================================================================

// Surface damage modifies material properties:
// - Darkens and discolors albedo (charring, rust, wear)
// - Increases roughness (surface degradation)
// - Can reveal underlying materials

// Damage visual result
struct DamageResult {
    vec3 albedo;
    float roughness;
    float metallic;
};

// Apply damage overlay to surface material
// damage: 0.0 = pristine, 1.0 = fully damaged/destroyed
// damageType: 0=wear, 1=burn, 2=rust, 3=crack
DamageResult applyDamage(vec3 baseAlbedo, float baseRoughness, float baseMetallic,
                          float damage, int damageType) {
    DamageResult result;

    // No damage - return base material
    if (damage < 0.001) {
        result.albedo = baseAlbedo;
        result.roughness = baseRoughness;
        result.metallic = baseMetallic;
        return result;
    }

    // Damage colors by type
    vec3 damageColor;
    float roughnessIncrease;
    float metallicDecrease;

    if (damageType == 1) {
        // Burn damage - darkens and chars
        damageColor = vec3(0.1, 0.05, 0.02);  // Charred black-brown
        roughnessIncrease = 0.3;
        metallicDecrease = 0.2;
    } else if (damageType == 2) {
        // Rust damage - oxidation
        damageColor = vec3(0.45, 0.22, 0.08);  // Rust orange-brown
        roughnessIncrease = 0.4;
        metallicDecrease = 0.6;  // Rust removes metallic appearance
    } else if (damageType == 3) {
        // Crack damage - reveals darker interior
        damageColor = vec3(0.15, 0.12, 0.1);  // Dark interior
        roughnessIncrease = 0.2;
        metallicDecrease = 0.1;
    } else {
        // Default wear damage - general degradation
        damageColor = vec3(0.3, 0.28, 0.25);  // Worn gray
        roughnessIncrease = 0.25;
        metallicDecrease = 0.15;
    }

    // Apply damage to albedo
    float damageBlend = damage * damage;  // Quadratic for more gradual onset
    result.albedo = mix(baseAlbedo, damageColor, damageBlend);

    // Damaged surfaces are rougher
    result.roughness = clamp(baseRoughness + roughnessIncrease * damage, 0.0, 1.0);

    // Damage reduces metallic appearance
    result.metallic = max(baseMetallic - metallicDecrease * damage, 0.0);

    return result;
}

// Simple damage albedo modification (when roughness/metallic not needed)
vec3 applyDamageAlbedo(vec3 baseAlbedo, float damage) {
    vec3 damageColor = vec3(0.3, 0.28, 0.25);  // Worn gray
    float damageBlend = damage * damage;
    return mix(baseAlbedo, damageColor, damageBlend);
}

// Calculate procedural damage pattern based on world position
// Can be combined with texture masks for more artistic control
float calculateProceduralDamage(
    vec3 worldPos,
    float baseDamage,     // Global damage amount (0-1)
    float noiseScale,     // Scale of damage variation
    float edgeBias        // Higher values concentrate damage at edges
) {
    // Simple noise-based damage pattern
    // In production, replace with better noise function
    vec3 scaledPos = worldPos * noiseScale;
    float noise = fract(sin(dot(scaledPos, vec3(12.9898, 78.233, 45.164))) * 43758.5453);

    // Edge-biased damage (using world position hash as proxy for edge detection)
    float edgeFactor = fract(sin(dot(worldPos.xz, vec2(12.9898, 78.233))) * 43758.5453);
    edgeFactor = pow(edgeFactor, 1.0 / (edgeBias + 0.1));

    // Combine base damage with procedural variation
    float damage = baseDamage * (0.5 + noise * 0.5) * (0.7 + edgeFactor * 0.3);

    return clamp(damage, 0.0, 1.0);
}

// ============================================================================
// Damage with Normal Perturbation
// ============================================================================

// Apply damage with surface normal modification for added depth
struct DamageWithNormal {
    vec3 albedo;
    float roughness;
    float metallic;
    vec3 normalOffset;  // Add to surface normal for bump effect
};

DamageWithNormal applyDamageWithNormal(
    vec3 baseAlbedo,
    float baseRoughness,
    float baseMetallic,
    vec3 worldPos,
    float damage,
    int damageType
) {
    DamageWithNormal result;

    // Get basic damage result
    DamageResult basic = applyDamage(baseAlbedo, baseRoughness, baseMetallic, damage, damageType);
    result.albedo = basic.albedo;
    result.roughness = basic.roughness;
    result.metallic = basic.metallic;

    // Calculate normal perturbation for cracks/wear
    // Simple derivative-based approach
    float epsilon = 0.01;
    float h0 = calculateProceduralDamage(worldPos, damage, 5.0, 1.0);
    float hx = calculateProceduralDamage(worldPos + vec3(epsilon, 0.0, 0.0), damage, 5.0, 1.0);
    float hz = calculateProceduralDamage(worldPos + vec3(0.0, 0.0, epsilon), damage, 5.0, 1.0);

    // Normal offset based on damage pattern gradient
    float bumpStrength = 0.3 * damage;
    result.normalOffset = vec3(
        (h0 - hx) * bumpStrength / epsilon,
        0.0,
        (h0 - hz) * bumpStrength / epsilon
    );

    return result;
}

#endif // DAMAGE_OVERLAY_COMMON_GLSL
