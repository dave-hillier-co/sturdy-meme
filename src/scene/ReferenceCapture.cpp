#include "ReferenceCapture.h"

#include <SDL3/SDL.h>

#include <system_error>

#include "Application.h"
#include "atmosphere/TimeSystem.h"
#include "core/Renderer.h"
#include "core/RendererSystems.h"

namespace {

// The swapchain copy is recorded on the capture frame and only becomes host
// readable when that frame slot comes round again, which is
// TripleBuffering::DEFAULT_FRAME_COUNT frames later; one more frame lets
// pollCompleted() hand the staging buffer to the encoder. Six is that with
// margin, and the extra frames cannot change the captured image because the
// copy is already in the staging buffer.
constexpr uint64_t kReadbackFrames = 6;

}  // namespace

int runReferenceCapture(Application& app, const ReferenceCaptureRequest& request) {
    const SceneScriptRef& script = request.script;

    Renderer& renderer = app.getRenderer();

    // Pin the clock the shaders see. Application::run's fixed delta covers
    // physics, animation and camera smoothing; this covers the frame time and
    // elapsed time that reach the UBOs, which TimeSystem reads from the wall
    // clock of its own accord.
    if (script.deterministicClock) {
        renderer.getSystems().time().setFixedTimeStep(script.fixedDeltaSeconds);
        SDL_Log("ReferenceCapture: clock pinned at %.9f s per frame",
                static_cast<double>(script.fixedDeltaSeconds));
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "ReferenceCapture: capture.deterministicClock is false, so this image is "
                    "not reproducible and must not be committed as a reference");
    }

    renderer.setAutoScreenshot(script.captureFrames, request.outputPath);

    Application::RunOptions options;
    options.exitAfterFrames = static_cast<uint64_t>(script.captureFrames) + kReadbackFrames;
    options.hideGui = true;
    if (script.deterministicClock) {
        options.fixedDeltaSeconds = script.fixedDeltaSeconds;
    }

    SDL_Log("ReferenceCapture: rendering %u frame(s) of \"%s\", capturing to %s",
            script.captureFrames, script.id.c_str(), request.outputPath.string().c_str());

    app.run(options);

    // ScreenshotCapture joins its encoder thread at destruction, which happens
    // inside shutdown(); the file does not reliably exist before that.
    app.shutdown();

    std::error_code ec;
    if (!std::filesystem::exists(request.outputPath, ec)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "ReferenceCapture: no image at %s; the capture frame was never recorded",
                     request.outputPath.string().c_str());
        return 1;
    }

    SDL_Log("ReferenceCapture: wrote %s (%llu bytes)", request.outputPath.string().c_str(),
            static_cast<unsigned long long>(std::filesystem::file_size(request.outputPath, ec)));
    return 0;
}
