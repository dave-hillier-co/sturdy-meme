#pragma once

#include <memory>

#include "HDRPassRecorder.h"
#include "SkinnedCharDrawable.h"  // RagdollDrawCallback

class RendererSystems;

/**
 * SceneComposition - declares what the HDR scene is made of.
 *
 * Builds and populates an HDRPassRecorder by registering every scene drawable
 * (sky, terrain, scene objects, characters, vegetation, water, weather, debug)
 * in its draw order and secondary-command-buffer slot. This is the single place
 * that knows the scene's composition; the renderer just asks for a ready recorder.
 */
namespace SceneComposition {

std::unique_ptr<HDRPassRecorder> buildHDRPassRecorder(
    RendererSystems& systems,
    RagdollDrawCallback ragdollDrawCallback);

} // namespace SceneComposition
