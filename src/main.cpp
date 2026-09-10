#include "Application.h"
#include "core/CrashHandler.h"
#include "scene/ReferenceCapture.h"
#include "scene/SceneScriptRef.h"
#include <SDL3/SDL.h>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

// Print available performance toggles
static void printUsage(const char* progName) {
    SDL_Log("Usage: %s [options]", progName);
    SDL_Log("");
    SDL_Log("Performance Toggle Options:");
    SDL_Log("  --disable <name>    Disable a specific toggle");
    SDL_Log("  --enable <name>     Enable a specific toggle");
    SDL_Log("  --minimal           Start with minimal rendering (sky + terrain + objects)");
    SDL_Log("  --list-toggles      List all available toggle names");
    SDL_Log("");
    SDL_Log("Toggle names (use with --disable/--enable):");
    SDL_Log("  Compute: terrainCompute, subdivisionCompute, grassCompute, weatherCompute,");
    SDL_Log("           snowCompute, leafCompute, foamCompute, cloudShadowCompute");
    SDL_Log("  HDR Draw: skyDraw, terrainDraw, catmullClarkDraw, sceneObjectsDraw,");
    SDL_Log("            skinnedCharacterDraw, treeEditDraw, grassDraw, waterDraw,");
    SDL_Log("            leavesDraw, weatherDraw, debugLinesDraw");
    SDL_Log("  Shadows: shadowPass, terrainShadows, grassShadows");
    SDL_Log("  Post: hiZPyramid, bloom");
    SDL_Log("  Other: froxelFog, atmosphereLUT, ssr, waterGBuffer, waterTileCull");
    SDL_Log("");
    SDL_Log("Parity Oracle Options:");
    SDL_Log("  --scene <path>      Load a solent scene script (.scene.json) and apply it");
    SDL_Log("  --capture <path>    Render capture.frames frames, write one PNG, exit");
    SDL_Log("  --frames <n>        Override the script's capture.frames");
    SDL_Log("  (use ABSOLUTE paths: run-debug.sh runs from inside the app bundle)");
    SDL_Log("");
    SDL_Log("Examples:");
    SDL_Log("  %s --disable grassCompute --disable grassDraw", progName);
    SDL_Log("  %s --minimal", progName);
    SDL_Log("  %s --scene /abs/path/solent/scenes/empty.scene.json \\", progName);
    SDL_Log("      --capture /abs/path/solent/goldens/oracle/zoo/clear_colour.png");
}

int main(int argc, char* argv[]) {
    installCrashHandler();

    // Parse command line arguments
    std::vector<std::pair<std::string, bool>> toggleChanges;
    bool minimalMode = false;
    bool listToggles = false;
    std::string scenePath;
    std::string capturePath;
    uint32_t frameOverride = 0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg == "--list-toggles") {
            listToggles = true;
        } else if (arg == "--minimal") {
            minimalMode = true;
        } else if (arg == "--disable" && i + 1 < argc) {
            toggleChanges.emplace_back(argv[++i], false);
        } else if (arg == "--enable" && i + 1 < argc) {
            toggleChanges.emplace_back(argv[++i], true);
        } else if (arg == "--scene" && i + 1 < argc) {
            scenePath = argv[++i];
        } else if (arg == "--capture" && i + 1 < argc) {
            capturePath = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            frameOverride = static_cast<uint32_t>(std::strtoul(argv[++i], nullptr, 10));
        }
    }

    if (listToggles) {
        printUsage(argv[0]);
        return 0;
    }

    if (capturePath.empty() && frameOverride != 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "--frames only affects --capture; ignoring it");
    }

    // Parity oracle: load the scene script before init so the window opens at
    // the resolution the script asks for. A capture must match solent's golden
    // pixel for pixel, so capture.resolution wins over render.resolution there;
    // an interactive --scene run uses render.resolution.
    SceneScriptRef script;
    bool haveScript = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    if (!scenePath.empty()) {
        std::string error;
        if (!loadSceneScriptRef(scenePath, script, error)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.c_str());
            return 1;
        }
        haveScript = true;
        if (frameOverride != 0) {
            SDL_Log("SceneScript: capture.frames overridden to %u", frameOverride);
            script.captureFrames = frameOverride;
        }
        if (capturePath.empty()) {
            windowWidth = static_cast<int>(script.width);
            windowHeight = static_cast<int>(script.height);
            // No capture: the whole capture section is unhonourable here, and
            // silently dropping a section is how a false parity result gets
            // made. Named now, logged with the rest by applySceneScriptRef.
            addUnappliedCaptureKeysForInteractiveRun(script);
        } else {
            windowWidth = static_cast<int>(script.captureWidth);
            windowHeight = static_cast<int>(script.captureHeight);
            if (script.width != script.captureWidth || script.height != script.captureHeight) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "SceneScript: NOT APPLIED /render/resolution (%ux%u); a capture reads "
                            "back the swapchain, so the window opens at /capture/resolution "
                            "(%ux%u) to match solent's golden size",
                            script.width, script.height, script.captureWidth,
                            script.captureHeight);
            }
        }
    } else if (!capturePath.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "--capture requires --scene");
        return 1;
    }

    Application app;

    if (!app.init("Vulkan Game", windowWidth, windowHeight)) {
        // init() releases what it created before returning false; shutdown()
        // is idempotent, so this only guards against a partially built state.
        app.shutdown();
        return 1;
    }

    // Apply performance toggle settings after init
    auto& toggles = app.getRenderer().getPerformanceToggles();

    // The script is applied after init because setupWorld() places the camera
    // itself while the loading screen presents, and before the command-line
    // toggles below so that --enable/--disable still override a script.
    if (haveScript) {
        applySceneScriptRef(script, app.getCamera(), toggles, app.getRenderer().getSystems());
    }

    if (minimalMode) {
        SDL_Log("Performance: Starting in minimal mode");
        toggles.disableAll();
        toggles.skyDraw = true;
        toggles.terrainDraw = true;
        toggles.sceneObjectsDraw = true;
    }

    for (const auto& [name, enabled] : toggleChanges) {
        if (toggles.setToggle(name, enabled)) {
            SDL_Log("Performance: %s %s", enabled ? "Enabled" : "Disabled", name.c_str());
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Unknown toggle: %s", name.c_str());
        }
    }

    if (!capturePath.empty()) {
        ReferenceCaptureRequest captureRequest;
        captureRequest.script = script;
        captureRequest.outputPath = std::filesystem::path(capturePath);
        const int result = runReferenceCapture(app, captureRequest);
        app.shutdown();  // idempotent; runReferenceCapture already called it
        return result;
    }

    app.run();
    app.shutdown();

    return 0;
}
