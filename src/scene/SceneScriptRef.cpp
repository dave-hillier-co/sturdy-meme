#include "SceneScriptRef.h"

#include <SDL3/SDL.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <stdexcept>

#include "Camera.h"
#include "atmosphere/CelestialCalculator.h"
#include "atmosphere/TimeSystem.h"
#include "atmosphere/WeatherSystem.h"
#include "atmosphere/WindSystem.h"
#include "core/PerformanceToggles.h"
#include "core/RendererSystems.h"
#include "core/interfaces/IEnvironmentControl.h"

namespace {

using json = nlohmann::json;

// Thrown on the first problem found and caught at the loader boundary, so a
// message always names one JSON pointer instead of a list of consequences.
struct ScriptError : std::runtime_error {
    explicit ScriptError(std::string message) : std::runtime_error(std::move(message)) {}
};

[[noreturn]] void fail(const std::string& pointer, const std::string& what) {
    throw ScriptError(pointer + ": " + what);
}

// Levenshtein distance, used only to name the closest known key in an
// unknown-key error. Rule 2 of the format is that a typo must be told what it
// probably meant, not merely that it was wrong.
size_t editDistance(const std::string& a, const std::string& b) {
    std::vector<size_t> previous(b.size() + 1);
    std::vector<size_t> current(b.size() + 1);
    for (size_t j = 0; j <= b.size(); ++j) previous[j] = j;
    for (size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        for (size_t j = 1; j <= b.size(); ++j) {
            const size_t substitution = previous[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            current[j] = std::min({previous[j] + 1, current[j - 1] + 1, substitution});
        }
        previous = current;
    }
    return previous[b.size()];
}

void rejectUnknownKeys(const json& object, const std::string& pointer,
                       std::initializer_list<const char*> known) {
    for (const auto& entry : object.items()) {
        const std::string& key = entry.key();
        if (std::any_of(known.begin(), known.end(),
                        [&](const char* candidate) { return key == candidate; })) {
            continue;
        }
        std::string closest;
        size_t best = static_cast<size_t>(-1);
        for (const char* candidate : known) {
            const size_t distance = editDistance(key, candidate);
            if (distance < best) {
                best = distance;
                closest = candidate;
            }
        }
        fail(pointer + "/" + key,
             "unknown key; closest known key is \"" + closest + "\"");
    }
}

const json* member(const json& object, const char* key) {
    const auto found = object.find(key);
    return found == object.end() ? nullptr : &*found;
}

const json& requireObject(const json& value, const std::string& pointer) {
    if (!value.is_object()) fail(pointer, "expected an object");
    return value;
}

double readNumber(const json& value, const std::string& pointer) {
    if (!value.is_number()) fail(pointer, "expected a number");
    return value.get<double>();
}

double readRanged(const json& value, const std::string& pointer, double low, double high,
                  bool lowInclusive, bool highInclusive) {
    const double result = readNumber(value, pointer);
    const bool lowOk = lowInclusive ? result >= low : result > low;
    const bool highOk = highInclusive ? result <= high : result < high;
    if (!lowOk || !highOk) {
        char range[128];
        snprintf(range, sizeof(range), "out of range %c%g, %g%c", lowInclusive ? '[' : '(', low,
                 high, highInclusive ? ']' : ')');
        fail(pointer, range);
    }
    return result;
}

int64_t readInteger(const json& value, const std::string& pointer, int64_t low, int64_t high) {
    if (!value.is_number_integer()) fail(pointer, "expected an integer");
    const int64_t result = value.get<int64_t>();
    if (result < low || result > high) {
        char range[128];
        snprintf(range, sizeof(range), "out of range [%lld, %lld]",
                 static_cast<long long>(low), static_cast<long long>(high));
        fail(pointer, range);
    }
    return result;
}

bool readBool(const json& value, const std::string& pointer) {
    if (!value.is_boolean()) fail(pointer, "expected a boolean");
    return value.get<bool>();
}

std::string readString(const json& value, const std::string& pointer) {
    if (!value.is_string()) fail(pointer, "expected a string");
    return value.get<std::string>();
}

const json& requireArray(const json& value, const std::string& pointer, size_t size) {
    if (!value.is_array()) fail(pointer, "expected an array");
    if (value.size() != size) {
        char detail[96];
        snprintf(detail, sizeof(detail), "expected %zu elements, found %zu", size, value.size());
        fail(pointer, detail);
    }
    return value;
}

// ------------------------------------------------------------------ sections

void readCamera(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer,
                      {"notes", "position", "yawDeg", "pitchDeg", "projection", "fovYDeg",
                       "orthoHeight", "nearPlane", "farPlane"});

    if (const json* position = member(object, "position")) {
        const json& array = requireArray(*position, pointer + "/position", 3);
        for (size_t i = 0; i < 3; ++i) {
            out.camera.position[static_cast<glm::length_t>(i)] = static_cast<float>(
                readNumber(array[i], pointer + "/position/" + std::to_string(i)));
        }
    }
    if (const json* yaw = member(object, "yawDeg")) {
        out.camera.yawDeg =
            static_cast<float>(readRanged(*yaw, pointer + "/yawDeg", 0.0, 360.0, true, false));
    }
    if (const json* pitch = member(object, "pitchDeg")) {
        out.camera.pitchDeg =
            static_cast<float>(readRanged(*pitch, pointer + "/pitchDeg", -89.0, 89.0, true, true));
    }
    if (const json* projection = member(object, "projection")) {
        const std::string name = readString(*projection, pointer + "/projection");
        if (name == "perspective") {
            out.camera.perspective = true;
        } else if (name == "orthographic") {
            out.camera.perspective = false;
        } else {
            fail(pointer + "/projection",
                 "unknown projection \"" + name + "\"; expected perspective, orthographic");
        }
    }
    if (const json* fov = member(object, "fovYDeg")) {
        out.camera.fovYDeg =
            static_cast<float>(readRanged(*fov, pointer + "/fovYDeg", 0.0, 179.0, false, true));
    }
    if (const json* orthoHeight = member(object, "orthoHeight")) {
        out.camera.orthoHeight = static_cast<float>(
            readRanged(*orthoHeight, pointer + "/orthoHeight", 0.0, 1.0e9, false, true));
    }
    if (const json* nearPlane = member(object, "nearPlane")) {
        out.camera.nearPlane = static_cast<float>(
            readRanged(*nearPlane, pointer + "/nearPlane", 0.0, 1.0e9, false, true));
    }
    if (const json* farPlane = member(object, "farPlane")) {
        out.camera.farPlane = static_cast<float>(
            readRanged(*farPlane, pointer + "/farPlane", 0.0, 1.0e9, false, true));
    }
    if (out.camera.farPlane <= out.camera.nearPlane) {
        fail(pointer + "/farPlane", "must be greater than nearPlane");
    }
}

void readTime(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer,
                      {"notes", "timeOfDay", "timeScale", "cycleDurationSeconds", "date",
                       "location", "moonPhase"});

