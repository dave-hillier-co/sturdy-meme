// Hue shift utility functions for color tinting
// Uses RGB to HSV to RGB conversion for smooth hue rotation

#ifndef HUE_SHIFT_COMMON_GLSL
#define HUE_SHIFT_COMMON_GLSL

// Convert RGB to HSV
vec3 rgb2hsv(vec3 c) {
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));

    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

// Convert HSV to RGB
vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// Apply hue shift to RGB color
// hueShift is in radians (0 to 2*PI for full rotation)
vec3 applyHueShift(vec3 rgb, float hueShift) {
    if (abs(hueShift) < 0.001) {
        return rgb;  // No shift needed
    }

    vec3 hsv = rgb2hsv(rgb);
    // Convert hue shift from radians to [0,1] range
    hsv.x = fract(hsv.x + hueShift / (2.0 * 3.14159265359));
    return hsv2rgb(hsv);
}

#endif // HUE_SHIFT_COMMON_GLSL
