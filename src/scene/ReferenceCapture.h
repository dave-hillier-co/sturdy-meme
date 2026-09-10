#pragma once

#include <filesystem>

#include "SceneScriptRef.h"

class Application;

/**
 * ReferenceCapture - one deterministic run that writes one PNG.
 *
 * This is the parity oracle's output path. solent renders a scene script and
 * compares the result against goldens/<name>.png; this engine renders the same
 * script and writes goldens/oracle/<name>.png, and the difference between the
 * two is what a cross-engine parity check measures.
 *
 * It adds no second render loop. It pins the clock, arms the existing
 * SCREENSHOT_AFTER_FRAMES hook with an explicit output path, and runs
 * Application::run() - the same loop the interactive binary runs - for a known
 * number of frames. Two mains with two loops is the failure the rebuild exists
 * to avoid, and it applies to the oracle as much as to the engine.
 */
struct ReferenceCaptureRequest {
    SceneScriptRef script;
    std::filesystem::path outputPath;  // conventionally goldens/oracle/<name>.png
};

/**
 * Run the capture and return a process exit code: 0 when the PNG was written,
 * 1 otherwise. Calls Application::shutdown() before returning, because the PNG
 * is encoded on a worker thread that ScreenshotCapture joins at destruction;
 * shutdown() is idempotent, so main() may call it again.
 */
int runReferenceCapture(Application& app, const ReferenceCaptureRequest& request);
