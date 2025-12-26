// Quaternion utility functions for GPU shaders
// Shared between all vegetation and instancing shaders
//
// Usage in shaders: #include "quaternion.glsl"

#ifndef QUATERNION_GLSL
#define QUATERNION_GLSL

// Rotate a vector by a quaternion
// q = quaternion (x, y, z, w) where w is the scalar component
vec3 rotateByQuat(vec3 v, vec4 q) {
    vec3 t = 2.0 * cross(q.xyz, v);
    return v + q.w * t + cross(q.xyz, t);
}

// Quaternion multiplication: q1 * q2
// Returns the quaternion representing rotation q1 followed by q2
vec4 quatMul(vec4 q1, vec4 q2) {
    return vec4(
        q1.w * q2.xyz + q2.w * q1.xyz + cross(q1.xyz, q2.xyz),
        q1.w * q2.w - dot(q1.xyz, q2.xyz)
    );
}

// Convert a 3x3 rotation matrix to a quaternion
// Handles all cases using the Shepperd method for numerical stability
vec4 mat3ToQuat(mat3 m) {
    float trace = m[0][0] + m[1][1] + m[2][2];
    vec4 q;

    if (trace > 0.0) {
        float s = 0.5 / sqrt(trace + 1.0);
        q.w = 0.25 / s;
        q.x = (m[2][1] - m[1][2]) * s;
        q.y = (m[0][2] - m[2][0]) * s;
        q.z = (m[1][0] - m[0][1]) * s;
    } else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) {
        float s = 2.0 * sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]);
        q.w = (m[2][1] - m[1][2]) / s;
        q.x = 0.25 * s;
        q.y = (m[0][1] + m[1][0]) / s;
        q.z = (m[0][2] + m[2][0]) / s;
    } else if (m[1][1] > m[2][2]) {
        float s = 2.0 * sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]);
        q.w = (m[0][2] - m[2][0]) / s;
        q.x = (m[0][1] + m[1][0]) / s;
        q.y = 0.25 * s;
        q.z = (m[1][2] + m[2][1]) / s;
    } else {
        float s = 2.0 * sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]);
        q.w = (m[1][0] - m[0][1]) / s;
        q.x = (m[0][2] + m[2][0]) / s;
        q.y = (m[1][2] + m[2][1]) / s;
        q.z = 0.25 * s;
    }

    return normalize(q);
}

// Create a quaternion from an axis and angle (in radians)
vec4 quatFromAxisAngle(vec3 axis, float angle) {
    float halfAngle = angle * 0.5;
    float s = sin(halfAngle);
    return vec4(axis * s, cos(halfAngle));
}

// Get the conjugate of a quaternion (inverts the rotation for unit quaternions)
vec4 quatConjugate(vec4 q) {
    return vec4(-q.xyz, q.w);
}

#endif // QUATERNION_GLSL
