#include "GuiKitchenTab.h"
#include <imgui.h>
#include <algorithm>

namespace {

// Color helpers for order status
ImVec4 getStatusColor(ecs::OrderStatus status) {
    switch (status) {
        case ecs::OrderStatus::Pending:   return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);  // Gray
        case ecs::OrderStatus::Held:      return ImVec4(1.0f, 0.8f, 0.2f, 1.0f);  // Yellow
        case ecs::OrderStatus::Fired:     return ImVec4(1.0f, 0.5f, 0.0f, 1.0f);  // Orange
        case ecs::OrderStatus::Cooking:   return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);  // Red
        case ecs::OrderStatus::Ready:     return ImVec4(0.3f, 1.0f, 0.3f, 1.0f);  // Green
        case ecs::OrderStatus::Served:    return ImVec4(0.5f, 0.8f, 1.0f, 1.0f);  // Blue
        case ecs::OrderStatus::Cancelled: return ImVec4(0.4f, 0.4f, 0.4f, 1.0f);  // Dark Gray
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

ImVec4 getStationStateColor(ecs::StationState state) {
    switch (state) {
        case ecs::StationState::Idle:      return ImVec4(0.3f, 0.8f, 0.3f, 1.0f);  // Green
        case ecs::StationState::Cooking:   return ImVec4(1.0f, 0.6f, 0.2f, 1.0f);  // Orange
        case ecs::StationState::Overcooked: return ImVec4(1.0f, 0.2f, 0.2f, 1.0f); // Red
        case ecs::StationState::Cleaning:  return ImVec4(0.5f, 0.5f, 1.0f, 1.0f);  // Blue
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
}

// Progress bar with custom colors
void drawProgressBar(float progress, const ImVec4& color, const char* overlay = nullptr) {
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(progress, ImVec2(-1, 0), overlay);
    ImGui::PopStyleColor();
}

} // anonymous namespace

void GuiKitchenTab::render(IKitchenControl& control, KitchenTabState& state, float deltaTime) {
    ImGui::Spacing();

    // Header with simulation controls
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.4f, 1.0f));
    ImGui::Text("KITCHEN ORDER MANAGEMENT");
    ImGui::PopStyleColor();

    ImGui::Separator();

    // Auto-simulation controls
    if (ImGui::CollapsingHeader("Simulation Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool simEnabled = control.isKitchenSimulationEnabled();
        if (ImGui::Checkbox("Enable Kitchen Simulation", &simEnabled)) {
            control.setKitchenSimulationEnabled(simEnabled);
        }

        ImGui::Checkbox("Auto-Spawn Orders", &state.autoSpawnOrders);
        if (state.autoSpawnOrders) {
            ImGui::SliderFloat("Spawn Interval (s)", &state.orderSpawnInterval, 3.0f, 30.0f);

            state.orderSpawnTimer += deltaTime;
            if (state.orderSpawnTimer >= state.orderSpawnInterval) {
                state.orderSpawnTimer = 0.0f;
                auto& kitchen = control.getKitchenSystem();
                auto& world = control.getECSWorld();

                // Create and immediately fire a random order
                ecs::Entity orderEntity = kitchen.createRandomOrder(world, state.nextAutoTable, 1, 3);
                if (orderEntity != ecs::NullEntity) {
                    kitchen.fireOrder(world, orderEntity);
                }
                state.nextAutoTable = (state.nextAutoTable % 10) + 1;
            }

            ImGui::ProgressBar(state.orderSpawnTimer / state.orderSpawnInterval,
                              ImVec2(-1, 0), "Next Order");
        }

        ImGui::Spacing();
    }

    // Create new order section
    if (ImGui::CollapsingHeader("Create Order", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& kitchen = control.getKitchenSystem();
        auto& world = control.getECSWorld();
        auto* kitchenState = kitchen.getKitchenState(world);

        if (kitchenState) {
            ImGui::InputScalar("Table Number", ImGuiDataType_U32, &state.newOrderTable);

            ImGui::Text("Select Items:");
            ImGui::Indent();

            int numItems = std::min(static_cast<int>(kitchenState->menu.size()), 8);
            for (int i = 0; i < numItems; ++i) {
                const auto& menuItem = kitchenState->menu[i];
                char label[64];
                snprintf(label, sizeof(label), "%s (%.0fs)", menuItem.name.c_str(), menuItem.cookTime);
                ImGui::Checkbox(label, &state.itemSelections[i]);
            }

            ImGui::Unindent();

            // Count selected items
            int selectedCount = 0;
            for (int i = 0; i < numItems; ++i) {
                if (state.itemSelections[i]) selectedCount++;
            }

            if (selectedCount > 0) {
                ImGui::Spacing();

                if (ImGui::Button("Create & Hold", ImVec2(120, 0))) {
                    std::vector<uint32_t> items;
                    for (int i = 0; i < numItems; ++i) {
                        if (state.itemSelections[i]) {
                            items.push_back(static_cast<uint32_t>(i));
                            state.itemSelections[i] = false;
                        }
                    }
                    ecs::Entity orderEntity = kitchen.createOrder(world, state.newOrderTable, items);
                    if (orderEntity != ecs::NullEntity) {
                        kitchen.holdOrder(world, orderEntity);
                    }
                }
                ImGui::SameLine();

                if (ImGui::Button("Create & Fire", ImVec2(120, 0))) {
                    std::vector<uint32_t> items;
                    for (int i = 0; i < numItems; ++i) {
                        if (state.itemSelections[i]) {
                            items.push_back(static_cast<uint32_t>(i));
                            state.itemSelections[i] = false;
                        }
                    }
                    ecs::Entity orderEntity = kitchen.createOrder(world, state.newOrderTable, items);
                    if (orderEntity != ecs::NullEntity) {
                        kitchen.fireOrder(world, orderEntity);
                    }
                }
            } else {
                ImGui::TextDisabled("Select at least one item");
            }
        }

        ImGui::Spacing();
    }

    // Main panels
    if (ImGui::BeginTable("KitchenLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Orders", ImGuiTableColumnFlags_WidthStretch, 0.6f);
        ImGui::TableSetupColumn("Stations", ImGuiTableColumnFlags_WidthStretch, 0.4f);

        ImGui::TableNextRow();

        ImGui::TableNextColumn();
        if (ImGui::CollapsingHeader("Order Queue", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderOrderQueue(control, state);
        }

        ImGui::TableNextColumn();
        if (ImGui::CollapsingHeader("Kitchen Stations", ImGuiTreeNodeFlags_DefaultOpen)) {
            renderStations(control);
        }

        ImGui::EndTable();
    }

    // Statistics footer
    ImGui::Separator();
    renderStats(control);
}

void GuiKitchenTab::renderOrderQueue(IKitchenControl& control, KitchenTabState& state) {
    auto& kitchen = control.getKitchenSystem();
    auto& world = control.getECSWorld();
    auto* kitchenState = kitchen.getKitchenState(world);

    if (!kitchenState) {
        ImGui::TextDisabled("Kitchen not initialized");
        return;
    }

    // Filter checkboxes
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("Filters:");
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::Checkbox("Pending", &state.showPendingOrders);
    ImGui::SameLine();
    ImGui::Checkbox("Held", &state.showHeldOrders);
    ImGui::SameLine();
    ImGui::Checkbox("Active", &state.showActiveOrders);
    ImGui::SameLine();
    ImGui::Checkbox("Ready", &state.showReadyOrders);
    ImGui::SameLine();
    ImGui::Checkbox("Done", &state.showCompletedOrders);

    ImGui::Spacing();

    // Order list
    auto orderView = world.view<ecs::Order>();

    // Sort orders by status and time
    std::vector<std::pair<ecs::Entity, const ecs::Order*>> orders;
    for (auto [entity, order] : orderView.each()) {
        auto status = order.overallStatus();
        bool show = false;
        switch (status) {
            case ecs::OrderStatus::Pending: show = state.showPendingOrders; break;
            case ecs::OrderStatus::Held: show = state.showHeldOrders; break;
            case ecs::OrderStatus::Fired:
            case ecs::OrderStatus::Cooking: show = state.showActiveOrders; break;
            case ecs::OrderStatus::Ready: show = state.showReadyOrders; break;
            case ecs::OrderStatus::Served:
            case ecs::OrderStatus::Cancelled: show = state.showCompletedOrders; break;
        }
        if (show) {
            orders.emplace_back(entity, &order);
        }
    }

    // Sort: active first, then by time
    std::sort(orders.begin(), orders.end(), [](const auto& a, const auto& b) {
        auto statusA = a.second->overallStatus();
        auto statusB = b.second->overallStatus();
        if (statusA != statusB) {
            // Priority: Cooking > Fired > Ready > Held > Pending > Served > Cancelled
            auto priority = [](ecs::OrderStatus s) -> int {
                switch (s) {
                    case ecs::OrderStatus::Cooking: return 0;
                    case ecs::OrderStatus::Fired: return 1;
                    case ecs::OrderStatus::Ready: return 2;
                    case ecs::OrderStatus::Held: return 3;
                    case ecs::OrderStatus::Pending: return 4;
                    case ecs::OrderStatus::Served: return 5;
                    case ecs::OrderStatus::Cancelled: return 6;
                }
                return 7;
            };
            return priority(statusA) < priority(statusB);
        }
        return a.second->timeReceived < b.second->timeReceived;
    });

    if (orders.empty()) {
        ImGui::TextDisabled("No orders to display");
        return;
    }

    // Render each order
    for (const auto& [entity, orderPtr] : orders) {
        const auto& order = *orderPtr;
        auto status = order.overallStatus();
        ImVec4 statusColor = getStatusColor(status);

        ImGui::PushID(order.orderId);

        // Order header with status color
        char header[64];
        snprintf(header, sizeof(header), "Order #%u - Table %u [%s]",
                order.orderId, order.tableNumber,
                kitchen::getOrderStatusName(status));

        ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(statusColor.x * 0.3f, statusColor.y * 0.3f, statusColor.z * 0.3f, 0.5f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(statusColor.x * 0.4f, statusColor.y * 0.4f, statusColor.z * 0.4f, 0.7f));

        bool expanded = ImGui::CollapsingHeader(header);

        ImGui::PopStyleColor(2);

        // Action buttons on same line
        if (status == ecs::OrderStatus::Pending || status == ecs::OrderStatus::Held) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 150);
            if (status == ecs::OrderStatus::Pending) {
                if (ImGui::SmallButton("Hold")) {
                    kitchen.holdOrder(world, entity);
                }
                ImGui::SameLine();
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.1f, 1.0f));
            if (ImGui::SmallButton("FIRE!")) {
                kitchen.fireOrder(world, entity);
            }
            ImGui::PopStyleColor();
        } else if (status == ecs::OrderStatus::Ready) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - 60);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.7f, 0.3f, 1.0f));
            if (ImGui::SmallButton("Serve")) {
                kitchen.serveOrder(world, entity);
            }
            ImGui::PopStyleColor();
        }

        if (expanded) {
            ImGui::Indent();

            // Order timing info
            float waitTime = kitchenState->gameTime - order.timeReceived;
            ImGui::Text("Wait Time: %.1f seconds", waitTime);

            if (order.urgent) {
                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::Text("[URGENT]");
                ImGui::PopStyleColor();
            }

            ImGui::Spacing();

            // Item list
            for (size_t i = 0; i < order.items.size(); ++i) {
                const auto& item = order.items[i];
                const auto& menuItem = kitchenState->menu[item.menuItemId];

                ImGui::PushID(static_cast<int>(i));

                // Item status indicator
                ImGui::PushStyleColor(ImGuiCol_Text, getStatusColor(item.status));
                ImGui::Bullet();
                ImGui::PopStyleColor();

                ImGui::SameLine();
                ImGui::Text("%s", menuItem.name.c_str());

                // Show cooking progress if cooking
                if (item.status == ecs::OrderStatus::Cooking) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(100);
                    float cookPct = item.cookProgress * 100.0f;
                    char overlayText[32];
                    snprintf(overlayText, sizeof(overlayText), "%.0f%%", cookPct);

                    ImVec4 progColor = item.cookProgress < 0.9f
                        ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
                        : ImVec4(0.2f, 1.0f, 0.3f, 1.0f);
                    drawProgressBar(item.cookProgress, progColor, overlayText);
                } else if (item.status == ecs::OrderStatus::Ready) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
                    ImGui::Text("[READY]");
                    ImGui::PopStyleColor();
                }

                // Quality indicator
                if (item.qualityModifier < 1.0f) {
                    ImGui::SameLine();
                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
                    ImGui::Text("(Quality: %.0f%%)", item.qualityModifier * 100.0f);
                    ImGui::PopStyleColor();
                }

                // Fire individual item button
                if (item.status == ecs::OrderStatus::Pending || item.status == ecs::OrderStatus::Held) {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Fire Item")) {
                        kitchen.fireItem(world, entity, static_cast<uint32_t>(i));
                    }
                }

                ImGui::PopID();
            }

            // Cancel button
            if (status != ecs::OrderStatus::Served && status != ecs::OrderStatus::Cancelled) {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.2f, 0.2f, 1.0f));
                if (ImGui::SmallButton("Cancel Order")) {
                    kitchen.cancelOrder(world, entity);
                }
                ImGui::PopStyleColor();
            }

            ImGui::Unindent();
        }

        ImGui::PopID();
    }
}

