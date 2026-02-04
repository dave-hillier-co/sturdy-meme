#pragma once

#include "ecs/World.h"
#include "ecs/Components.h"
#include <functional>
#include <vector>
#include <random>

namespace kitchen {

// Callback types for kitchen events
using OrderCallback = std::function<void(ecs::Entity orderEntity, const ecs::Order& order)>;
using ItemCallback = std::function<void(ecs::Entity orderEntity, uint32_t itemIndex, const ecs::OrderItem& item)>;

/**
 * KitchenSystem manages the complete kitchen workflow:
 * - Order creation and lifecycle (pending -> held -> fired -> cooking -> ready -> served)
 * - Station assignment and cooking progress
 * - Cook NPC work assignments
 * - Quality scoring and statistics
 */
class KitchenSystem {
public:
    KitchenSystem() = default;
    ~KitchenSystem() = default;

    // Non-copyable
    KitchenSystem(const KitchenSystem&) = delete;
    KitchenSystem& operator=(const KitchenSystem&) = delete;

    /**
     * Initialize the kitchen system with stations and optional NPCs.
     * Creates the kitchen state singleton entity.
     */
    void initialize(ecs::World& world);

    /**
     * Update all kitchen systems (called every frame).
     * - Advances cooking progress on active stations
     * - Auto-assigns fired items to available stations
     * - Updates cook and server NPCs
     * - Handles overcooked items
     */
    void update(ecs::World& world, float deltaTime);

    // =========================================================================
    // Order Management API
    // =========================================================================

    /**
     * Create a new order with the specified items.
     * @param world ECS world
     * @param tableNumber Customer table identifier
     * @param menuItemIds List of menu item indices to include
     * @param notes Optional special instructions
     * @return Entity handle for the new order
     */
    ecs::Entity createOrder(ecs::World& world, uint32_t tableNumber,
                            const std::vector<uint32_t>& menuItemIds,
                            const std::string& notes = "");

    /**
     * Create a random order for testing/simulation.
     * @param world ECS world
     * @param tableNumber Customer table identifier
     * @param minItems Minimum items in order
     * @param maxItems Maximum items in order
     */
    ecs::Entity createRandomOrder(ecs::World& world, uint32_t tableNumber,
                                  uint32_t minItems = 1, uint32_t maxItems = 4);

    /**
     * Hold an order - pauses before cooking starts.
     * Called when waiting for other orders from same table or timing coordination.
     */
    void holdOrder(ecs::World& world, ecs::Entity orderEntity);

    /**
     * Fire an order - signals cooking should begin.
     * Items will be assigned to available stations.
     */
    void fireOrder(ecs::World& world, ecs::Entity orderEntity);

    /**
     * Fire a specific item within an order.
     * Useful for coursed dining where items cook at different times.
     */
    void fireItem(ecs::World& world, ecs::Entity orderEntity, uint32_t itemIndex);

    /**
     * Mark an item as ready (cooking complete).
     * Called automatically when cook progress reaches 100%.
     */
    void markItemReady(ecs::World& world, ecs::Entity orderEntity, uint32_t itemIndex);

    /**
     * Serve an order (deliver to customer).
     * Should only be called when all items are ready.
     */
    void serveOrder(ecs::World& world, ecs::Entity orderEntity);

    /**
     * Cancel an order.
     * Releases any stations that were cooking items.
     */
    void cancelOrder(ecs::World& world, ecs::Entity orderEntity);

    // =========================================================================
    // Station Management API
    // =========================================================================

    /**
     * Add a new kitchen station.
     */
    ecs::Entity addStation(ecs::World& world, ecs::StationType type);

    /**
     * Manually assign an item to a specific station.
     * Returns false if station is not available or wrong type.
     */
    bool assignItemToStation(ecs::World& world, ecs::Entity orderEntity,
                             uint32_t itemIndex, ecs::Entity stationEntity);

    /**
     * Collect a cooked item from a station.
     * Called when cook/expeditor picks up the finished item.
     */
    void collectFromStation(ecs::World& world, ecs::Entity stationEntity);

    /**
     * Clean a station (reset from overcooked state).
     */
    void cleanStation(ecs::World& world, ecs::Entity stationEntity, float cleanTime = 2.0f);

    // =========================================================================
    // NPC Management API
    // =========================================================================

    /**
     * Add a cook NPC to the kitchen.
     */
    ecs::Entity addCook(ecs::World& world, const std::string& name, float skill = 1.0f);

