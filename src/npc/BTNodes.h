#pragma once

#include "BehaviorTree.h"
#include "NPC.h"
#include "physics/PhysicsSystem.h"
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <SDL3/SDL_log.h>
#include <cmath>
#include <algorithm>

// ============================================================================
// Condition Nodes - Check NPC/world state
// ============================================================================

namespace BTConditions {

// Check if NPC can see the player (within FOV and line of sight)
inline BTNodePtr CanSeePlayer() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.canSeePlayer;
    });
}

// Check if NPC is aware of player (above detection threshold)
inline BTNodePtr IsAwareOfPlayer() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.awareness >= ctx.npc->config.detectionThreshold;
    });
}

// Check if NPC has high awareness (chase threshold)
inline BTNodePtr HasHighAwareness() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.awareness >= ctx.npc->config.chaseThreshold;
    });
}

// Check if NPC has attack-level awareness
inline BTNodePtr HasAttackAwareness() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.awareness >= ctx.npc->config.attackThreshold;
    });
}

// Check if player is within attack range
inline BTNodePtr IsInAttackRange() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.distanceToPlayer <= ctx.npc->config.attackRange;
    });
}

// Check if player is within a specific range
inline BTNodePtr IsPlayerInRange(float range) {
    return std::make_unique<BTCondition>([range](BTContext& ctx) {
        return ctx.npc->perception.distanceToPlayer <= range;
    });
}

// Check if player is within personal space (for neutral NPCs)
inline BTNodePtr IsPlayerInPersonalSpace() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.distanceToPlayer <= ctx.npc->config.personalSpace;
    });
}

// Check if player is beyond chase range
inline BTNodePtr IsPlayerOutOfChaseRange() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.distanceToPlayer > ctx.npc->config.chaseRange;
    });
}

// Check if NPC has fled far enough
inline BTNodePtr HasFledFarEnough() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.distanceToPlayer >= ctx.npc->config.fleeDistance;
    });
}

// Check if NPC has a last known player position
inline BTNodePtr HasLastKnownPosition() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->perception.hasLastKnownPosition;
    });
}

// Check if NPC is at spawn position
inline BTNodePtr IsAtSpawnPosition(float tolerance = 1.0f) {
    return std::make_unique<BTCondition>([tolerance](BTContext& ctx) {
        glm::vec3 toSpawn = ctx.npc->spawnPosition - ctx.npc->transform.position;
        toSpawn.y = 0.0f;
        return glm::length(toSpawn) <= tolerance;
    });
}

// Check if NPC can attack (cooldown elapsed)
inline BTNodePtr CanAttack() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->canAttack();
    });
}

// Check if NPC has low health
inline BTNodePtr HasLowHealth(float threshold = 0.3f) {
    return std::make_unique<BTCondition>([threshold](BTContext& ctx) {
        return (ctx.npc->health / ctx.npc->maxHealth) <= threshold;
    });
}

// Check if NPC is alive
inline BTNodePtr IsAlive() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->isAlive();
    });
}

// Check hostility level
inline BTNodePtr IsHostile() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->hostility == HostilityLevel::Hostile;
    });
}

inline BTNodePtr IsNeutral() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->hostility == HostilityLevel::Neutral;
    });
}

inline BTNodePtr IsFriendly() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->hostility == HostilityLevel::Friendly;
    });
}

inline BTNodePtr IsAfraid() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return ctx.npc->hostility == HostilityLevel::Afraid;
    });
}

// Check if NPC has a patrol path
inline BTNodePtr HasPatrolPath() {
    return std::make_unique<BTCondition>([](BTContext& ctx) {
        return !ctx.npc->patrolPath.empty();
    });
}

// Random chance condition
inline BTNodePtr RandomChance(float probability) {
    return std::make_unique<BTCondition>([probability](BTContext& ctx) {
        return (static_cast<float>(rand()) / RAND_MAX) < probability;
    });
}

