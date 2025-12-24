#pragma once

struct PerformanceToggles;

/**
 * Interface for performance toggle controls.
 * Used by GuiPerformanceTab to enable/disable rendering subsystems.
 */
class IPerformanceControl {
public:
    virtual ~IPerformanceControl() = default;

    // Performance toggles access
    virtual PerformanceToggles& getPerformanceToggles() = 0;
    virtual const PerformanceToggles& getPerformanceToggles() const = 0;

    // Sync toggle state to renderer
    virtual void syncPerformanceToggles() = 0;
};