    /**
     * Add a server NPC to the kitchen.
     */
    ecs::Entity addServer(ecs::World& world, const std::string& name, float speed = 1.0f);

    /**
     * Assign a cook to a station.
     */
    void assignCookToStation(ecs::World& world, ecs::Entity cookEntity, ecs::Entity stationEntity);

    // =========================================================================
    // Query API
    // =========================================================================

    /**
     * Get the kitchen state singleton.
     */
    ecs::KitchenState* getKitchenState(ecs::World& world);
    const ecs::KitchenState* getKitchenState(const ecs::World& world) const;

    /**
     * Find an order by its order ID.
     */
    ecs::Entity findOrderById(ecs::World& world, uint32_t orderId);

    /**
     * Get all orders at a specific status.
     */
    std::vector<ecs::Entity> getOrdersByStatus(ecs::World& world, ecs::OrderStatus status);

    /**
     * Get all orders for a specific table.
     */
    std::vector<ecs::Entity> getOrdersByTable(ecs::World& world, uint32_t tableNumber);

    /**
     * Find an available station of the specified type.
     */
    ecs::Entity findAvailableStation(ecs::World& world, ecs::StationType type);

    /**
     * Get all stations (optionally filtered by type).
     */
    std::vector<ecs::Entity> getStations(ecs::World& world,
                                         std::optional<ecs::StationType> type = std::nullopt);

    /**
     * Get current kitchen statistics.
     */
    struct KitchenStats {
        uint32_t pendingOrders = 0;
        uint32_t heldOrders = 0;
        uint32_t activeOrders = 0;
        uint32_t readyOrders = 0;
        uint32_t completedOrders = 0;
        uint32_t cancelledOrders = 0;
        uint32_t busyStations = 0;
        uint32_t availableStations = 0;
        float averageQuality = 0.0f;
        float averageWaitTime = 0.0f;
    };
    KitchenStats getStats(ecs::World& world) const;

    // =========================================================================
    // Event Callbacks
    // =========================================================================

    void setOnOrderCreated(OrderCallback callback) { onOrderCreated_ = std::move(callback); }
    void setOnOrderFired(OrderCallback callback) { onOrderFired_ = std::move(callback); }
    void setOnOrderReady(OrderCallback callback) { onOrderReady_ = std::move(callback); }
    void setOnOrderServed(OrderCallback callback) { onOrderServed_ = std::move(callback); }
    void setOnItemReady(ItemCallback callback) { onItemReady_ = std::move(callback); }
    void setOnItemOvercooked(ItemCallback callback) { onItemOvercooked_ = std::move(callback); }

    // =========================================================================
    // Configuration
    // =========================================================================

    void setOvercookThreshold(float seconds) { overcookThreshold_ = seconds; }
    float getOvercookThreshold() const { return overcookThreshold_; }

    void setAutoAssignEnabled(bool enabled) { autoAssignEnabled_ = enabled; }
    bool isAutoAssignEnabled() const { return autoAssignEnabled_; }

private:
    // Internal update systems
    void updateCookingProgress(ecs::World& world, float deltaTime);
    void autoAssignFiredItems(ecs::World& world);
    void updateNPCs(ecs::World& world, float deltaTime);
    void checkForOvercookedItems(ecs::World& world, float deltaTime);
    void updateOrderStatuses(ecs::World& world);

    // Find order entity that contains a specific order ID
    ecs::Entity findOrderEntityById(ecs::World& world, uint32_t orderId);

    // Configuration
    float overcookThreshold_ = 5.0f;  // Seconds past done before quality penalty
    bool autoAssignEnabled_ = true;   // Auto-assign fired items to stations

    // Callbacks
    OrderCallback onOrderCreated_;
    OrderCallback onOrderFired_;
    OrderCallback onOrderReady_;
    OrderCallback onOrderServed_;
    ItemCallback onItemReady_;
    ItemCallback onItemOvercooked_;

    // Random number generator for random orders
    std::mt19937 rng_{std::random_device{}()};

    // Kitchen state entity (cached for fast access)
    ecs::Entity kitchenStateEntity_ = ecs::NullEntity;
};

// Helper functions for status names
const char* getOrderStatusName(ecs::OrderStatus status);
const char* getStationTypeName(ecs::StationType type);
const char* getStationStateName(ecs::StationState state);

} // namespace kitchen
