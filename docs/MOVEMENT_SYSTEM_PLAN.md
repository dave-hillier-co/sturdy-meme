# Movement & Animation System Plan

## Current State

### What exists

| Subsystem | Status | Key files |
|-----------|--------|-----------|
| Kinematic motor | Working, thin Jolt wrapper | `CharacterController.h/cpp` |
| State machine | Working but legacy — candidate for deprecation | `AnimationStateMachine.h/cpp` |
| 1D blend space | Working, used by state machine | `BlendSpace.h/cpp` |
| 2D blend space | Exists, unused | `BlendSpace.h` |
| Motion matching | Working (search, KD-tree, strafe, inertial blend) | `MotionMatchingController.h/cpp`, `MotionDatabase.h/cpp` |
| Two-bone IK | Working (analytical solver) | `IKSolver.h/cpp` |
| Foot placement IK | Working with bugs | `FootPlacementIKSolver.cpp` |
| Foot phase tracker | Working, loosely coupled to IK | `FootPhaseTracker.h/cpp` |
| Pelvis adjustment | Working with ordering bug | `FootPlacementIKSolver.cpp` |
| Straddle IK | Working | `StraddleIKSolver.cpp` |
| Look-at IK | Working | `LookAtIKSolver.cpp` |
| Climbing IK | Exists, incomplete | `ClimbingIKSolver.cpp` |
| LOD / NPC batching | Working | `CharacterLOD*.h/cpp`, `NPC*.h/cpp` |
| Inertial blending | Partial — no per-bone velocity | `AnimationBlend.h/cpp` |

### Architecture decision: Motion matching is the forward path

`AnimationStateMachine` and `MotionMatchingController` are independent systems. The state machine is legacy — motion matching should own all locomotion. The state machine may be retained temporarily for jump trajectory sync (which has no motion matching equivalent yet) but should be deprecated once motion matching handles all states.

This means:
- New locomotion features (strafe, turn-in-place, start/stop) go into the motion matching database as clips, not as state machine states
- Phase-aligned transitions are handled by motion matching's pose search (it inherently finds matching phases)
- The blend space classes remain useful as utilities but aren't the primary locomotion driver

---

## Bugs & Logic Issues (by subsystem)

### Kinematic motor
1. **No slope velocity projection.** Horizontal input goes straight into velocity without projecting onto the ground plane. Near the 45° slope limit the character jitters between `OnGround` and airborne.
2. **No ground normal exposure.** `CharacterController` exposes `isOnGround()` but not `getGroundNormal()`. Foot IK re-raycasts to get what Jolt already knows.
3. **Moving platform support missing.** `getGroundVelocity()` Y-component is used for jumping; horizontal ground velocity is discarded.
4. **Hardcoded jump impulse.** `5.0f` m/s at `CharacterController.cpp:85`.

### Motion matching
5. **Root Y-rotation stripping is global.** `updatePose()` removes all Y-rotation from root to prevent double-rotation with the character controller. Correct for walk/run; destroys turn-in-place and combat animations. Root motion rotation should be fed into the character controller's facing instead.
6. **No playback speed scaling.** Time advances at 1x (`advancePlayback`). If actual movement speed diverges from the matched clip's root motion speed, feet slide.
7. **Inertial blending lacks per-bone velocity.** Comment at `MotionMatchingController.cpp:286` acknowledges this. Without velocity the blender is a fancy crossfade, not true inertialization (Bovet & Clavet GDC 2016).
8. **Trajectory world→local rotation is 2D-only.** `performSearch()` rotates trajectory samples around Y. If the trajectory predictor produces non-zero Y positions (slopes), matching breaks.
9. **Transition hysteresis has several interacting magic constants** (0.8f cost ratio, 0.5s time, 0.2s time diff, 0.5f cost ratio). Needs a coherent policy.

### Foot placement IK
10. **Foot locking stores world position but IK solves in skeleton space.** Locked position is converted back through `inverse(characterTransform)` each frame. As the character moves, precision degrades and the lock drifts. Should store in world space and convert at solve time, comparing drift against the *original* lock position.
11. **Max lock distance (15cm) releases during normal walking.** Compares locked-world vs. current-animation-world. During locomotion the character advances and the delta exceeds 15cm almost immediately. Should compare against the foot's position *at lock time*.
12. **Extension ratio weight discontinuity.** At ratio 0.95, `targetUnreachable` flips and IK weight jumps from 1.0 to 0.5 instantly. The ramp formula starts at 0.9 but the guard check starts at 0.95.
13. **`computeGlobalTransforms` called 5-6 times per solve frame.** After every sub-solver. Should use lazy invalidation or subtree recomputation.
14. **Ground alignment normal transform is fragile.** Uses `inverse(mat3(characterTransform))` instead of `transpose(inverse(mat3))`. Correct only when the character transform has no non-uniform scale.