    if (const json* timeOfDay = member(object, "timeOfDay")) {
        out.time.timeOfDay = static_cast<float>(
            readRanged(*timeOfDay, pointer + "/timeOfDay", 0.0, 1.0, true, false));
    }
    if (const json* timeScale = member(object, "timeScale")) {
        out.time.timeScale = static_cast<float>(
            readRanged(*timeScale, pointer + "/timeScale", 0.0, 1.0e9, true, true));
    }
    if (const json* cycle = member(object, "cycleDurationSeconds")) {
        out.time.cycleDurationSeconds = static_cast<float>(
            readRanged(*cycle, pointer + "/cycleDurationSeconds", 0.0, 1.0e9, false, true));
    }
    if (const json* date = member(object, "date")) {
        const json& array = requireArray(*date, pointer + "/date", 3);
        out.time.year = static_cast<int>(readInteger(array[0], pointer + "/date/0", 1900, 2200));
        out.time.month = static_cast<int>(readInteger(array[1], pointer + "/date/1", 1, 12));
        out.time.day = static_cast<int>(readInteger(array[2], pointer + "/date/2", 1, 31));
    }
    if (const json* location = member(object, "location")) {
        const json& array = requireArray(*location, pointer + "/location", 2);
        out.time.latitudeDeg = readRanged(array[0], pointer + "/location/0", -90.0, 90.0, true, true);
        out.time.longitudeDeg =
            readRanged(array[1], pointer + "/location/1", -180.0, 180.0, true, true);
    }
    if (const json* moonPhase = member(object, "moonPhase")) {
        if (moonPhase->is_null()) {
            out.time.moonPhase.reset();
        } else {
            out.time.moonPhase = static_cast<float>(
                readRanged(*moonPhase, pointer + "/moonPhase", 0.0, 1.0, true, true));
        }
    }
}

