// Selection outline vertex shader (Phase 4.3)
// Renders outlines around selected entities using vertex extrusion
#version 450

// Vertex attributes
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// Uniform buffer - uses existing UBO layout from ubo_common.glsl
layout(set = 0, binding = 0) uniform GlobalUBO {
    mat4 view;
    mat4 proj;
    vec4 cameraPos;
    vec4 time;  // x = total time, y = delta time
} globals;

// Push constants for per-object data
layout(push_constant) uniform OutlinePushConstants {
    mat4 model;
    vec4 outlineColor;     // rgb = color, a = alpha
    float outlineThickness; // In world units
    float pulseSpeed;       // 0 = no pulse
    float _pad0;
    float _pad1;
} outline;

// Output to fragment shader
layout(location = 0) out vec4 fragColor;

void main() {
    // Extrude vertex along normal for outline effect
    vec3 worldNormal = normalize(mat3(outline.model) * inNormal);
    vec3 worldPos = vec3(outline.model * vec4(inPosition, 1.0));

    // Calculate view-dependent thickness for consistent screen-space appearance
    vec3 viewDir = normalize(globals.cameraPos.xyz - worldPos);
    float viewDot = abs(dot(worldNormal, viewDir));

    // Scale extrusion based on distance from camera for consistent visual thickness
    float distanceToCamera = length(globals.cameraPos.xyz - worldPos);
    float scaledThickness = outline.outlineThickness * (distanceToCamera * 0.01 + 0.5);

    // Extrude position along normal
    vec3 extrudedPos = worldPos + worldNormal * scaledThickness;

    // Apply pulse effect if enabled
    float pulseAlpha = 1.0;
    if (outline.pulseSpeed > 0.0) {
        float pulse = sin(globals.time.x * outline.pulseSpeed * 6.28318) * 0.5 + 0.5;
        pulseAlpha = 0.5 + pulse * 0.5;
    }

    // Transform to clip space
    gl_Position = globals.proj * globals.view * vec4(extrudedPos, 1.0);

    // Pass color to fragment shader with pulse
    fragColor = vec4(outline.outlineColor.rgb, outline.outlineColor.a * pulseAlpha);
}
