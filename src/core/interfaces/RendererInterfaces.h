#pragma once

/**
 * Convenience header that includes all renderer interfaces.
 *
 * These interfaces decouple the GUI from the Renderer by providing
 * focused abstractions for each subsystem the GUI needs to control.
 */

#include "ITimeControl.h"
#include "IWeatherControl.h"
#include "IEnvironmentControl.h"
#include "IPostProcessControl.h"
#include "ITerrainControl.h"
#include "IWaterControl.h"
#include "ITreeControl.h"
#include "IDebugControl.h"
#include "IProfilerControl.h"
#include "IPerformanceControl.h"
#include "ISceneControl.h"
#include "IPlayerControl.h"
