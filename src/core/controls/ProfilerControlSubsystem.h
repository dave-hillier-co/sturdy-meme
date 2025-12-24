#pragma once

#include "interfaces/IProfilerControl.h"

class Profiler;

/**
 * ProfilerControlSubsystem - Implements IProfilerControl
 * Provides access to the Profiler for timing information.
 */
class ProfilerControlSubsystem : public IProfilerControl {
public:
    explicit ProfilerControlSubsystem(Profiler& profiler)
        : profiler_(profiler) {}

    Profiler& getProfiler() override;
    const Profiler& getProfiler() const override;

private:
    Profiler& profiler_;
};
