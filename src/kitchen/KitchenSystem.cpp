#include "KitchenSystem.h"
#include <SDL3/SDL.h>
#include <algorithm>

namespace kitchen {

// Helper function implementations
const char* getOrderStatusName(ecs::OrderStatus status) {
    switch (status) {
        case ecs::OrderStatus::Pending: return "Pending";
        case ecs::OrderStatus::Held: return "Held";
        case ecs::OrderStatus::Fired: return "Fired";
        case ecs::OrderStatus::Cooking: return "Cooking";
        case ecs::OrderStatus::Ready: return "Ready";
        case ecs::OrderStatus::Served: return "Served";
        case ecs::OrderStatus::Cancelled: return "Cancelled";
    }
    return "Unknown";
}

const char* getStationTypeName(ecs::StationType type) {
    switch (type) {
        case ecs::StationType::Grill: return "Grill";
        case ecs::StationType::Fryer: return "Fryer";
        case ecs::StationType::Saute: return "Saute";
        case ecs::StationType::Prep: return "Prep";
        case ecs::StationType::Oven: return "Oven";
        case ecs::StationType::Dessert: return "Dessert";
    }
    return "Unknown";
}

const char* getStationStateName(ecs::StationState state) {
    switch (state) {
        case ecs::StationState::Idle: return "Idle";
        case ecs::StationState::Cooking: return "Cooking";
        case ecs::StationState::Overcooked: return "Overcooked";
        case ecs::StationState::Cleaning: return "Cleaning";
    }
    return "Unknown";
}

void KitchenSystem::initialize(ecs::World& world) {
    // Create kitchen state singleton entity
    kitchenStateEntity_ = world.create();
    world.add<ecs::KitchenState>(kitchenStateEntity_);
    world.add<ecs::KitchenStateTag>(kitchenStateEntity_);
    world.add<ecs::DebugName>(kitchenStateEntity_, "KitchenState");

    SDL_Log("KitchenSystem: Initialized with %zu menu items",
            world.get<ecs::KitchenState>(kitchenStateEntity_).menu.size());

    // Create default stations (one of each type)
    addStation(world, ecs::StationType::Grill);
    addStation(world, ecs::StationType::Fryer);
    addStation(world, ecs::StationType::Saute);
    addStation(world, ecs::StationType::Prep);
    addStation(world, ecs::StationType::Oven);
    addStation(world, ecs::StationType::Dessert);

    SDL_Log("KitchenSystem: Created 6 default stations");
}

void KitchenSystem::update(ecs::World& world, float deltaTime) {
    // Update game time in kitchen state
    auto* state = getKitchenState(world);
    if (state) {
        state->gameTime += deltaTime;
    }

    // Run all subsystems
    updateCookingProgress(world, deltaTime);
    autoAssignFiredItems(world);
    checkForOvercookedItems(world, deltaTime);
    updateOrderStatuses(world);
    updateNPCs(world, deltaTime);
}

// =============================================================================
// Order Management Implementation
// =============================================================================

ecs::Entity KitchenSystem::createOrder(ecs::World& world, uint32_t tableNumber,
                                        const std::vector<uint32_t>& menuItemIds,
                                        const std::string& notes) {
    auto* state = getKitchenState(world);
    if (!state) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "KitchenSystem: No kitchen state!");
        return ecs::NullEntity;
    }

    ecs::Entity orderEntity = world.create();

    ecs::Order order;
    order.orderId = state->generateOrderId();
    order.tableNumber = tableNumber;
    order.timeReceived = state->gameTime;
    order.notes = notes;

    // Add items
    for (uint32_t itemId : menuItemIds) {
        if (itemId < state->menu.size()) {
            order.items.emplace_back(itemId);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "KitchenSystem: Invalid menu item ID %u", itemId);
        }
    }

    world.add<ecs::Order>(orderEntity, std::move(order));
    world.add<ecs::OrderTag>(orderEntity);

    const auto& addedOrder = world.get<ecs::Order>(orderEntity);
    SDL_Log("KitchenSystem: Order #%u created for table %u with %zu items",
            addedOrder.orderId, tableNumber, addedOrder.items.size());

    if (onOrderCreated_) {
        onOrderCreated_(orderEntity, addedOrder);
    }

    return orderEntity;
}

