#include "Components.h"
#include <glm/gtc/matrix_transform.hpp>

namespace ecs {

Transform::Transform(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale) {
    matrix = glm::mat4(1.0f);
    matrix = glm::translate(matrix, position);
    matrix *= glm::mat4_cast(rotation);
    matrix = glm::scale(matrix, scale);
}

Transform Transform::fromPosition(const glm::vec3& pos) {
    Transform t;
    t.matrix = glm::translate(glm::mat4(1.0f), pos);
    return t;
}

Transform Transform::fromPositionRotation(const glm::vec3& pos, const glm::quat& rot) {
    Transform t;
    t.matrix = glm::translate(glm::mat4(1.0f), pos);
    t.matrix *= glm::mat4_cast(rot);
    return t;
}

Transform Transform::fromTRS(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scale) {
    return Transform(pos, rot, scale);
}

glm::mat4 LocalTransform::toMatrix() const {
    glm::mat4 m = glm::mat4(1.0f);
    m = glm::translate(m, position);
    m *= glm::mat4_cast(rotation);
    m = glm::scale(m, scale);
    return m;
}

} // namespace ecs
