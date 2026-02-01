// Selection outline fragment shader (Phase 4.3)
// Simple solid color output for outline effect
#version 450

// Input from vertex shader
layout(location = 0) in vec4 fragColor;

// Output color
layout(location = 0) out vec4 outColor;

void main() {
    // Simple solid color output
    // The vertex shader has already computed the final color with pulse
    outColor = fragColor;

    // Optional: Add slight glow falloff at edges
    // This creates a softer outline appearance
    // float edgeFalloff = 1.0 - smoothstep(0.0, 1.0, gl_FragCoord.z * 0.1);
    // outColor.a *= edgeFalloff;
}