ecs::Entity KitchenSystem::createRandomOrder(ecs::World& world, uint32_t tableNumber,
                                              uint32_t minItems, uint32_t maxItems) {
    auto* state = getKitchenState(world);
    if (!state || state->menu.empty()) {
        return ecs::NullEntity;
    }

    std::uniform_int_distribution<uint32_t> itemCountDist(minItems, maxItems);
    std::uniform_int_distribution<uint32_t> itemDist(0, static_cast<uint32_t>(state->menu.size() - 1));

    uint32_t itemCount = itemCountDist(rng_);
    std::vector<uint32_t> items;
    items.reserve(itemCount);

    for (uint32_t i = 0; i < itemCount; ++i) {
        items.push_back(itemDist(rng_));
    }

    return createOrder(world, tableNumber, items);
}

void KitchenSystem::holdOrder(ecs::World& world, ecs::Entity orderEntity) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);
    auto* state = getKitchenState(world);

    // Only pending orders can be held
    if (order.overallStatus() != ecs::OrderStatus::Pending) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "KitchenSystem: Cannot hold order #%u - not in pending state",
                   order.orderId);
        return;
    }

    // Update all pending items to held
    for (auto& item : order.items) {
        if (item.status == ecs::OrderStatus::Pending) {
            item.status = ecs::OrderStatus::Held;
        }
    }
    order.timeHeld = state ? state->gameTime : 0.0f;

    SDL_Log("KitchenSystem: Order #%u held", order.orderId);
}

void KitchenSystem::fireOrder(ecs::World& world, ecs::Entity orderEntity) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);
    auto* state = getKitchenState(world);

    // Can fire from pending or held state
    auto status = order.overallStatus();
    if (status != ecs::OrderStatus::Pending && status != ecs::OrderStatus::Held) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "KitchenSystem: Cannot fire order #%u - already in progress",
                   order.orderId);
        return;
    }

    // Update all pending/held items to fired
    for (auto& item : order.items) {
        if (item.status == ecs::OrderStatus::Pending ||
            item.status == ecs::OrderStatus::Held) {
            item.status = ecs::OrderStatus::Fired;
        }
    }
    order.timeFired = state ? state->gameTime : 0.0f;

    SDL_Log("KitchenSystem: Order #%u FIRED! (table %u)", order.orderId, order.tableNumber);

    if (onOrderFired_) {
        onOrderFired_(orderEntity, order);
    }
}

void KitchenSystem::fireItem(ecs::World& world, ecs::Entity orderEntity, uint32_t itemIndex) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);

    if (itemIndex >= order.items.size()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "KitchenSystem: Invalid item index %u for order #%u",
                   itemIndex, order.orderId);
        return;
    }

    auto& item = order.items[itemIndex];
    if (item.status == ecs::OrderStatus::Pending ||
        item.status == ecs::OrderStatus::Held) {
        item.status = ecs::OrderStatus::Fired;

        auto* state = getKitchenState(world);
        if (state) {
            SDL_Log("KitchenSystem: Item %u (%s) FIRED on order #%u",
                   itemIndex, state->menu[item.menuItemId].name.c_str(), order.orderId);
        }
    }
}

void KitchenSystem::markItemReady(ecs::World& world, ecs::Entity orderEntity, uint32_t itemIndex) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);

    if (itemIndex >= order.items.size()) {
        return;
    }

    auto& item = order.items[itemIndex];
    if (item.status == ecs::OrderStatus::Cooking) {
        item.status = ecs::OrderStatus::Ready;
        item.cookProgress = 1.0f;

        if (onItemReady_) {
            onItemReady_(orderEntity, itemIndex, item);
        }

        // Check if all items are ready
        if (order.allItemsAtLeast(ecs::OrderStatus::Ready)) {
            auto* state = getKitchenState(world);
            order.timeCompleted = state ? state->gameTime : 0.0f;

            SDL_Log("KitchenSystem: Order #%u is READY!", order.orderId);

            if (onOrderReady_) {
                onOrderReady_(orderEntity, order);
            }
        }
    }
}

