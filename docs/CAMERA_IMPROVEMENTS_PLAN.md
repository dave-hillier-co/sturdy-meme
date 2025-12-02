# Third-Person Camera Improvements Plan

## Overview

Improve the 3rd person camera with:

1. Smoothing for polished feel
2. Occlusion handling (objects between camera and player become transparent)
3. Orientation lock (player faces a fixed direction while moving)

Each phase produces a working, improved camera.

## Current State

- Basic orbit camera works (keyboard arrows + gamepad right stick)
- Zoom with Q/E or bumpers (2-15m range)
- No smoothing - instant response to input
- No occlusion handling - camera clips through geometry
- No orientation lock for player

---

## Phase 1: Camera Smoothing

**Goal**: Make camera movement feel polished and responsive.

### Camera.h Changes

Add smoothing state variables:

- `smoothedTarget`, `smoothedYaw`, `smoothedPitch`, `smoothedDistance` - interpolated values
- `targetYaw`, `targetPitch`, `targetDistance` - input-driven targets
- `positionSmoothSpeed` (8.0), `rotationSmoothSpeed` (12.0), `distanceSmoothSpeed` (6.0)

Add methods:

- `void updateThirdPerson(float deltaTime)` - changed signature
- `void resetSmoothing()` - snap to target on mode switch

### Camera.cpp Changes

In `updateThirdPerson()`:

1. Interpolate smoothed values toward targets using exponential smoothing
2. Formula: `smoothed += (target - smoothed) * (1 - exp(-speed * deltaTime))`
3. Calculate position from smoothed values instead of raw values

Modify `orbitYaw()`, `orbitPitch()`, `adjustDistance()`:

- Set target values instead of modifying values directly

### Application.cpp Changes

- Pass `deltaTime` to `camera.updateThirdPerson(deltaTime)`
- Call `camera.resetSmoothing()` on mode switch

---

## Phase 2: Occlusion Handling (Transparent Obstructors)

**Goal**: Objects between camera and player fade to transparent instead of camera clipping.

### Approach

Use raycast to detect objects between camera and player. Track which objects are occluding and set their opacity via a per-instance uniform. Objects fade back in when no longer occluding.

### PhysicsSystem Changes

Add raycast query method:

```cpp
struct RaycastHit {
    bool hit;
    float distance;
    uint32_t bodyId;  // To identify which object
};

std::vector<RaycastHit> castRayAllHits(glm::vec3 from, glm::vec3 to) const;
```

### Renderer/SceneManager Changes

Track per-object opacity:

- Add `float opacity` to object instance data (default 1.0)
- Pass opacity to shader via push constant or instance buffer
- Track set of currently-occluding object IDs

### Shader Changes (shader.frag)

- Add opacity uniform/input
- Multiply final color alpha by opacity
- Enable alpha blending for objects with opacity < 1.0

### Camera/Application Changes

Each frame:

1. Raycast from player focus point to camera position
2. Get list of hit object IDs
3. For newly occluding objects: start fading opacity toward 0.3
4. For no-longer-occluding objects: fade opacity back toward 1.0
5. Use smooth fade (not instant) for polish

---

## Phase 3: Orientation Lock

**Goal**: Toggle to lock player facing direction while allowing movement in any direction (strafe behavior).

### Player.h/cpp Changes

Add orientation lock state:

- `bool orientationLocked`
- `float lockedYaw` - the yaw to face when locked
- `void setOrientationLock(bool locked)`
- `void lockToCurrentOrientation()` - locks to current facing

When locked:

- Player model always faces `lockedYaw` direction
- Movement input still works but becomes strafing (movement relative to camera, rotation locked)

### InputSystem Changes

Add lock toggle input:

- Gamepad: Left trigger hold or right stick click toggle
- Keyboard: Caps Lock or middle mouse

### Application.cpp Changes

- On lock input: toggle `player.setOrientationLock()`
- When locked, player rotation calls are suppressed
- Movement continues to work relative to camera yaw

### Animation Integration (Future)

When orientation locked + moving sideways relative to locked yaw:

- Could trigger strafe animations (already loaded but not wired)
- For now, just use walk/run animations

---

## Phase 4: Dynamic FOV (Optional Polish)

**Goal**: Widen FOV during sprinting for sense of speed.

### Camera Changes

- Add `baseFov` (45), `currentFov`, `targetFov`
- Smoothly interpolate `currentFov` toward `targetFov`
- Set `targetFov` based on player movement speed (45 idle, 55 sprint)

---

## Files to Modify

| File | Phase 1 | Phase 2 | Phase 3 | Phase 4 |
|------|---------|---------|---------|---------|
| src/Camera.h | Yes | - | - | Yes |
| src/Camera.cpp | Yes | Yes | - | Yes |
| src/Application.cpp | Yes | Yes | Yes | Yes |
| src/PhysicsSystem.h | - | Yes | - | - |
| src/PhysicsSystem.cpp | - | Yes | - | - |
| src/Renderer.cpp | - | Yes | - | - |
| src/SceneManager.h | - | Yes | - | - |
| src/Player.h | - | - | Yes | - |
| src/Player.cpp | - | - | Yes | - |
| src/InputSystem.h | - | - | Yes | - |
| src/InputSystem.cpp | - | - | Yes | - |
| shaders/shader.frag | - | Yes | - | - |

## Implementation Order

1. **Phase 1** - Smoothing (standalone, immediate improvement)
2. **Phase 2** - Occlusion transparency (requires shader + renderer changes)
3. **Phase 3** - Orientation lock (player changes, enables strafe gameplay)
4. **Phase 4** - Dynamic FOV (optional polish)

Each phase should compile and run before moving to the next.
