#pragma once

#include "ecs/World.h"
#include "kitchen/KitchenSystem.h"

/**
 * Kitchen control interface for GUI interactions.
 * Allows the GUI to interact with the kitchen system without direct coupling.
 */
class IKitchenControl {
public:
    virtual ~IKitchenControl() = default;

    virtual kitchen::KitchenSystem& getKitchenSystem() = 0;
    virtual ecs::World& getECSWorld() = 0;

    // Simulation controls
    virtual bool isKitchenSimulationEnabled() const = 0;
    virtual void setKitchenSimulationEnabled(bool enabled) = 0;
    virtual float getOrderSpawnRate() const = 0;
    virtual void setOrderSpawnRate(float rate) = 0;
};

/**
 * State for the kitchen tab UI.
 */
struct KitchenTabState {
    // Order creation
    uint32_t newOrderTable = 1;
    bool itemSelections[8] = {false};  // Menu item selections

    // Auto-simulation
    bool autoSpawnOrders = false;
    float orderSpawnTimer = 0.0f;
    float orderSpawnInterval = 10.0f;
    uint32_t nextAutoTable = 1;

    // View filters
    bool showPendingOrders = true;
    bool showHeldOrders = true;
    bool showActiveOrders = true;
    bool showReadyOrders = true;
    bool showCompletedOrders = false;

    // Selected order for detail view
    uint32_t selectedOrderId = 0;
};

namespace GuiKitchenTab {
    /**
     * Render the kitchen tab UI.
     * @param control Kitchen control interface
     * @param state Tab state (persists between frames)
     * @param deltaTime Frame delta time for animations/timers
     */
    void render(IKitchenControl& control, KitchenTabState& state, float deltaTime);

    /**
     * Render just the order queue panel (can be used standalone).
     */
    void renderOrderQueue(IKitchenControl& control, KitchenTabState& state);

    /**
     * Render just the stations panel (can be used standalone).
     */
    void renderStations(IKitchenControl& control);

    /**
     * Render just the statistics panel.
     */
    void renderStats(IKitchenControl& control);
}