void KitchenSystem::serveOrder(ecs::World& world, ecs::Entity orderEntity) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);

    // All items must be ready
    if (!order.allItemsAtLeast(ecs::OrderStatus::Ready)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "KitchenSystem: Cannot serve order #%u - not all items ready",
                   order.orderId);
        return;
    }

    // Mark all items as served
    for (auto& item : order.items) {
        item.status = ecs::OrderStatus::Served;
    }

    // Update statistics
    auto* state = getKitchenState(world);
    if (state) {
        state->ordersCompleted++;

        // Calculate average quality
        float orderQuality = 0.0f;
        for (const auto& item : order.items) {
            orderQuality += item.qualityModifier;
        }
        orderQuality /= static_cast<float>(order.items.size());
        state->totalQuality += orderQuality;

        // Update average wait time
        float waitTime = order.timeCompleted - order.timeReceived;
        state->averageWaitTime = (state->averageWaitTime * (state->ordersCompleted - 1) + waitTime)
                                 / static_cast<float>(state->ordersCompleted);
    }

    SDL_Log("KitchenSystem: Order #%u SERVED to table %u", order.orderId, order.tableNumber);

    if (onOrderServed_) {
        onOrderServed_(orderEntity, order);
    }
}

void KitchenSystem::cancelOrder(ecs::World& world, ecs::Entity orderEntity) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return;
    }

    auto& order = world.get<ecs::Order>(orderEntity);

    // Release any stations cooking this order's items
    auto stationView = world.view<ecs::KitchenStation>();
    for (auto [entity, station] : stationView.each()) {
        if (station.currentOrderId == order.orderId) {
            station.finishCooking();
        }
    }

    // Mark all items as cancelled
    for (auto& item : order.items) {
        item.status = ecs::OrderStatus::Cancelled;
    }

    auto* state = getKitchenState(world);
    if (state) {
        state->ordersCancelled++;
    }

    SDL_Log("KitchenSystem: Order #%u CANCELLED", order.orderId);
}

// =============================================================================
// Station Management Implementation
// =============================================================================

ecs::Entity KitchenSystem::addStation(ecs::World& world, ecs::StationType type) {
    // Count existing stations of this type
    uint32_t typeCount = 0;
    auto view = world.view<ecs::KitchenStation>();
    for (auto [entity, station] : view.each()) {
        if (station.type == type) {
            typeCount++;
        }
    }

    ecs::Entity stationEntity = world.create();
    world.add<ecs::KitchenStation>(stationEntity, type, typeCount);
    world.add<ecs::KitchenStationTag>(stationEntity);

    SDL_Log("KitchenSystem: Added %s station #%u", getStationTypeName(type), typeCount);

    return stationEntity;
}

bool KitchenSystem::assignItemToStation(ecs::World& world, ecs::Entity orderEntity,
                                         uint32_t itemIndex, ecs::Entity stationEntity) {
    if (!world.valid(orderEntity) || !world.has<ecs::Order>(orderEntity)) {
        return false;
    }
    if (!world.valid(stationEntity) || !world.has<ecs::KitchenStation>(stationEntity)) {
        return false;
    }

    auto& order = world.get<ecs::Order>(orderEntity);
    auto& station = world.get<ecs::KitchenStation>(stationEntity);

    if (itemIndex >= order.items.size()) {
        return false;
    }

    auto& item = order.items[itemIndex];

    // Check station is available
    if (!station.isAvailable()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                   "KitchenSystem: Station not available for item assignment");
        return false;
    }

    // Check station type matches item requirement
    auto* state = getKitchenState(world);
    if (state && item.menuItemId < state->menu.size()) {
        const auto& menuItem = state->menu[item.menuItemId];
        if (menuItem.stationType != static_cast<uint32_t>(station.type)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                       "KitchenSystem: Station type mismatch - %s needs %s",
                       menuItem.name.c_str(),
                       getStationTypeName(static_cast<ecs::StationType>(menuItem.stationType)));
            return false;
        }
    }

    // Assign item to station
    station.startCooking(order.orderId, itemIndex);
    item.status = ecs::OrderStatus::Cooking;
    item.assignedStation = station.stationIndex;

    if (state && item.menuItemId < state->menu.size()) {
        SDL_Log("KitchenSystem: %s assigned to %s station",
               state->menu[item.menuItemId].name.c_str(),
               getStationTypeName(station.type));
    }

    return true;
}