// Check blackboard value
template<typename T>
inline BTNodePtr BlackboardEquals(const std::string& key, T value) {
    return std::make_unique<BTCondition>([key, value](BTContext& ctx) {
        return ctx.blackboard->get<T>(key) == value;
    });
}

inline BTNodePtr BlackboardHas(const std::string& key) {
    return std::make_unique<BTCondition>([key](BTContext& ctx) {
        return ctx.blackboard->has(key);
    });
}

} // namespace BTConditions

// ============================================================================
// Action Nodes - Perform actions
// ============================================================================

namespace BTActions {

// Move towards a target position
inline BTNodePtr MoveTowards(const glm::vec3& target, float arrivalDistance = 0.5f) {
    return std::make_unique<BTAction>([target, arrivalDistance](BTContext& ctx) {
        glm::vec3 direction = target - ctx.npc->transform.position;
        direction.y = 0.0f;
        float distance = glm::length(direction);

        if (distance <= arrivalDistance) {
            ctx.npc->velocity = glm::vec3(0.0f);
            return BTStatus::Success;
        }

        direction = glm::normalize(direction);
        ctx.npc->transform.smoothLookAt(target, ctx.deltaTime);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed * ctx.npc->config.patrolSpeedMultiplier;

        return BTStatus::Running;
    });
}

// Move towards the player
inline BTNodePtr MoveToPlayer() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        glm::vec3 target = *ctx.playerPosition;
        glm::vec3 direction = target - ctx.npc->transform.position;
        direction.y = 0.0f;
        float distance = glm::length(direction);

        if (distance <= ctx.npc->config.attackRange * 0.8f) {
            ctx.npc->velocity = glm::vec3(0.0f);
            return BTStatus::Success;
        }

        direction = glm::normalize(direction);
        ctx.npc->transform.smoothLookAt(target, ctx.deltaTime, 8.0f);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed * ctx.npc->config.chaseSpeedMultiplier;

        return BTStatus::Running;
    });
}

// Move towards last known player position
inline BTNodePtr MoveToLastKnownPosition() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        if (!ctx.npc->perception.hasLastKnownPosition) {
            return BTStatus::Failure;
        }

        glm::vec3 target = ctx.npc->perception.lastKnownPosition;
        glm::vec3 direction = target - ctx.npc->transform.position;
        direction.y = 0.0f;
        float distance = glm::length(direction);

        if (distance <= 1.0f) {
            ctx.npc->velocity = glm::vec3(0.0f);
            ctx.npc->perception.hasLastKnownPosition = false;  // Clear after arrival
            return BTStatus::Success;
        }

        direction = glm::normalize(direction);
        ctx.npc->transform.smoothLookAt(target, ctx.deltaTime, 5.0f);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed;

        return BTStatus::Running;
    });
}

// Move away from the player (flee)
inline BTNodePtr FleeFromPlayer() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        glm::vec3 direction = ctx.npc->transform.position - *ctx.playerPosition;
        direction.y = 0.0f;

        if (glm::length(direction) < 0.001f) {
            direction = glm::vec3(1.0f, 0.0f, 0.0f);
        }

        direction = glm::normalize(direction);

        // Look away while fleeing
        glm::vec3 fleeTarget = ctx.npc->transform.position + direction * 10.0f;
        ctx.npc->transform.smoothLookAt(fleeTarget, ctx.deltaTime, 8.0f);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed * ctx.npc->config.fleeSpeedMultiplier;

        return BTStatus::Running;
    });
}

// Return to spawn position
inline BTNodePtr ReturnToSpawn() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        glm::vec3 direction = ctx.npc->spawnPosition - ctx.npc->transform.position;
        direction.y = 0.0f;
        float distance = glm::length(direction);

        if (distance <= 1.0f) {
            ctx.npc->velocity = glm::vec3(0.0f);
            return BTStatus::Success;
        }

        direction = glm::normalize(direction);
        ctx.npc->transform.smoothLookAt(ctx.npc->spawnPosition, ctx.deltaTime, 5.0f);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed;

        return BTStatus::Running;
    });
}