### Foot phase tracker
15. **`sampleFootHeight` bind-pose reset is a no-op.** `FootPhaseTracker.cpp:118-120` — the loop body is empty. Foot height is sampled relative to whatever skeleton state existed at analysis time.
16. **Only detects first contact/lift per cycle.** Animations with multiple contacts (shuffle, skip) won't be handled.
17. **Phase tracker and IK solver have duplicated state.** `FootPhaseData` (tracker) and `FootPlacementIK` both store phase, progress, lock position, lock blend. No enforcement that they stay in sync.

### Pelvis adjustment
18. **Solved before foot placement.** `IKSystem::solve()` runs pelvis adjustment first, but it reads `animationFootPosition` which is only written during `FootPlacementIKSolver::solve()`. Uses last frame's foot positions.
19. **Left/right foot identification by substring.** String matching on `"left"`, `"Left"`, `"L_"`. A foot named `"foot_l"` goes to `rightFoot`.
20. **Only adjusts Y.** On slopes the pelvis should also shift forward/backward.
21. **`min(leftDrop, rightDrop)` may over-extend the higher foot** when one foot is on a step.

---

## Phased Improvement Plan

### Phase 1: Fix the Foundation (Motor + IK Bugs)

**Goal:** Make what already exists work correctly. No new features — fix the bugs that undermine foot placement and locomotion.