void GuiKitchenTab::renderStations(IKitchenControl& control) {
    auto& kitchen = control.getKitchenSystem();
    auto& world = control.getECSWorld();
    auto* kitchenState = kitchen.getKitchenState(world);

    if (!kitchenState) {
        ImGui::TextDisabled("Kitchen not initialized");
        return;
    }

    auto stationView = world.view<ecs::KitchenStation>();

    for (auto [entity, station] : stationView.each()) {
        ImGui::PushID(station.stationIndex);

        ImVec4 stateColor = getStationStateColor(station.state);

        // Station header
        char header[64];
        snprintf(header, sizeof(header), "%s #%u",
                kitchen::getStationTypeName(station.type),
                station.stationIndex);

        ImGui::PushStyleColor(ImGuiCol_Text, stateColor);
        ImGui::Text("%s", header);
        ImGui::PopStyleColor();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
        ImGui::Text("[%s]", kitchen::getStationStateName(station.state));

        if (station.state == ecs::StationState::Cooking) {
            // Show what's cooking
            ecs::Entity orderEntity = kitchen.findOrderById(world, station.currentOrderId);
            if (orderEntity != ecs::NullEntity && world.has<ecs::Order>(orderEntity)) {
                const auto& order = world.get<ecs::Order>(orderEntity);
                if (station.currentItemIndex < order.items.size()) {
                    const auto& item = order.items[station.currentItemIndex];
                    const auto& menuItem = kitchenState->menu[item.menuItemId];

                    ImGui::Indent();
                    ImGui::Text("Cooking: %s (Order #%u)",
                               menuItem.name.c_str(), station.currentOrderId);

                    // Progress bar
                    ImVec4 progColor = station.cookProgress < 0.9f
                        ? ImVec4(1.0f, 0.6f, 0.2f, 1.0f)
                        : ImVec4(0.2f, 1.0f, 0.3f, 1.0f);

                    if (station.cookProgress >= 1.0f) {
                        // Done - waiting for pickup
                        progColor = ImVec4(0.3f, 1.0f, 0.3f, 1.0f);
                        drawProgressBar(1.0f, progColor, "DONE - Collect!");

                        if (ImGui::SmallButton("Collect")) {
                            kitchen.collectFromStation(world, entity);
                        }
                    } else {
                        char overlay[32];
                        snprintf(overlay, sizeof(overlay), "%.0f%%", station.cookProgress * 100.0f);
                        drawProgressBar(station.cookProgress, progColor, overlay);
                    }

                    ImGui::Unindent();
                }
            }
        } else if (station.state == ecs::StationState::Overcooked) {
            ImGui::Indent();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
            ImGui::Text("OVERCOOKED! Needs cleaning.");
            ImGui::PopStyleColor();

            if (ImGui::SmallButton("Clean Station")) {
                kitchen.cleanStation(world, entity);
            }
            ImGui::Unindent();
        } else if (station.state == ecs::StationState::Idle) {
            ImGui::Indent();
            ImGui::TextDisabled("Ready for orders");
            ImGui::Unindent();
        }

        ImGui::Separator();
        ImGui::PopID();
    }
}