// Patrol along waypoints
inline BTNodePtr Patrol() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        if (ctx.npc->patrolPath.empty()) {
            return BTStatus::Failure;
        }

        const auto& waypoint = ctx.npc->patrolPath[ctx.npc->currentWaypointIndex];
        glm::vec3 direction = waypoint.position - ctx.npc->transform.position;
        direction.y = 0.0f;
        float distance = glm::length(direction);

        // Reached waypoint?
        if (distance < 0.5f) {
            ctx.npc->waypointWaitTimer += ctx.deltaTime;

            if (ctx.npc->waypointWaitTimer >= waypoint.waitTime) {
                ctx.npc->waypointWaitTimer = 0.0f;

                // Move to next waypoint (ping-pong)
                if (ctx.npc->patrolForward) {
                    ctx.npc->currentWaypointIndex++;
                    if (ctx.npc->currentWaypointIndex >= ctx.npc->patrolPath.size()) {
                        ctx.npc->currentWaypointIndex = ctx.npc->patrolPath.size() > 1 ?
                            ctx.npc->patrolPath.size() - 2 : 0;
                        ctx.npc->patrolForward = false;
                    }
                } else {
                    if (ctx.npc->currentWaypointIndex == 0) {
                        ctx.npc->patrolForward = true;
                        if (ctx.npc->patrolPath.size() > 1) {
                            ctx.npc->currentWaypointIndex = 1;
                        }
                    } else {
                        ctx.npc->currentWaypointIndex--;
                    }
                }
            }

            ctx.npc->velocity = glm::vec3(0.0f);
            return BTStatus::Running;
        }

        direction = glm::normalize(direction);
        ctx.npc->transform.smoothLookAt(waypoint.position, ctx.deltaTime, 3.0f);
        ctx.npc->velocity = direction * ctx.npc->baseSpeed * ctx.npc->config.patrolSpeedMultiplier;

        return BTStatus::Running;
    });
}

// Attack the player
inline BTNodePtr Attack() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        // Face the player
        ctx.npc->transform.smoothLookAt(*ctx.playerPosition, ctx.deltaTime, 10.0f);
        ctx.npc->velocity = glm::vec3(0.0f);

        if (ctx.npc->canAttack()) {
            ctx.npc->isAttacking = true;
            ctx.npc->attackCooldownTimer = ctx.npc->config.attackCooldown;
            SDL_Log("NPC %s attacks!", ctx.npc->name.c_str());
            return BTStatus::Success;
        }

        ctx.npc->isAttacking = false;
        return BTStatus::Running;
    });
}

// Idle - stand still and optionally look around
inline BTNodePtr Idle() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        ctx.npc->velocity = glm::vec3(0.0f);
        ctx.npc->idleTimer += ctx.deltaTime;

        // Occasional random look
        if (ctx.npc->idleTimer > 3.0f) {
            ctx.npc->idleTimer = 0.0f;
            // Could trigger look animation here
        }

        return BTStatus::Running;
    });
}

// Look at player
inline BTNodePtr LookAtPlayer() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        ctx.npc->transform.smoothLookAt(*ctx.playerPosition, ctx.deltaTime, 5.0f);
        ctx.npc->velocity = glm::vec3(0.0f);
        return BTStatus::Success;
    });
}

// Set hostility level
inline BTNodePtr SetHostility(HostilityLevel level, HostilityTrigger trigger) {
    return std::make_unique<BTAction>([level, trigger](BTContext& ctx) {
        if (ctx.npc->hostility != level) {
            ctx.npc->hostility = level;
            ctx.npc->lastTrigger = trigger;
            ctx.npc->hostilityTimer = 0.0f;
            SDL_Log("NPC %s hostility -> %d", ctx.npc->name.c_str(), static_cast<int>(level));
        }
        return BTStatus::Success;
    });
}