void readWeather(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer,
                      {"notes", "kind", "intensity", "cloudCoverage", "windDirectionDeg",
                       "windSpeedMs", "fogDensity"});

    if (const json* kind = member(object, "kind")) {
        const std::string name = readString(*kind, pointer + "/kind");
        if (name == "clear") {
            out.weather.kind = SceneScriptRef::WeatherRefState::Kind::Clear;
        } else if (name == "rain") {
            out.weather.kind = SceneScriptRef::WeatherRefState::Kind::Rain;
        } else if (name == "snow") {
            out.weather.kind = SceneScriptRef::WeatherRefState::Kind::Snow;
        } else {
            fail(pointer + "/kind",
                 "unknown weather kind \"" + name + "\"; expected clear, rain, snow");
        }
    }
    if (const json* intensity = member(object, "intensity")) {
        out.weather.intensity = static_cast<float>(
            readRanged(*intensity, pointer + "/intensity", 0.0, 1.0, true, true));
    }
    if (const json* coverage = member(object, "cloudCoverage")) {
        out.weather.cloudCoverage = static_cast<float>(
            readRanged(*coverage, pointer + "/cloudCoverage", 0.0, 1.0, true, true));
    }
    if (const json* direction = member(object, "windDirectionDeg")) {
        out.weather.windDirectionDeg = static_cast<float>(
            readRanged(*direction, pointer + "/windDirectionDeg", 0.0, 360.0, true, false));
    }
    if (const json* speed = member(object, "windSpeedMs")) {
        out.weather.windSpeedMs = static_cast<float>(
            readRanged(*speed, pointer + "/windSpeedMs", 0.0, 1.0e9, true, true));
    }
    if (const json* fog = member(object, "fogDensity")) {
        out.weather.fogDensity = static_cast<float>(
            readRanged(*fog, pointer + "/fogDensity", 0.0, 1.0e9, true, true));
    }
}

void readRender(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer, {"notes", "clearColor", "resolution", "vsync"});

    if (const json* color = member(object, "clearColor")) {
        const json& array = requireArray(*color, pointer + "/clearColor", 4);
        for (size_t i = 0; i < 4; ++i) {
            out.clearColor[static_cast<glm::length_t>(i)] = static_cast<float>(readRanged(
                array[i], pointer + "/clearColor/" + std::to_string(i), 0.0, 1.0, true, true));
        }
    }
    if (const json* resolution = member(object, "resolution")) {
        const json& array = requireArray(*resolution, pointer + "/resolution", 2);
        out.width = static_cast<uint32_t>(
            readInteger(array[0], pointer + "/resolution/0", 16, 65535));
        out.height = static_cast<uint32_t>(
            readInteger(array[1], pointer + "/resolution/1", 16, 65535));
    }
    if (const json* vsync = member(object, "vsync")) {
        out.vsync = readBool(*vsync, pointer + "/vsync");
    }
}

void readFeatures(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    for (const auto& entry : object.items()) {
        out.features.emplace_back(entry.key(),
                                  readBool(entry.value(), pointer + "/" + entry.key()));
    }
}

void readContent(const json& value, const std::string& pointer, SceneScriptRef& out) {
    if (!value.is_array()) fail(pointer, "expected an array");
    for (size_t i = 0; i < value.size(); ++i) {
        const std::string entryPointer = pointer + "/" + std::to_string(i);
        const json& object = requireObject(value[i], entryPointer);
        rejectUnknownKeys(object, entryPointer, {"notes", "kind", "name", "params"});
        const json* kind = member(object, "kind");
        if (!kind) fail(entryPointer, "a content entry requires \"kind\"");
        const std::string kindName = readString(*kind, entryPointer + "/kind");
        // Parsed and range-checked, then reported: this engine renders its own
        // fixed world and has no spawner a script can drive.
        out.unappliedKeys.push_back(entryPointer + " (kind \"" + kindName + "\")");
    }
}