**Motor fixes:**
- Project `desiredVelocity` onto ground plane when grounded (use Jolt's `GetGroundNormal()`)
- Expose `getGroundNormal()` and full `getGroundVelocity()` from `CharacterController`
- Make jump impulse configurable
- Pass horizontal ground velocity through for moving platform support

**Foot placement IK fixes:**
- Fix foot lock coordinate space: store lock in world, convert through `inverse(characterTransform)` at solve time, compare drift against *original* lock position
- Fix extension ratio weight discontinuity: align ramp start with guard threshold
- Fix pelvis solve ordering: query ground heights → pelvis offset → foot IK
- Fix left/right identification: add explicit `isLeftFoot` flag to `addFootPlacement()` instead of name heuristics
- Fix ground normal transform: use `transpose(inverse(mat3))` for correct normal transformation
- Reduce redundant `computeGlobalTransforms`: recompute only when a solver modified bones

**Foot phase tracker fixes:**
- Fix the no-op bind pose reset in `sampleFootHeight`
- Unify phase state: `FootPhaseTracker` directly drives `FootPlacementIK`'s phase/lock fields (single source of truth)

**Motion matching fixes:**
- Fix transition hysteresis: replace ad-hoc magic constants with a coherent cost-ratio + minimum-dwell-time policy

**Testing:** Walk across uneven terrain, step on boxes, walk up slopes. Verify: feet plant without jitter, no visible pops when foot lock engages/releases, pelvis tracks correctly.

---

### Phase 2: Eliminate Foot Sliding (Stride Matching + Root Motion)

**Goal:** Foot contact with the ground should be stable during stance at all locomotion speeds.

**Stride matching for motion matching:**
- Add playback speed scaling to `MotionMatchingController` (currently fixed at 1.0x) — scale playback rate to match actual character speed vs. clip root motion speed
- Compute stride length per animation clip: track foot X/Z displacement over one cycle, store in `AnimationClip`
- For in-place Mixamo clips with `locomotionSpeed` override, use that speed for the ratio

**Root motion integration:**
- Fix root Y-rotation stripping: instead of discarding all Y-rotation from root, extract it and feed it into the character controller's facing delta. This preserves turn animations while preventing double-rotation for walk/run
- Extract per-frame root displacement from the playing animation; feed it back to the character controller as authoritative horizontal velocity when foot locking is active
- This closes the loop: animation drives movement → feet don't slide → IK only adjusts for terrain

**Inertial blending (full):**
- Track per-bone velocity via finite difference from previous frame's pose
- Pass into `InertialBlender::startSkeletalBlend()` for proper inertialization
- Critical for motion matching transitions to look natural

**Testing:** Walk/run at every speed from 0 to max. Observe feet in stance phase — zero visible sliding. Motion matching transitions produce smooth overshoot-and-settle blends.

---

### Phase 3: Full Locomotion (Turn, Strafe, Start/Stop)

**Goal:** The character can move in all directions, turn naturally, and transition smoothly into and out of locomotion. All driven through motion matching.

**Turn-in-place:**
- Add turn clips (turn-left-90, turn-right-90, turn-180) to the motion matching database with appropriate tags
- The motion matcher's trajectory prediction already handles facing changes — the turn clips should score well when the query trajectory shows a large facing delta with low positional movement
- Foot locking during turns: lock the planted (pivot) foot; the moving foot follows the animation arc

**Directional locomotion:**
- Add strafe-walk, strafe-run, walk-backward clips to the motion matching database tagged as `"strafe"`
- Motion matching strafe mode already exists (`setStrafeMode()`) — extend it with the new clips
- Upper/lower body split: use the existing `AnimationLayer` and `BoneMask` system so upper body faces aim/camera direction while lower body follows movement

**Start/stop transitions:**
- Add start-walk, stop-walk, start-run, stop-run clips tagged with `"transition"`
- Motion matching naturally selects these when the trajectory shows acceleration from idle or deceleration to idle
- No special state machine logic needed — the cost function handles it

**Trajectory prediction for slopes:**
- Fix the 2D-only trajectory rotation in `performSearch()` to handle non-zero Y (slope-relative positions)
- Include vertical velocity in trajectory samples for uphill/downhill awareness

**Testing:** Stand still, rotate 90°/180° — natural turn animation, no foot slide. Strafe left/right at walk and run speed — feet match movement. Start walking from idle, stop — visible weight transfer.

---

### Phase 4: Terrain Detail (Multi-Point Ground, Toe IK, Foot Roll, Slopes)

**Goal:** Feet interact realistically with complex terrain — slopes, stairs, edges.

**Multi-point ground queries:**
- Replace single ankle raycast with heel, ball-of-foot, and toe queries
- Derive probe positions from skeleton geometry (foot bone → toe bone)
- Fit a plane to the contacts; compute foot pitch and roll
- Handle partial contacts (heel on step edge, toe hanging off)

**Toe IK:**
- After foot placement, raycast from toe position
- Calculate toe rotation to match ground angle
- Clamp to natural limits (~45° dorsiflexion, ~60° plantarflexion)
- Blend with foot phase: push-off allows toe bend, swing returns to animation

**Foot roll:**
- Add heel/ball/toe virtual markers (derived from skeleton)
- Implement roll phases driven by foot phase tracker: heel strike → foot flat → heel off → toe off
- Apply incremental rotations to foot bone

**Slope compensation:**
- Sample ground normal at character center
- Forward/backward pelvis shift on slopes (currently pelvis only adjusts Y)
- Body lean proportional to grade (integrate with existing straddle IK)
- Adjust playback speed slightly for uphill (shorter stride feel) / downhill (longer stride)

**Testing:** Walk up/down stairs — toe bends on step edges. Walk across rocky terrain — heel and toe both contact. Walk up a 30° slope — body lean and stride adjustment visible.

---

## Animation Assets Needed

### Have
- [x] Idle
- [x] Walk
- [x] Run
- [x] Jump

### Phase 3
- [ ] Turn Left 90, Turn Right 90, Turn 180
- [ ] Strafe Left Walk, Strafe Right Walk
- [ ] Walk Backward
- [ ] Strafe Left Run, Strafe Right Run (optional, run speed strafing is less common)
- [ ] Walk Start, Walk Stop
- [ ] Run Start, Run Stop

### Phase 4
- No new animation assets — driven by IK and procedural adjustment

---

## Success Metrics

1. **No visible foot sliding** at any movement speed or during any transition
2. **Clean foot lifts** during swing phase (no dragging or hovering)
3. **Natural turning** without sliding rotation
4. **Stable foot locks** on uneven terrain without jitter or pops
5. **Correct pelvis tracking** — no leg hyper-extension, no floating above terrain
6. **Directional locomotion** with proper upper/lower body split
7. **Terrain conformance** — toes bend on slopes, feet plant on stairs

---

## Architecture Notes

### IK solve order (corrected)
1. Query ground heights at animation foot positions
2. Calculate pelvis offset from ground data
3. Apply pelvis adjustment
4. Recompute global transforms (once)
5. Solve foot placement IK (left, right)
6. Recompute global transforms (once)
7. Solve straddle IK
8. Solve two-bone chains (arms, etc.)
9. Solve look-at IK (last — head shouldn't affect body)

### AnimationStateMachine deprecation path
The state machine currently handles jump trajectory synchronization (mapping physics arc progress to animation time). To fully deprecate it:
- Add jump clips to the motion matching database
- The motion matcher's forced search on non-looping clip end (`forceSearchNextUpdate_`) already handles the "clip finished, find next pose" case
- Jump trajectory sync (mapping elapsed time to animation progress based on predicted flight time) would need to be ported into the motion matching controller as a special playback mode, or simply dropped in favor of letting motion matching find appropriate falling/landing poses naturally

### Root motion authority
The current system uses kinematic input (`setInput(desiredVelocity)`) with post-hoc speed scaling. The target architecture: animation drives horizontal displacement during stance → character controller reconciles with physics → IK handles terrain only. This is the standard root-motion-driven approach.
