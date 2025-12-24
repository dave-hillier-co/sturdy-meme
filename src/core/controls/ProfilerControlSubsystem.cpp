#include "ProfilerControlSubsystem.h"
#include "Profiler.h"

Profiler& ProfilerControlSubsystem::getProfiler() {
    return profiler_;
}

const Profiler& ProfilerControlSubsystem::getProfiler() const {
    return profiler_;
}