void KitchenSystem::collectFromStation(ecs::World& world, ecs::Entity stationEntity) {
    if (!world.valid(stationEntity) || !world.has<ecs::KitchenStation>(stationEntity)) {
        return;
    }

    auto& station = world.get<ecs::KitchenStation>(stationEntity);

    if (station.state != ecs::StationState::Cooking &&
        station.state != ecs::StationState::Overcooked) {
        return;
    }

    // Find the order and mark item ready
    ecs::Entity orderEntity = findOrderEntityById(world, station.currentOrderId);
    if (orderEntity != ecs::NullEntity && station.currentItemIndex != UINT32_MAX) {
        markItemReady(world, orderEntity, station.currentItemIndex);
    }

    station.finishCooking();
}

void KitchenSystem::cleanStation(ecs::World& world, ecs::Entity stationEntity, float cleanTime) {
    if (!world.valid(stationEntity) || !world.has<ecs::KitchenStation>(stationEntity)) {
        return;
    }

    auto& station = world.get<ecs::KitchenStation>(stationEntity);

    if (station.state == ecs::StationState::Overcooked) {
        // For now, instant clean - could add timer later
        station.finishCooking();
        SDL_Log("KitchenSystem: %s station cleaned",
               getStationTypeName(station.type));
    }
}

// =============================================================================
// NPC Management Implementation
// =============================================================================

ecs::Entity KitchenSystem::addCook(ecs::World& world, const std::string& name, float skill) {
    ecs::Entity cookEntity = world.create();
    world.add<ecs::CookNPC>(cookEntity, name, skill);
    world.add<ecs::CookTag>(cookEntity);
    world.add<ecs::DebugName>(cookEntity, name.c_str());

    SDL_Log("KitchenSystem: Added cook '%s' (skill: %.1f)", name.c_str(), skill);

    return cookEntity;
}

ecs::Entity KitchenSystem::addServer(ecs::World& world, const std::string& name, float speed) {
    ecs::Entity serverEntity = world.create();
    world.add<ecs::ServerNPC>(serverEntity, name, speed);
    world.add<ecs::ServerTag>(serverEntity);
    world.add<ecs::DebugName>(serverEntity, name.c_str());

    SDL_Log("KitchenSystem: Added server '%s' (speed: %.1f)", name.c_str(), speed);

    return serverEntity;
}

void KitchenSystem::assignCookToStation(ecs::World& world, ecs::Entity cookEntity,
                                         ecs::Entity stationEntity) {
    if (!world.valid(cookEntity) || !world.has<ecs::CookNPC>(cookEntity)) {
        return;
    }
    if (!world.valid(stationEntity) || !world.has<ecs::KitchenStation>(stationEntity)) {
        return;
    }

    auto& cook = world.get<ecs::CookNPC>(cookEntity);
    auto& station = world.get<ecs::KitchenStation>(stationEntity);

    cook.assignedStation = station.stationIndex;

    SDL_Log("KitchenSystem: Cook '%s' assigned to %s station",
           cook.name.c_str(), getStationTypeName(station.type));
}

// =============================================================================
// Query Implementation
// =============================================================================

ecs::KitchenState* KitchenSystem::getKitchenState(ecs::World& world) {
    if (kitchenStateEntity_ != ecs::NullEntity && world.valid(kitchenStateEntity_)) {
        return world.tryGet<ecs::KitchenState>(kitchenStateEntity_);
    }

    // Fallback: search for it
    auto view = world.view<ecs::KitchenState>();
    for (auto entity : view) {
        kitchenStateEntity_ = entity;
        return &world.get<ecs::KitchenState>(entity);
    }

    return nullptr;
}

const ecs::KitchenState* KitchenSystem::getKitchenState(const ecs::World& world) const {
    if (kitchenStateEntity_ != ecs::NullEntity && world.valid(kitchenStateEntity_)) {
        return world.tryGet<ecs::KitchenState>(kitchenStateEntity_);
    }
    return nullptr;
}

