#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include <glm/gtx/quaternion.hpp>

namespace RotationUtils {

// Create a quaternion that rotates from defaultDir to the target direction
// Handles edge cases where directions are parallel or anti-parallel
inline glm::quat rotationFromDirection(const glm::vec3& direction,
                                        const glm::vec3& defaultDir = glm::vec3(0.0f, -1.0f, 0.0f)) {
    glm::vec3 dir = glm::normalize(direction);
    // glm::rotation computes shortest-arc quaternion between two unit vectors,
    // handling aligned and anti-parallel cases internally.
    return glm::rotation(defaultDir, dir);
}

// Get direction vector from a quaternion rotation (rotates defaultDir by the quaternion)
inline glm::vec3 directionFromRotation(const glm::quat& rotation,
                                        const glm::vec3& defaultDir = glm::vec3(0.0f, -1.0f, 0.0f)) {
    return glm::vec3(glm::mat4_cast(rotation) * glm::vec4(defaultDir, 0.0f));
}

}  // namespace RotationUtils