void readCapture(const json& value, const std::string& pointer, SceneScriptRef& out) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer,
                      {"notes", "frames", "resolution", "golden", "tolerance",
                       "deterministicClock", "fixedDeltaSeconds"});

    if (const json* frames = member(object, "frames")) {
        out.captureFrames =
            static_cast<uint32_t>(readInteger(*frames, pointer + "/frames", 1, 1000000));
    }
    if (const json* resolution = member(object, "resolution")) {
        const json& array = requireArray(*resolution, pointer + "/resolution", 2);
        out.captureWidth = static_cast<uint32_t>(
            readInteger(array[0], pointer + "/resolution/0", 16, 65535));
        out.captureHeight = static_cast<uint32_t>(
            readInteger(array[1], pointer + "/resolution/1", 16, 65535));
    }
    if (const json* golden = member(object, "golden")) {
        (void)readString(*golden, pointer + "/golden");
    }
    if (const json* tolerance = member(object, "tolerance")) {
        const std::string tolerancePointer = pointer + "/tolerance";
        const json& toleranceObject = requireObject(*tolerance, tolerancePointer);
        rejectUnknownKeys(toleranceObject, tolerancePointer,
                          {"notes", "maxChannelDelta", "maxFailingPixelFraction"});
        if (const json* delta = member(toleranceObject, "maxChannelDelta")) {
            (void)readInteger(*delta, tolerancePointer + "/maxChannelDelta", 0, 255);
        }
        if (const json* fraction = member(toleranceObject, "maxFailingPixelFraction")) {
            (void)readRanged(*fraction, tolerancePointer + "/maxFailingPixelFraction", 0.0, 1.0,
                             true, true);
        }
    }
    if (const json* deterministic = member(object, "deterministicClock")) {
        out.deterministicClock = readBool(*deterministic, pointer + "/deterministicClock");
    }
    if (const json* delta = member(object, "fixedDeltaSeconds")) {
        out.fixedDeltaSeconds = static_cast<float>(
            readRanged(*delta, pointer + "/fixedDeltaSeconds", 0.0, 3600.0, false, true));
    }
}

void readAssert(const json& value, const std::string& pointer) {
    const json& object = requireObject(value, pointer);
    rejectUnknownKeys(object, pointer, {"notes", "maxValidationMessages", "maxFrameMs"});
    if (const json* messages = member(object, "maxValidationMessages")) {
        (void)readInteger(*messages, pointer + "/maxValidationMessages", 0, 1000000);
    }
    if (const json* frameMs = member(object, "maxFrameMs")) {
        if (!frameMs->is_null()) {
            (void)readRanged(*frameMs, pointer + "/maxFrameMs", 0.0, 1.0e9, false, true);
        }
    }
}

bool isValidSceneId(const std::string& id) {
    if (id.empty()) return false;
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
               c == '-';
    });
}