// Set behavior state (for visual feedback/animation)
inline BTNodePtr SetBehaviorState(BehaviorState state) {
    return std::make_unique<BTAction>([state](BTContext& ctx) {
        if (ctx.npc->behaviorState != state) {
            ctx.npc->previousState = ctx.npc->behaviorState;
            ctx.npc->behaviorState = state;
            ctx.npc->stateTimer = 0.0f;
        }
        return BTStatus::Success;
    });
}

// Update alert level for visual feedback
inline BTNodePtr SetAlertLevel(float level) {
    return std::make_unique<BTAction>([level](BTContext& ctx) {
        ctx.npc->alertLevel = level;
        return BTStatus::Success;
    });
}

// Reset perception (forget player)
inline BTNodePtr ForgetPlayer() {
    return std::make_unique<BTAction>([](BTContext& ctx) {
        ctx.npc->perception.reset();
        return BTStatus::Success;
    });
}

// Log a message (for debugging)
inline BTNodePtr Log(const std::string& message) {
    return std::make_unique<BTAction>([message](BTContext& ctx) {
        SDL_Log("BT [%s]: %s", ctx.npc->name.c_str(), message.c_str());
        return BTStatus::Success;
    });
}

// Set blackboard value
template<typename T>
inline BTNodePtr SetBlackboard(const std::string& key, T value) {
    return std::make_unique<BTAction>([key, value](BTContext& ctx) {
        ctx.blackboard->set(key, value);
        return BTStatus::Success;
    });
}

// Clear blackboard key
inline BTNodePtr ClearBlackboard(const std::string& key) {
    return std::make_unique<BTAction>([key](BTContext& ctx) {
        ctx.blackboard->remove(key);
        return BTStatus::Success;
    });
}

} // namespace BTActions

// ============================================================================
// Builder Helpers - Fluent API for constructing trees
// ============================================================================

namespace BTBuilder {

// Create a selector (OR node)
inline std::unique_ptr<BTSelector> Selector() {
    return std::make_unique<BTSelector>();
}

// Create a sequence (AND node)
inline std::unique_ptr<BTSequence> Sequence() {
    return std::make_unique<BTSequence>();
}

// Create a parallel node
inline std::unique_ptr<BTParallel> Parallel(
    BTParallel::Policy successPolicy = BTParallel::Policy::RequireAll,
    BTParallel::Policy failurePolicy = BTParallel::Policy::RequireOne) {
    return std::make_unique<BTParallel>(successPolicy, failurePolicy);
}

// Create a random selector
inline std::unique_ptr<BTRandomSelector> RandomSelector() {
    return std::make_unique<BTRandomSelector>();
}

// Create decorator nodes
inline std::unique_ptr<BTInverter> Inverter(BTNodePtr child) {
    auto node = std::make_unique<BTInverter>();
    node->setChild(std::move(child));
    return node;
}

inline std::unique_ptr<BTSucceeder> Succeeder(BTNodePtr child) {
    auto node = std::make_unique<BTSucceeder>();
    node->setChild(std::move(child));
    return node;
}

inline std::unique_ptr<BTRepeater> Repeat(BTNodePtr child, int count = 0) {
    auto node = std::make_unique<BTRepeater>(count);
    node->setChild(std::move(child));
    return node;
}

inline std::unique_ptr<BTRepeatUntilFail> RepeatUntilFail(BTNodePtr child) {
    auto node = std::make_unique<BTRepeatUntilFail>();
    node->setChild(std::move(child));
    return node;
}

inline std::unique_ptr<BTCooldown> Cooldown(BTNodePtr child, float cooldownTime) {
    auto node = std::make_unique<BTCooldown>(cooldownTime);
    node->setChild(std::move(child));
    return node;
}

inline std::unique_ptr<BTTimeLimit> TimeLimit(BTNodePtr child, float maxTime) {
    auto node = std::make_unique<BTTimeLimit>(maxTime);
    node->setChild(std::move(child));
    return node;
}

// Create a wait node
inline std::unique_ptr<BTWait> Wait(float duration) {
    return std::make_unique<BTWait>(duration);
}

} // namespace BTBuilder
