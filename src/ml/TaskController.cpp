#include "TaskController.h"
#include <cmath>
#include <cassert>

namespace ml {

// --- TaskController ---

void TaskController::setNetwork(MLPNetwork network) {
    network_ = std::move(network);
}

void TaskController::evaluate(const Tensor& taskObs, Tensor& outLatent) const {
    assert(network_.numLayers() > 0);
    network_.forward(taskObs, output_);

    // L2 normalize to place on the unit hypersphere
    outLatent = output_;
    Tensor::l2Normalize(outLatent);
}

int TaskController::taskObsDim() const {
    return network_.inputSize();
}

int TaskController::latentDim() const {
    return network_.outputSize();
}

// --- HeadingController ---

void HeadingController::setTarget(glm::vec2 direction, float speed) {
    float len = glm::length(direction);
    if (len > 1e-6f) {
        targetDirection_ = direction / len;
    }
    targetSpeed_ = speed;
}

void HeadingController::evaluate(const glm::quat& characterRotation, Tensor& outLatent) const {
    // Rotate target direction into the character's local frame
    glm::vec3 worldDir(targetDirection_.x, 0.0f, targetDirection_.y);
    glm::vec3 localDir = glm::inverse(characterRotation) * worldDir;

    // Build task observation: [local_dir_x, local_dir_z, target_speed]
    taskObs_ = Tensor(3);
    taskObs_[0] = localDir.x;
    taskObs_[1] = localDir.z;
    taskObs_[2] = targetSpeed_;

    hlc_.evaluate(taskObs_, outLatent);
}

// --- LocationController ---

void LocationController::setTarget(glm::vec3 worldPosition) {
    targetPosition_ = worldPosition;
}

void LocationController::evaluate(glm::vec3 characterPosition, const glm::quat& characterRotation,
                                   Tensor& outLatent) const {
    // Compute world-space offset and rotate into character's local frame
    glm::vec3 offset = targetPosition_ - characterPosition;
    glm::vec3 localOffset = glm::inverse(characterRotation) * offset;

    // Build task observation: [local_offset_x, local_offset_y, local_offset_z]
    taskObs_ = Tensor(3);
    taskObs_[0] = localOffset.x;
    taskObs_[1] = localOffset.y;
    taskObs_[2] = localOffset.z;

    hlc_.evaluate(taskObs_, outLatent);
}

bool LocationController::hasReached(glm::vec3 characterPosition, float threshold) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    return glm::length(offset) < threshold;
}

// --- StrikeController ---

void StrikeController::setTarget(glm::vec3 targetPosition) {
    targetPosition_ = targetPosition;
}

void StrikeController::evaluate(glm::vec3 characterPosition, const glm::quat& characterRotation,
                                 Tensor& outLatent) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    float dist = glm::length(offset);

    // Rotate into character's local frame
    glm::vec3 localOffset = glm::inverse(characterRotation) * offset;

    // Build task observation: [local_target_x, local_target_y, local_target_z, distance]
    taskObs_ = Tensor(4);
    taskObs_[0] = localOffset.x;
    taskObs_[1] = localOffset.y;
    taskObs_[2] = localOffset.z;
    taskObs_[3] = dist;

    hlc_.evaluate(taskObs_, outLatent);
}

float StrikeController::distanceToTarget(glm::vec3 characterPosition) const {
    glm::vec3 offset = targetPosition_ - characterPosition;
    return glm::length(offset);
}

} // namespace ml
