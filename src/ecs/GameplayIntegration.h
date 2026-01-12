#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <string>
#include <vector>
#include <functional>
#include "Components.h"

// Gameplay Systems ECS Integration
// Bridges ECS entities with gameplay mechanics (triggers, navigation, interaction)
namespace GameplayECS {

// ============================================================================
// Event callback types
// ============================================================================

using TriggerCallback = std::function<void(entt::entity trigger, entt::entity other)>;
using InteractionCallback = std::function<void(entt::entity interactor, entt::entity target)>;

// ============================================================================
// Trigger Volume factory functions
// ============================================================================

// Create a box trigger volume
inline entt::entity createBoxTrigger(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& extents,
    const std::string& name = "BoxTrigger")
{
    auto entity = registry.create();

    TriggerVolume trigger;
    trigger.extents = extents;
    trigger.shape = TriggerVolume::Shape::Box;
    registry.emplace<TriggerVolume>(entity, trigger);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<IsTrigger>(entity);

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "T";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a sphere trigger volume
inline entt::entity createSphereTrigger(
    entt::registry& registry,
    const glm::vec3& position,
    float radius,
    const std::string& name = "SphereTrigger")
{
    auto entity = registry.create();

    TriggerVolume trigger;
    trigger.shape = TriggerVolume::Shape::Sphere;
    trigger.radius = radius;
    registry.emplace<TriggerVolume>(entity, trigger);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<IsTrigger>(entity);

    BoundingSphere bounds;
    bounds.radius = radius;
    registry.emplace<BoundingSphere>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "T";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a one-shot trigger (fires once then disables)
inline entt::entity createOneShotTrigger(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& extents,
    const std::string& name = "OneShotTrigger")
{
    auto entity = createBoxTrigger(registry, position, extents, name);
    auto& trigger = registry.get<TriggerVolume>(entity);
    trigger.triggerOnce = true;
    return entity;
}

// ============================================================================
// NavMesh Agent factory functions
// ============================================================================

// Create a NavMesh agent entity
inline entt::entity createNavMeshAgent(
    entt::registry& registry,
    const glm::vec3& position,
    NavMeshHandle navMesh = InvalidNavMesh,
    const std::string& name = "NavMeshAgent")
{
    auto entity = registry.create();

    NavMeshAgent agent;
    agent.navMesh = navMesh;
    registry.emplace<NavMeshAgent>(entity, agent);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    EntityInfo info;
    info.name = name;
    info.icon = "A";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Set agent destination
inline void setAgentDestination(
    entt::registry& registry,
    entt::entity entity,
    const glm::vec3& destination)
{
    if (!registry.all_of<NavMeshAgent>(entity)) return;

    auto& agent = registry.get<NavMeshAgent>(entity);
    agent.destination = destination;
    agent.pathPending = true;
    agent.status = NavMeshAgent::Status::Moving;
}

// Stop agent movement
inline void stopAgent(entt::registry& registry, entt::entity entity) {
    if (!registry.all_of<NavMeshAgent>(entity)) return;

    auto& agent = registry.get<NavMeshAgent>(entity);
    agent.hasPath = false;
    agent.pathPending = false;
    agent.currentPath.clear();
    agent.status = NavMeshAgent::Status::Idle;
    agent.velocity = glm::vec3(0.0f);
}

// ============================================================================
// Interactable factory functions
// ============================================================================

// Create a generic interactable entity
inline entt::entity createInteractable(
    entt::registry& registry,
    const glm::vec3& position,
    Interactable::Type type,
    const std::string& promptText,
    const std::string& name = "Interactable")
{
    auto entity = registry.create();

    Interactable interact;
    interact.type = type;
    interact.promptText = promptText;
    registry.emplace<Interactable>(entity, interact);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<IsInteractable>(entity);

    EntityInfo info;
    info.name = name;
    info.icon = "I";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a pickup item
inline entt::entity createPickup(
    entt::registry& registry,
    const glm::vec3& position,
    const std::string& itemId,
    uint32_t quantity = 1,
    const std::string& name = "Pickup")
{
    auto entity = createInteractable(registry, position, Interactable::Type::Pickup, "Pick up", name);

    Pickup pickup;
    pickup.itemId = itemId;
    pickup.quantity = quantity;
    registry.emplace<Pickup>(entity, pickup);

    return entity;
}

// Create a door
inline entt::entity createDoor(
    entt::registry& registry,
    const glm::vec3& position,
    float openAngle = 90.0f,
    bool locked = false,
    const std::string& name = "Door")
{
    auto entity = createInteractable(registry, position, Interactable::Type::Door, locked ? "Locked" : "Open", name);

    Door door;
    door.openAngle = openAngle;
    door.locked = locked;
    registry.emplace<Door>(entity, door);

    return entity;
}

// Create a switch/button
inline entt::entity createSwitch(
    entt::registry& registry,
    const glm::vec3& position,
    Switch::Type type = Switch::Type::Toggle,
    entt::entity target = entt::null,
    const std::string& name = "Switch")
{
    auto entity = createInteractable(registry, position, Interactable::Type::Switch, "Activate", name);

    Switch sw;
    sw.type = type;
    sw.targetEntity = target;
    registry.emplace<Switch>(entity, sw);

    return entity;
}

// Create a dialogue NPC
inline entt::entity createDialogueNPC(
    entt::registry& registry,
    const glm::vec3& position,
    DialogueHandle dialogue,
    const std::string& name = "NPC")
{
    auto entity = createInteractable(registry, position, Interactable::Type::NPC, "Talk", name);

    DialogueTrigger dialogueTrig;
    dialogueTrig.dialogue = dialogue;
    registry.emplace<DialogueTrigger>(entity, dialogueTrig);
    registry.emplace<IsDialogueNPC>(entity);

    return entity;
}

// ============================================================================
// Spawn point factory functions
// ============================================================================

// Create a spawn point
inline entt::entity createSpawnPoint(
    entt::registry& registry,
    const glm::vec3& position,
    const std::string& entityType,
    float respawnDelay = 10.0f,
    const std::string& name = "SpawnPoint")
{
    auto entity = registry.create();

    SpawnPoint spawn;
    spawn.entityType = entityType;
    spawn.respawnDelay = respawnDelay;
    registry.emplace<SpawnPoint>(entity, spawn);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<IsSpawnPoint>(entity);

    EntityInfo info;
    info.name = name;
    info.icon = "S";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a checkpoint
inline entt::entity createCheckpoint(
    entt::registry& registry,
    const glm::vec3& position,
    uint32_t checkpointId,
    const std::string& name = "Checkpoint")
{
    auto entity = registry.create();

    Checkpoint checkpoint;
    checkpoint.checkpointId = checkpointId;
    registry.emplace<Checkpoint>(entity, checkpoint);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});

    EntityInfo info;
    info.name = name + "_" + std::to_string(checkpointId);
    info.icon = "C";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// Create a damage zone
inline entt::entity createDamageZone(
    entt::registry& registry,
    const glm::vec3& position,
    const glm::vec3& extents,
    float damagePerSecond,
    DamageZone::DamageType damageType = DamageZone::DamageType::Generic,
    const std::string& name = "DamageZone")
{
    auto entity = registry.create();

    DamageZone zone;
    zone.extents = extents;
    zone.damagePerSecond = damagePerSecond;
    zone.damageType = damageType;
    registry.emplace<DamageZone>(entity, zone);

    registry.emplace<Transform>(entity, Transform{position, 0.0f});
    registry.emplace<IsTrigger>(entity);

    AABBBounds bounds;
    bounds.min = -extents;
    bounds.max = extents;
    registry.emplace<AABBBounds>(entity, bounds);

    EntityInfo info;
    info.name = name;
    info.icon = "!";
    registry.emplace<EntityInfo>(entity, info);

    return entity;
}

// ============================================================================
// Trigger volume system helpers
// ============================================================================

// Check if point is inside trigger volume
inline bool isInsideTrigger(
    const TriggerVolume& trigger,
    const glm::vec3& triggerPos,
    const glm::vec3& point)
{
    switch (trigger.shape) {
        case TriggerVolume::Shape::Box: {
            glm::vec3 localPoint = point - triggerPos;
            return glm::abs(localPoint.x) <= trigger.extents.x &&
                   glm::abs(localPoint.y) <= trigger.extents.y &&
                   glm::abs(localPoint.z) <= trigger.extents.z;
        }
        case TriggerVolume::Shape::Sphere: {
            float dist = glm::distance(triggerPos, point);
            return dist <= trigger.radius;
        }
        case TriggerVolume::Shape::Capsule: {
            // Simplified capsule test (vertical axis)
            float dy = point.y - triggerPos.y;
            float halfHeight = trigger.height * 0.5f;
            if (dy < -halfHeight - trigger.radius || dy > halfHeight + trigger.radius) {
                return false;
            }
            glm::vec2 horizontal(point.x - triggerPos.x, point.z - triggerPos.z);
            return glm::length(horizontal) <= trigger.radius;
        }
    }
    return false;
}

// Update trigger volumes (call each frame)
inline void updateTriggerVolumes(
    entt::registry& registry,
    TriggerCallback onEnter = nullptr,
    TriggerCallback onExit = nullptr,
    TriggerCallback onStay = nullptr,
    float deltaTime = 0.0f)
{
    auto triggerView = registry.view<TriggerVolume, Transform>();
    auto triggerableView = registry.view<Triggerable, Transform>();

    for (auto triggerEntity : triggerView) {
        auto& trigger = triggerView.get<TriggerVolume>(triggerEntity);
        auto& triggerTransform = triggerView.get<Transform>(triggerEntity);

        if (trigger.triggerOnce && trigger.triggered) continue;

        std::vector<entt::entity> nowInside;

        // Check all triggerable entities
        for (auto otherEntity : triggerableView) {
            auto& triggerable = triggerableView.get<Triggerable>(otherEntity);
            auto& otherTransform = triggerableView.get<Transform>(otherEntity);

            // Check mask
            if ((trigger.triggerMask & triggerable.triggerLayer) == 0) continue;

            // Check if inside
            if (isInsideTrigger(trigger, triggerTransform.position, otherTransform.position)) {
                nowInside.push_back(otherEntity);
            }
        }

        // Find entities that entered
        for (auto entity : nowInside) {
            bool wasInside = std::find(trigger.entitiesInside.begin(),
                                       trigger.entitiesInside.end(),
                                       entity) != trigger.entitiesInside.end();
            if (!wasInside) {
                // Entity entered
                if (onEnter) onEnter(triggerEntity, entity);
                if (trigger.triggerOnce) {
                    trigger.triggered = true;
                }
            }
        }

        // Find entities that exited
        for (auto entity : trigger.entitiesInside) {
            bool stillInside = std::find(nowInside.begin(),
                                         nowInside.end(),
                                         entity) != nowInside.end();
            if (!stillInside) {
                // Entity exited
                if (onExit) onExit(triggerEntity, entity);
            }
        }

        // Handle stay events
        if (onStay && !nowInside.empty()) {
            trigger.timeSinceStayEvent += deltaTime;
            if (trigger.stayEventInterval <= 0.0f ||
                trigger.timeSinceStayEvent >= trigger.stayEventInterval) {
                for (auto entity : nowInside) {
                    onStay(triggerEntity, entity);
                }
                trigger.timeSinceStayEvent = 0.0f;
            }
        }

        // Update entity list
        trigger.entitiesInside = std::move(nowInside);
    }
}

// ============================================================================
// Interaction system helpers
// ============================================================================

// Find the best interactable for an entity
inline entt::entity findBestInteractable(
    entt::registry& registry,
    entt::entity interactor,
    const glm::vec3& lookDirection)
{
    if (!registry.all_of<CanInteract, Transform>(interactor)) {
        return entt::null;
    }

    auto& canInteract = registry.get<CanInteract>(interactor);
    auto& interactorTransform = registry.get<Transform>(interactor);
    glm::vec3 interactorPos = interactorTransform.position;

    entt::entity best = entt::null;
    float bestScore = -1.0f;

    auto view = registry.view<Interactable, Transform>();
    for (auto entity : view) {
        auto& interact = view.get<Interactable>(entity);
        auto& transform = view.get<Transform>(entity);

        if (!interact.canInteract) continue;

        glm::vec3 targetPos = transform.position + interact.interactionPoint;
        float distance = glm::distance(interactorPos, targetPos);

        // Check range
        if (distance > canInteract.interactionRange) continue;
        if (distance > interact.interactionRadius) continue;

        // Check angle
        glm::vec3 toTarget = glm::normalize(targetPos - interactorPos);
        float dot = glm::dot(lookDirection, toTarget);
        float angle = glm::degrees(glm::acos(glm::clamp(dot, -1.0f, 1.0f)));
        if (interact.interactionAngle < 360.0f && angle > interact.interactionAngle * 0.5f) {
            continue;
        }

        // Score based on distance and angle (closer and more centered = better)
        float score = (1.0f - distance / canInteract.interactionRange) * 0.5f +
                      (dot + 1.0f) * 0.25f +
                      interact.priority * 0.01f;

        if (score > bestScore) {
            bestScore = score;
            best = entity;
        }
    }

    return best;
}

// Update interaction highlighting
inline void updateInteractionHighlighting(
    entt::registry& registry,
    entt::entity interactor,
    const glm::vec3& lookDirection,
    InteractionCallback onHighlight = nullptr,
    InteractionCallback onUnhighlight = nullptr)
{
    if (!registry.all_of<CanInteract>(interactor)) return;

    auto& canInteract = registry.get<CanInteract>(interactor);
    entt::entity newTarget = findBestInteractable(registry, interactor, lookDirection);

    // Unhighlight previous
    if (canInteract.currentTarget != entt::null &&
        canInteract.currentTarget != newTarget &&
        registry.valid(canInteract.currentTarget)) {
        if (registry.all_of<Interactable>(canInteract.currentTarget)) {
            auto& interact = registry.get<Interactable>(canInteract.currentTarget);
            interact.highlighted = false;
            if (onUnhighlight) onUnhighlight(interactor, canInteract.currentTarget);
        }
    }

    // Highlight new
    if (newTarget != entt::null && newTarget != canInteract.currentTarget) {
        if (registry.all_of<Interactable>(newTarget)) {
            auto& interact = registry.get<Interactable>(newTarget);
            interact.highlighted = true;
            if (onHighlight) onHighlight(interactor, newTarget);
        }
    }

    canInteract.currentTarget = newTarget;
}

// Perform interaction with current target
inline bool performInteraction(
    entt::registry& registry,
    entt::entity interactor,
    InteractionCallback onInteract = nullptr)
{
    if (!registry.all_of<CanInteract>(interactor)) return false;

    auto& canInteract = registry.get<CanInteract>(interactor);
    if (canInteract.currentTarget == entt::null) return false;
    if (!registry.valid(canInteract.currentTarget)) return false;

    auto& interact = registry.get<Interactable>(canInteract.currentTarget);
    if (!interact.canInteract) return false;

    // Perform interaction
    interact.interacting = true;
    canInteract.interactingWith = canInteract.currentTarget;

    if (onInteract) onInteract(interactor, canInteract.currentTarget);

    return true;
}

// ============================================================================
// Door system helpers
// ============================================================================

// Update door states
inline void updateDoors(entt::registry& registry, float deltaTime) {
    auto view = registry.view<Door>();

    for (auto entity : view) {
        auto& door = view.get<Door>(entity);

        switch (door.state) {
            case Door::State::Opening:
                if (door.sliding) {
                    door.currentSlide += door.openSpeed * deltaTime;
                    if (door.currentSlide >= door.slideDistance) {
                        door.currentSlide = door.slideDistance;
                        door.state = Door::State::Open;
                        door.timeSinceOpened = 0.0f;
                    }
                } else {
                    door.currentAngle += door.openSpeed * deltaTime;
                    if (door.currentAngle >= door.openAngle) {
                        door.currentAngle = door.openAngle;
                        door.state = Door::State::Open;
                        door.timeSinceOpened = 0.0f;
                    }
                }
                break;

            case Door::State::Open:
                if (door.autoClose) {
                    door.timeSinceOpened += deltaTime;
                    if (door.timeSinceOpened >= door.autoCloseDelay) {
                        door.state = Door::State::Closing;
                    }
                }
                break;

            case Door::State::Closing:
                if (door.sliding) {
                    door.currentSlide -= door.openSpeed * deltaTime;
                    if (door.currentSlide <= 0.0f) {
                        door.currentSlide = 0.0f;
                        door.state = Door::State::Closed;
                    }
                } else {
                    door.currentAngle -= door.openSpeed * deltaTime;
                    if (door.currentAngle <= 0.0f) {
                        door.currentAngle = 0.0f;
                        door.state = Door::State::Closed;
                    }
                }
                break;

            case Door::State::Closed:
                // Nothing to do
                break;
        }
    }
}

// Toggle door state
inline void toggleDoor(entt::registry& registry, entt::entity entity) {
    if (!registry.all_of<Door>(entity)) return;

    auto& door = registry.get<Door>(entity);
    if (door.locked) return;

    switch (door.state) {
        case Door::State::Closed:
        case Door::State::Closing:
            door.state = Door::State::Opening;
            break;
        case Door::State::Open:
        case Door::State::Opening:
            door.state = Door::State::Closing;
            break;
    }
}

// ============================================================================
// Pickup system helpers
// ============================================================================

// Update pickup visual effects
inline void updatePickups(entt::registry& registry, float deltaTime, float time) {
    auto view = registry.view<Pickup, Transform>();

    for (auto entity : view) {
        auto& pickup = view.get<Pickup>(entity);
        auto& transform = view.get<Transform>(entity);

        if (pickup.pickedUp) {
            // Handle respawn
            if (pickup.respawns) {
                pickup.timeSincePickup += deltaTime;
                if (pickup.timeSincePickup >= pickup.respawnTime) {
                    pickup.pickedUp = false;
                    pickup.timeSincePickup = 0.0f;
                }
            }
            continue;
        }

        // Bobbing effect
        if (pickup.bobbing) {
            float bob = glm::sin(time * pickup.bobSpeed) * pickup.bobHeight;
            // Note: This modifies the transform - in a real system you'd
            // have a separate visual offset
            (void)bob;  // Placeholder
        }

        // Rotation effect
        if (pickup.rotating) {
            transform.yaw += pickup.rotateSpeed * deltaTime;
            if (transform.yaw >= 360.0f) transform.yaw -= 360.0f;
        }
    }
}

// Collect a pickup
inline bool collectPickup(
    entt::registry& registry,
    entt::entity pickupEntity,
    std::string& outItemId,
    uint32_t& outQuantity)
{
    if (!registry.all_of<Pickup>(pickupEntity)) return false;

    auto& pickup = registry.get<Pickup>(pickupEntity);
    if (pickup.pickedUp) return false;

    outItemId = pickup.itemId;
    outQuantity = pickup.quantity;
    pickup.pickedUp = true;
    pickup.timeSincePickup = 0.0f;

    return true;
}

// ============================================================================
// Query functions
// ============================================================================

// Get all triggers
inline std::vector<entt::entity> getTriggers(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsTrigger>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all interactables
inline std::vector<entt::entity> getInteractables(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsInteractable>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all spawn points
inline std::vector<entt::entity> getSpawnPoints(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsSpawnPoint>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all NavMesh agents
inline std::vector<entt::entity> getNavMeshAgents(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<NavMeshAgent>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Get all dialogue NPCs
inline std::vector<entt::entity> getDialogueNPCs(entt::registry& registry) {
    std::vector<entt::entity> result;
    auto view = registry.view<IsDialogueNPC>();
    for (auto entity : view) {
        result.push_back(entity);
    }
    return result;
}

// Find nearest interactable of type
inline entt::entity findNearestInteractable(
    entt::registry& registry,
    const glm::vec3& position,
    Interactable::Type type,
    float maxDistance = 100.0f)
{
    entt::entity nearest = entt::null;
    float nearestDist = maxDistance;

    auto view = registry.view<Interactable, Transform>();
    for (auto entity : view) {
        auto& interact = view.get<Interactable>(entity);
        auto& transform = view.get<Transform>(entity);

        if (interact.type != type) continue;

        float dist = glm::distance(position, transform.position);
        if (dist < nearestDist) {
            nearestDist = dist;
            nearest = entity;
        }
    }

    return nearest;
}

// Get active checkpoint
inline entt::entity getActiveCheckpoint(entt::registry& registry) {
    auto view = registry.view<Checkpoint>();
    for (auto entity : view) {
        auto& checkpoint = view.get<Checkpoint>(entity);
        if (checkpoint.activated && checkpoint.isRespawnPoint) {
            return entity;
        }
    }
    return entt::null;
}

}  // namespace GameplayECS
