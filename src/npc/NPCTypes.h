#pragma once

#include <cstdint>
#include <cstddef>

// Shared NPC enums and lightweight per-NPC playback state.
//
// These types are consumed by the NPC simulation/rendering API surface
// (NPCSpawnInfo, NPCRenderer, CharacterTemplate) and a few ML/GUI helpers.
// The authoritative per-NPC runtime state lives in ECS components
// (see src/ecs/Components.h); this header only carries the small value
// types that cross the public NPC API.

// NPC LOD levels inspired by Assassin's Creed crowd systems.
// Controls update frequency and animation quality.
enum class NPCLODLevel : uint8_t {
    Virtual = 0,      // >50m: No rendering, minimal updates (every 10 seconds)
    Bulk = 1,         // 25-50m: Simplified animation, reduced updates (every 1 second)
    Real = 2,         // <25m: Full animation every frame
    PhysicsBased = 3  // <10m: Physics-driven ragdoll with ML policy (UniCon)
};

// NPC activity states for animation variety.
enum class NPCActivity : uint8_t {
    Idle = 0,       // Standing still
    Walking = 1,    // Slow movement (walk animation)
    Running = 2     // Fast movement (run animation)
};

// Animation playback state per-NPC.
// Minimal state needed to continue animation from any point.
struct AnimationPlaybackState {
    size_t clipIndex = 0;          // Index into template's animation clips
    float currentTime = 0.0f;      // Current playback position in seconds
    float playbackSpeed = 1.0f;    // Speed multiplier
    float blendWeight = 1.0f;      // Blend weight for transitions
    bool looping = true;           // Whether to loop at end
    NPCActivity activity = NPCActivity::Idle;  // Current activity state
};
