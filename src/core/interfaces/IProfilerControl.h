#pragma once

class Profiler;

/**
 * Interface for profiler controls.
 * Used by GuiProfilerTab to display GPU/CPU timing information.
 */
class IProfilerControl {
public:
    virtual ~IProfilerControl() = default;

    // Profiler access
    virtual Profiler& getProfiler() = 0;
    virtual const Profiler& getProfiler() const = 0;
};