void parse(const json& document, SceneScriptRef& out) {
    const json& root = requireObject(document, "");
    rejectUnknownKeys(root, "",
                      {"version", "id", "title", "notes", "seed", "camera", "time", "weather",
                       "render", "features", "content", "capture", "assert"});

    const json* version = member(root, "version");
    if (!version) fail("/version", "required");
    out.version = static_cast<int>(readInteger(*version, "/version", 1, 1));

    const json* id = member(root, "id");
    if (!id) fail("/id", "required");
    out.id = readString(*id, "/id");
    if (!isValidSceneId(out.id)) {
        fail("/id", "must match [a-z0-9._-]+, found \"" + out.id + "\"");
    }

    out.title = out.id;
    if (const json* title = member(root, "title")) out.title = readString(*title, "/title");
    if (const json* notes = member(root, "notes")) out.notes = readString(*notes, "/notes");
    if (const json* seed = member(root, "seed")) {
        out.seed = static_cast<uint64_t>(
            readInteger(*seed, "/seed", 0, static_cast<int64_t>(1) << 62));
    }

    if (const json* camera = member(root, "camera")) readCamera(*camera, "/camera", out);
    if (const json* time = member(root, "time")) readTime(*time, "/time", out);
    if (const json* weather = member(root, "weather")) readWeather(*weather, "/weather", out);
    if (const json* render = member(root, "render")) readRender(*render, "/render", out);
    if (const json* features = member(root, "features")) readFeatures(*features, "/features", out);
    if (const json* content = member(root, "content")) readContent(*content, "/content", out);
    if (const json* capture = member(root, "capture")) readCapture(*capture, "/capture", out);
    if (const json* assertions = member(root, "assert")) readAssert(*assertions, "/assert");

    // Fields this engine cannot express, reported whether or not the file
    // spelled them out. See solent's docs/PARITY.md for why each one is here.
    out.unappliedKeys.emplace_back(
        "/seed (no run-time seed input; placement comes from per-call-site constants and baked "
        "preprocessing output)");
    out.unappliedKeys.emplace_back(
        "/render/clearColor (the HDR pass clears to black and the sky pass covers it)");
    if (!out.vsync) {
        out.unappliedKeys.emplace_back("/render/vsync (present mode is fixed at FIFO)");
    }
    if (!out.camera.perspective) {
        out.unappliedKeys.emplace_back("/camera/projection (only perspective exists here)");
        out.unappliedKeys.emplace_back("/camera/orthoHeight (no orthographic projection)");
    }
}

}  // namespace

float SceneScriptRef::cameraYawDegForThisEngine() const {
    float yaw = std::fmod(camera.yawDeg + 90.0f, 360.0f);
    if (yaw < 0.0f) yaw += 360.0f;
    return yaw;
}

bool loadSceneScriptRef(const std::string& path, SceneScriptRef& out, std::string& error) {
    std::ifstream file(path);
    if (!file) {
        error = "cannot open scene script \"" + path + "\"";
        return false;
    }

    json document;
    try {
        // Strict: no comments, no trailing commas (ruling 39 of the format).
        document = json::parse(file, nullptr, true, false);
    } catch (const json::parse_error& e) {
        error = "scene script \"" + path + "\" is not strict JSON: " + e.what();
        return false;
    }

    SceneScriptRef parsed;
    try {
        parse(document, parsed);
    } catch (const ScriptError& e) {
        error = "scene script \"" + path + "\" " + e.what();
        return false;
    } catch (const json::exception& e) {
        error = "scene script \"" + path + "\": " + e.what();
        return false;
    }

    out = std::move(parsed);
    return true;
}

void reportUnappliedSceneScriptKeys(const SceneScriptRef& script, size_t alsoUnappliedCount) {
    // Loud on purpose. An unreported difference between the two engines is a
    // false parity result, which is worse than having no reference image.
    for (const std::string& key : script.unappliedKeys) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SceneScript: NOT APPLIED %s", key.c_str());
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SceneScript: \"%s\" has %zu field(s) this engine cannot honour; the reference "
                "image differs from solent's by that much before any renderer difference",
                script.id.c_str(), script.unappliedKeys.size() + alsoUnappliedCount);
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                "SceneScript: capture.golden, capture.tolerance and assert.* describe the "
                "comparison, not the render; the harness that invokes this capture owns them");
}

void addUnappliedCaptureKeysForInteractiveRun(SceneScriptRef& script) {
    // One entry per field, each carrying the value that was asked for and what
    // happens instead, because "the capture section was ignored" is not a
    // report a reader can act on.
    script.unappliedKeys.push_back(
        "/capture/frames (" + std::to_string(script.captureFrames) +
        "): no capture was requested, so no frame counter is armed and the loop runs until "
        "the window closes");
    script.unappliedKeys.push_back(
        "/capture/resolution (" + std::to_string(script.captureWidth) + "x" +
        std::to_string(script.captureHeight) +
        "): an interactive run opens the window at /render/resolution (" +
        std::to_string(script.width) + "x" + std::to_string(script.height) +
        ") because there is no swapchain readback to match a golden's size");
    if (script.deterministicClock) {
        script.unappliedKeys.push_back(
            "/capture/deterministicClock, /capture/fixedDeltaSeconds (" +
            std::to_string(script.fixedDeltaSeconds) +
            " s): only --capture pins the clock, so this run reads the wall clock and no two "
            "runs of it show the same world state at the same frame");
    }
}

