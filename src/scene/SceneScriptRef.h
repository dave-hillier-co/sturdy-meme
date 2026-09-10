#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

class Camera;
class RendererSystems;
struct PerformanceToggles;

/**
 * SceneScriptRef - reader for the solent engine's scene-script JSON.
 *
 * This engine is the parity oracle for the solent rebuild: both engines are
 * pointed at one `<name>.scene.json` and their images are compared. The format
 * is documented in solent's docs/SCENE_SCRIPT.md and its normative definition
 * is solent's src/scene_script/SceneScript.h. Every default member initialiser
 * below is that header's default, so a file that omits a key means the same
 * thing to both readers.
 *
 * Three rules are copied from the format, not invented here:
 *  1. Strict JSON. No comments, no trailing commas. Human annotation lives in
 *     the `notes` string, which every object accepts.
 *  2. An unknown key is a hard error naming its JSON pointer. Silent tolerance
 *     is how a typo becomes a missing feature that nobody notices.
 *  3. A field this engine cannot honour is reported by name through
 *     `unappliedKeys`, never ignored. An unreported difference is a false
 *     parity result, which is worse than no result at all.
 *
 * Which fields land where is documented in solent's docs/PARITY.md, and
 * `unappliedKeys` is the executable copy of that document's third column.
 *
 * This is a reader and an applier and nothing else: no render graph, no scene
 * loader, no structure from solent comes back across.
 */
struct SceneScriptRef {
    // ---------------------------------------------------------------- sections
    struct CameraPose {
        glm::vec3 position{0.0f, 2.0f, 5.0f};
        float yawDeg = 180.0f;   // [0, 360), 0 looks down +Z (see applyTo)
        float pitchDeg = 0.0f;   // [-89, 89]
        bool perspective = true; // "orthographic" cannot be expressed here
        float fovYDeg = 60.0f;   // (0, 179]
        float orthoHeight = 20.0f;
        float nearPlane = 0.1f;
        float farPlane = 10000.0f;
    };

    struct TimeOfDayState {
        float timeOfDay = 0.5f;              // [0, 1), 0.5 = local noon
        float timeScale = 0.0f;              // >= 0, 0 freezes the sun
        float cycleDurationSeconds = 600.0f; // > 0
        int year = 2024;
        int month = 6;
        int day = 21;
        double latitudeDeg = 51.5074;
        double longitudeDeg = -0.1278;
        std::optional<float> moonPhase{};    // JSON null = derive from the date
    };

    struct WeatherRefState {
        enum class Kind { Clear, Rain, Snow };
        Kind kind = Kind::Clear;
        float intensity = 0.0f;
        float cloudCoverage = 0.0f;
        float windDirectionDeg = 0.0f;
        float windSpeedMs = 0.0f;
        float fogDensity = 0.0f;
    };

    // ----------------------------------------------------------------- fields
    int version = 1;
    std::string id;
    std::string title;
    std::string notes;
    uint64_t seed = 0;

    CameraPose camera{};
    TimeOfDayState time{};
    WeatherRefState weather{};

    glm::vec4 clearColor{0.043f, 0.055f, 0.075f, 1.0f};
    uint32_t width = 1280;
    uint32_t height = 720;
    bool vsync = true;

    std::vector<std::pair<std::string, bool>> features;

    uint32_t captureFrames = 1;
    uint32_t captureWidth = 800;
    uint32_t captureHeight = 600;
    bool deterministicClock = true;
    float fixedDeltaSeconds = 1.0f / 60.0f;

    // JSON pointers of fields this engine cannot honour, filled in by the
    // loader whether or not the file spelled them out: a default value is
    // still a claim, and an unhonoured default is still a parity difference.
    std::vector<std::string> unappliedKeys;

    // Camera yaw is the one convention that differs. solent's yaw 0 looks down
    // +Z; this engine's Camera::updateVectors builds its forward vector as
    // (cos yaw * cos pitch, sin pitch, sin yaw * cos pitch), so its yaw 0 looks
    // down +X and its yaw 90 looks down +Z. cameraYawDegForThisEngine() applies
    // the +90 offset, which keeps the direction of rotation the same in both
    // engines.
    [[nodiscard]] float cameraYawDegForThisEngine() const;
};

/**
 * Parse a solent scene script. Returns false and fills `error` with a message
 * naming the offending JSON pointer on any failure; `out` is untouched then.
 */
bool loadSceneScriptRef(const std::string& path, SceneScriptRef& out, std::string& error);

/**
 * Log every field the script asked for that this engine cannot honour, one
 * warning per field plus a summary line. Called by applySceneScriptRef; call
 * it directly only when applying by hand.
 *
 * `alsoUnappliedCount` is added to the summary's total for fields the caller
 * reported itself, because only the caller knows them: which feature names have
 * no toggle here is known at apply time, not at load time.
 */
void reportUnappliedSceneScriptKeys(const SceneScriptRef& script,
                                    size_t alsoUnappliedCount = 0);

/**
 * Add the /capture fields an interactive `--scene` run (one with no --capture)
 * cannot honour, so they are named instead of dropped.
 *
 * Without --capture there is no frame counter armed, no swapchain readback and
 * no pinned clock, so every field in the capture section is a difference the
 * comparison cannot see. Dropping the whole section silently is precisely the
 * false-parity failure rule 3 above exists to prevent, so the interactive path
 * reports it the same way --capture reports /render/resolution.
 *
 * Call before applySceneScriptRef, which is what does the logging.
 */
void addUnappliedCaptureKeysForInteractiveRun(SceneScriptRef& script);

/**
 * Apply what this engine can express. Must run AFTER Application::init(),
 * because setupWorld() places the camera itself while the loading screen is
 * still presenting and would otherwise overwrite the script's pose.
 *
 * Feature names come straight from PerformanceToggles::getAllToggles(), so the
 * mapping is setToggle(name, enabled) with no translation table. A name this
 * engine does not have is reported, not applied - solent declares four
 * render-graph toggles and one scheduler toggle that exist only there.
 */
void applySceneScriptRef(const SceneScriptRef& script, Camera& camera,
                         PerformanceToggles& toggles, RendererSystems& systems);
