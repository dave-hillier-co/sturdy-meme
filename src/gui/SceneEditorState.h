#pragma once

#include "ecs/World.h"
#include <string>
#include <vector>

/**
 * State for the scene editor - tracks selection, editor mode, and panel states.
 * Used by GuiHierarchyPanel and GuiInspectorPanel.
 */
struct SceneEditorState {
    // Selection state
    ecs::Entity selectedEntity = ecs::NullEntity;
    std::vector<ecs::Entity> multiSelection;  // For multi-select (shift+click)

    // Hierarchy panel state
    char hierarchyFilterText[256] = "";
    bool showHierarchyFilter = true;

    // Inspector panel state
    bool showTransformSection = true;
    bool showMaterialSection = true;
    bool showComponentsSection = true;
    bool showTagsSection = true;

    // Editor mode
    enum class TransformMode {
        Translate,
        Rotate,
        Scale
    };
    TransformMode transformMode = TransformMode::Translate;

    // Transform space
    enum class TransformSpace {
        Local,
        World
    };
    TransformSpace transformSpace = TransformSpace::Local;

    // Drag state for hierarchy reparenting
    ecs::Entity draggedEntity = ecs::NullEntity;
    ecs::Entity dropTargetEntity = ecs::NullEntity;

    // Entity creation state
    bool showCreateEntityPopup = false;

    // Expand/collapse state for hierarchy nodes (entity ID -> expanded)
    std::vector<ecs::Entity> expandedNodes;

    // Helper methods
    bool isSelected(ecs::Entity entity) const {
        if (entity == selectedEntity) return true;
        for (auto e : multiSelection) {
            if (e == entity) return true;
        }
        return false;
    }

    void select(ecs::Entity entity) {
        selectedEntity = entity;
        multiSelection.clear();
    }

    void addToSelection(ecs::Entity entity) {
        if (!isSelected(entity)) {
            multiSelection.push_back(entity);
        }
    }

    void clearSelection() {
        selectedEntity = ecs::NullEntity;
        multiSelection.clear();
    }

    bool isExpanded(ecs::Entity entity) const {
        for (auto e : expandedNodes) {
            if (e == entity) return true;
        }
        return false;
    }

    void setExpanded(ecs::Entity entity, bool expanded) {
        auto it = std::find(expandedNodes.begin(), expandedNodes.end(), entity);
        if (expanded && it == expandedNodes.end()) {
            expandedNodes.push_back(entity);
        } else if (!expanded && it != expandedNodes.end()) {
            expandedNodes.erase(it);
        }
    }

    void toggleExpanded(ecs::Entity entity) {
        setExpanded(entity, !isExpanded(entity));
    }
};