void applySceneScriptRef(const SceneScriptRef& script, Camera& camera,
                         PerformanceToggles& toggles, RendererSystems& systems) {
    SDL_Log("SceneScript: applying \"%s\" (%s)", script.id.c_str(), script.title.c_str());
    if (!script.notes.empty()) {
        SDL_Log("SceneScript: notes: %s", script.notes.c_str());
    }

    // Camera. Yaw is offset by 90 degrees because solent's yaw 0 looks down +Z
    // and this engine's Camera yaw 0 looks down +X.
    camera.setPosition(script.camera.position);
    camera.setRotation(script.cameraYawDegForThisEngine(), script.camera.pitchDeg);
    camera.setFov(script.camera.fovYDeg);
    camera.setClipPlanes(script.camera.nearPlane, script.camera.farPlane);

    // Time. setTimeOfDay() pauses the cycle as a side effect, so the scale is
    // applied after it, not before.
    TimeSystem& time = systems.time();
    time.setDate(script.time.year, script.time.month, script.time.day);
    time.setTimeOfDay(script.time.timeOfDay);
    time.setCycleDuration(script.time.cycleDurationSeconds);
    time.setTimeScale(script.time.timeScale);
    time.setMoonPhaseOverride(script.time.moonPhase.has_value());
    if (script.time.moonPhase) {
        time.setMoonPhase(*script.time.moonPhase);
    }
    systems.celestial().setLocation(
        GeographicLocation{script.time.latitudeDeg, script.time.longitudeDeg});

    // Weather. "clear" is intensity zero rather than a third particle type.
    if (auto* weather = systems.registry().find<WeatherSystem>()) {
        using Kind = SceneScriptRef::WeatherRefState::Kind;
        weather->setWeatherType(script.weather.kind == Kind::Snow ? 1u : 0u);
        weather->setIntensity(script.weather.kind == Kind::Clear ? 0.0f : script.weather.intensity);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SceneScript: NOT APPLIED /weather/kind, /weather/intensity "
                    "(no weather system in this build)");
    }

    // Wind. windDirectionDeg is a compass-style bearing: 0 points down +Z and
    // increases toward +X, which is the same sense as the engine's own
    // atan2(x, z) facing convention. This engine's WindSystem takes a raw XZ
    // vector with no documented convention, so the mapping is fixed here and
    // in solent's docs/PARITY.md rather than being left to the caller.
    if (auto* wind = systems.registry().find<WindSystem>()) {
        const float radians = glm::radians(script.weather.windDirectionDeg);
        wind->setWindDirection(glm::vec2(std::sin(radians), std::cos(radians)));
        wind->setWindSpeed(script.weather.windSpeedMs);
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "SceneScript: NOT APPLIED /weather/windDirectionDeg, /weather/windSpeedMs "
                    "(no wind system in this build)");
    }

    systems.environmentControl().setCloudCoverage(script.weather.cloudCoverage);
    systems.environmentControl().setFogDensity(script.weather.fogDensity);

    // Feature toggles. The names are this engine's own
    // PerformanceToggles::getAllToggles() names, so there is no translation
    // table; solent declares five names that exist only there and they are
    // reported rather than silently dropped.
    size_t rejectedFeatures = 0;
    for (const auto& [name, enabled] : script.features) {
        if (toggles.setToggle(name, enabled)) {
            SDL_Log("SceneScript: feature %s = %s", name.c_str(), enabled ? "on" : "off");
        } else {
            ++rejectedFeatures;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "SceneScript: NOT APPLIED /features/%s (no such toggle in this engine)",
                        name.c_str());
        }
    }

    reportUnappliedSceneScriptKeys(script, rejectedFeatures);
}