ecs::Entity KitchenSystem::findOrderById(ecs::World& world, uint32_t orderId) {
    return findOrderEntityById(world, orderId);
}

ecs::Entity KitchenSystem::findOrderEntityById(ecs::World& world, uint32_t orderId) {
    auto view = world.view<ecs::Order>();
    for (auto [entity, order] : view.each()) {
        if (order.orderId == orderId) {
            return entity;
        }
    }
    return ecs::NullEntity;
}

std::vector<ecs::Entity> KitchenSystem::getOrdersByStatus(ecs::World& world, ecs::OrderStatus status) {
    std::vector<ecs::Entity> result;
    auto view = world.view<ecs::Order>();
    for (auto [entity, order] : view.each()) {
        if (order.overallStatus() == status) {
            result.push_back(entity);
        }
    }
    return result;
}

std::vector<ecs::Entity> KitchenSystem::getOrdersByTable(ecs::World& world, uint32_t tableNumber) {
    std::vector<ecs::Entity> result;
    auto view = world.view<ecs::Order>();
    for (auto [entity, order] : view.each()) {
        if (order.tableNumber == tableNumber) {
            result.push_back(entity);
        }
    }
    return result;
}

ecs::Entity KitchenSystem::findAvailableStation(ecs::World& world, ecs::StationType type) {
    auto view = world.view<ecs::KitchenStation>();
    for (auto [entity, station] : view.each()) {
        if (station.type == type && station.isAvailable()) {
            return entity;
        }
    }
    return ecs::NullEntity;
}

std::vector<ecs::Entity> KitchenSystem::getStations(ecs::World& world,
                                                     std::optional<ecs::StationType> type) {
    std::vector<ecs::Entity> result;
    auto view = world.view<ecs::KitchenStation>();
    for (auto [entity, station] : view.each()) {
        if (!type.has_value() || station.type == type.value()) {
            result.push_back(entity);
        }
    }
    return result;
}

KitchenSystem::KitchenStats KitchenSystem::getStats(ecs::World& world) const {
    KitchenStats stats;

    // Count orders by status
    auto orderView = world.view<ecs::Order>();
    for (auto [entity, order] : orderView.each()) {
        switch (order.overallStatus()) {
            case ecs::OrderStatus::Pending: stats.pendingOrders++; break;
            case ecs::OrderStatus::Held: stats.heldOrders++; break;
            case ecs::OrderStatus::Fired:
            case ecs::OrderStatus::Cooking: stats.activeOrders++; break;
            case ecs::OrderStatus::Ready: stats.readyOrders++; break;
            case ecs::OrderStatus::Served: stats.completedOrders++; break;
            case ecs::OrderStatus::Cancelled: stats.cancelledOrders++; break;
        }
    }

    // Count station states
    auto stationView = world.view<ecs::KitchenStation>();
    for (auto [entity, station] : stationView.each()) {
        if (station.isAvailable()) {
            stats.availableStations++;
        } else {
            stats.busyStations++;
        }
    }

    // Get quality stats from kitchen state
    const auto* state = getKitchenState(world);
    if (state && state->ordersCompleted > 0) {
        stats.averageQuality = state->totalQuality / static_cast<float>(state->ordersCompleted);
        stats.averageWaitTime = state->averageWaitTime;
    }

    return stats;
}

// =============================================================================
// Internal Update Systems
// =============================================================================

