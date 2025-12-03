#pragma once

#include <glm/glm.hpp>
#include <cstdint>

/**
 * FrameData - Per-frame shared state passed to subsystems
 *
 * Consolidates the scattered per-frame parameters that are computed once
 * at the start of render() and passed to multiple subsystems. This reduces
 * parameter passing overhead and makes dependencies explicit.
 *
 * Usage:
 *   FrameData frame;
 *   frame.frameIndex = currentFrame;
 *   frame.deltaTime = deltaTime;
 *   // ... populate other fields
 *   subsystem.update(frame);
 */
struct FrameData {
    // Frame identification
    uint32_t frameIndex = 0;

    // Timing
    float deltaTime = 0.0f;
    float time = 0.0f;          // Total elapsed time
    float timeOfDay = 0.0f;     // Normalized day/night cycle [0, 1]

    // Camera
    glm::vec3 cameraPosition = glm::vec3(0.0f);
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 projection = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);

    // Lighting
    glm::vec3 sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
    float sunIntensity = 1.0f;

    // Player (for interaction systems like grass displacement)
    glm::vec3 playerPosition = glm::vec3(0.0f);
    float playerCapsuleRadius = 0.5f;

    // Terrain parameters (for systems that need terrain info)
    float terrainSize = 1024.0f;
    float heightScale = 100.0f;
};
