#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cmath>

namespace MathUtils {

// Rotate a vec3 around the Y axis by the given angle (radians).
// Applies the standard Y-axis rotation matrix to the XZ components.
inline glm::vec3 rotateAroundY(const glm::vec3& v, float angle) {
    float c = std::cos(angle);
    float s = std::sin(angle);
    return glm::vec3(v.x * c + v.z * s, v.y, -v.x * s + v.z * c);
}

// Extract Y-axis rotation (yaw) from quaternion using ZYX Euler decomposition.
// Returns angle in radians. Uses atan2 for full range without gimbal lock.
// Note: glm::yaw() uses a different decomposition (asin-based) and is NOT equivalent.
inline float extractYaw(const glm::quat& q) {
    return std::atan2(2.0f * (q.w * q.y + q.x * q.z),
                      1.0f - 2.0f * (q.y * q.y + q.x * q.x));
}

} // namespace MathUtils