void KitchenSystem::updateCookingProgress(ecs::World& world, float deltaTime) {
    auto* state = getKitchenState(world);
    if (!state) return;

    auto stationView = world.view<ecs::KitchenStation>();
    for (auto [stationEntity, station] : stationView.each()) {
        if (station.state != ecs::StationState::Cooking) {
            continue;
        }

        // Find the order and item being cooked
        ecs::Entity orderEntity = findOrderEntityById(world, station.currentOrderId);
        if (orderEntity == ecs::NullEntity) {
            station.finishCooking();
            continue;
        }

        auto& order = world.get<ecs::Order>(orderEntity);
        if (station.currentItemIndex >= order.items.size()) {
            station.finishCooking();
            continue;
        }

        auto& item = order.items[station.currentItemIndex];
        const auto& menuItem = state->menu[item.menuItemId];

        // Calculate cook speed (affected by station and assigned cook)
        float cookSpeed = station.speedModifier / menuItem.cookTime;

        // Apply cook skill if assigned
        auto cookView = world.view<ecs::CookNPC>();
        for (auto [cookEntity, cook] : cookView.each()) {
            if (cook.assignedStation == station.stationIndex) {
                cookSpeed *= cook.skill;
                break;
            }
        }

        // Update progress
        station.cookProgress += cookSpeed * deltaTime;
        item.cookProgress = station.cookProgress;

        // Check if done
        if (station.cookProgress >= 1.0f) {
            station.cookProgress = 1.0f;
            item.cookProgress = 1.0f;
            // Don't auto-complete - wait for collection or overcook
        }
    }
}

void KitchenSystem::autoAssignFiredItems(ecs::World& world) {
    if (!autoAssignEnabled_) return;

    auto* state = getKitchenState(world);
    if (!state) return;

    // Find all fired items that need stations
    auto orderView = world.view<ecs::Order>();
    for (auto [orderEntity, order] : orderView.each()) {
        for (uint32_t i = 0; i < order.items.size(); ++i) {
            auto& item = order.items[i];
            if (item.status != ecs::OrderStatus::Fired) {
                continue;
            }

            // Find available station of the right type
            const auto& menuItem = state->menu[item.menuItemId];
            auto stationType = static_cast<ecs::StationType>(menuItem.stationType);
            ecs::Entity stationEntity = findAvailableStation(world, stationType);

            if (stationEntity != ecs::NullEntity) {
                assignItemToStation(world, orderEntity, i, stationEntity);
            }
        }
    }
}

void KitchenSystem::checkForOvercookedItems(ecs::World& world, float deltaTime) {
    auto stationView = world.view<ecs::KitchenStation>();
    for (auto [stationEntity, station] : stationView.each()) {
        if (station.state != ecs::StationState::Cooking) {
            continue;
        }

        // Check if cooking is complete but item not collected
        if (station.cookProgress >= 1.0f) {
            station.overcookTimer += deltaTime;

            if (station.overcookTimer > overcookThreshold_) {
                station.state = ecs::StationState::Overcooked;

                // Apply quality penalty to item
                ecs::Entity orderEntity = findOrderEntityById(world, station.currentOrderId);
                if (orderEntity != ecs::NullEntity) {
                    auto& order = world.get<ecs::Order>(orderEntity);
                    if (station.currentItemIndex < order.items.size()) {
                        auto& item = order.items[station.currentItemIndex];
                        float penalty = 1.0f - std::min(1.0f, station.overcookTimer / 10.0f);
                        item.qualityModifier *= penalty;

                        if (onItemOvercooked_) {
                            onItemOvercooked_(orderEntity, station.currentItemIndex, item);
                        }

                        auto* state = getKitchenState(world);
                        if (state && item.menuItemId < state->menu.size()) {
                            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                       "KitchenSystem: %s OVERCOOKED! (quality: %.0f%%)",
                                       state->menu[item.menuItemId].name.c_str(),
                                       item.qualityModifier * 100.0f);
                        }
                    }
                }
            }
        }
    }
}

void KitchenSystem::updateOrderStatuses(ecs::World& world) {
    // This handles state transitions when all items reach certain states
    // Most transitions are handled explicitly, but this catches edge cases
}

void KitchenSystem::updateNPCs(ecs::World& world, float deltaTime) {
    // Update cook stamina
    auto cookView = world.view<ecs::CookNPC>();
    for (auto [entity, cook] : cookView.each()) {
        // Slow stamina drain when assigned to a station
        if (cook.assignedStation != UINT32_MAX) {
            cook.stamina = std::max(0.0f, cook.stamina - 0.001f * deltaTime);
        } else {
            // Recover stamina when not working
            cook.stamina = std::min(1.0f, cook.stamina + 0.01f * deltaTime);
        }
    }

    // Server NPCs would handle order delivery here
    // For now, just a placeholder
}

} // namespace kitchen