void GuiKitchenTab::renderStats(IKitchenControl& control) {
    auto& kitchen = control.getKitchenSystem();
    auto& world = control.getECSWorld();

    auto stats = kitchen.getStats(world);

    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.9f, 1.0f, 1.0f));
    ImGui::Text("STATISTICS");
    ImGui::PopStyleColor();

    ImGui::Columns(4, "StatsColumns", false);

    ImGui::Text("Orders");
    ImGui::Text("Pending: %u", stats.pendingOrders);
    ImGui::Text("Held: %u", stats.heldOrders);
    ImGui::Text("Active: %u", stats.activeOrders);

    ImGui::NextColumn();
    ImGui::Text(" ");
    ImGui::Text("Ready: %u", stats.readyOrders);
    ImGui::Text("Completed: %u", stats.completedOrders);
    ImGui::Text("Cancelled: %u", stats.cancelledOrders);

    ImGui::NextColumn();
    ImGui::Text("Stations");
    ImGui::Text("Busy: %u", stats.busyStations);
    ImGui::Text("Available: %u", stats.availableStations);

    ImGui::NextColumn();
    ImGui::Text("Quality");
    ImGui::Text("Avg Quality: %.0f%%", stats.averageQuality * 100.0f);
    ImGui::Text("Avg Wait: %.1fs", stats.averageWaitTime);

    ImGui::Columns(1);
}
