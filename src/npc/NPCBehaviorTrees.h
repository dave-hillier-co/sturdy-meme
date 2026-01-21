#pragma once

#include "BehaviorTree.h"
#include "BTNodes.h"
#include <memory>

// Factory for creating behavior trees for different NPC archetypes
namespace NPCBehaviorTrees {

using namespace BTBuilder;
using namespace BTConditions;
using namespace BTActions;

// ============================================================================
// Hostile NPC - Aggressive enemy that chases and attacks on sight
// ============================================================================
// Behavior:
//   1. If sees player and in attack range -> Attack
//   2. If aware of player -> Chase
//   3. If lost player but has last known position -> Investigate
//   4. If far from spawn -> Return
//   5. If has patrol path -> Patrol
//   6. Otherwise -> Idle
// ============================================================================

inline std::unique_ptr<BehaviorTree> CreateHostileBehavior() {
    auto tree = std::make_unique<BehaviorTree>();

    // Root selector - tries each branch until one succeeds
    auto root = Selector();

    // === Branch 1: Combat - Attack if in range and ready ===
    {
        auto combat = Sequence();
        combat->addChild(CanSeePlayer());
        combat->addChild(IsInAttackRange());
        combat->addChild(HasAttackAwareness());
        combat->addChild(SetBehaviorState(BehaviorState::Attack));

        // Attack with cooldown
        auto attackSeq = Sequence();
        attackSeq->addChild(CanAttack());
        attackSeq->addChild(Attack());

        // Either attack or wait for cooldown
        auto attackOrWait = Selector();
        attackOrWait->addChild(std::move(attackSeq));
        attackOrWait->addChild(LookAtPlayer());  // Face player while waiting

        combat->addChild(std::move(attackOrWait));
        root->addChild(std::move(combat));
    }

    // === Branch 2: Chase - Pursue visible player ===
    {
        auto chase = Sequence();
        chase->addChild(HasHighAwareness());

        // Chase if can see player or has last known position
        auto hasTarget = Selector();
        hasTarget->addChild(CanSeePlayer());
        hasTarget->addChild(HasLastKnownPosition());
        chase->addChild(std::move(hasTarget));

        // But not if player is too far
        chase->addChild(Inverter(IsPlayerOutOfChaseRange()));
        chase->addChild(SetBehaviorState(BehaviorState::Chase));
        chase->addChild(MoveToPlayer());
        root->addChild(std::move(chase));
    }

    // === Branch 3: Investigate - Go to last known position ===
    {
        auto investigate = Sequence();
        investigate->addChild(HasLastKnownPosition());
        investigate->addChild(Inverter(CanSeePlayer()));
        investigate->addChild(SetBehaviorState(BehaviorState::Chase));  // Still alert
        investigate->addChild(MoveToLastKnownPosition());
        root->addChild(std::move(investigate));
    }

    // === Branch 4: Return to spawn if far away ===
    {
        auto returnHome = Sequence();
        returnHome->addChild(Inverter(IsAtSpawnPosition(10.0f)));
        returnHome->addChild(Inverter(IsAwareOfPlayer()));
        returnHome->addChild(SetBehaviorState(BehaviorState::Return));
        returnHome->addChild(ReturnToSpawn());
        root->addChild(std::move(returnHome));
    }

    // === Branch 5: Patrol if has patrol path ===
    {
        auto patrol = Sequence();
        patrol->addChild(HasPatrolPath());
        patrol->addChild(SetBehaviorState(BehaviorState::Patrol));
        patrol->addChild(Patrol());
        root->addChild(std::move(patrol));
    }

    // === Branch 6: Default - Idle ===
    {
        auto idle = Sequence();
        idle->addChild(SetBehaviorState(BehaviorState::Idle));
        idle->addChild(Idle());
        root->addChild(std::move(idle));
    }

    tree->setRoot(std::move(root));
    return tree;
}

// ============================================================================
// Neutral NPC - Becomes hostile if player invades personal space
// ============================================================================
// Behavior:
//   1. If hostile (was provoked) -> Use hostile behavior
//   2. If player in personal space -> Become hostile, warn
//   3. If player nearby -> Watch player suspiciously
//   4. If has patrol path -> Patrol
//   5. Otherwise -> Idle
// ============================================================================

inline std::unique_ptr<BehaviorTree> CreateNeutralBehavior() {
    auto tree = std::make_unique<BehaviorTree>();

    auto root = Selector();

    // === Branch 1: Already hostile - full combat mode ===
    {
        auto whenHostile = Sequence();
        whenHostile->addChild(IsHostile());

        // Hostile sub-tree (similar to hostile NPC)
        auto hostileBehavior = Selector();

        // Attack
        {
            auto attack = Sequence();
            attack->addChild(CanSeePlayer());
            attack->addChild(IsInAttackRange());
            attack->addChild(SetBehaviorState(BehaviorState::Attack));
            attack->addChild(Attack());
            hostileBehavior->addChild(std::move(attack));
        }

        // Chase
        {
            auto chase = Sequence();
            chase->addChild(HasHighAwareness());
            chase->addChild(Inverter(IsPlayerOutOfChaseRange()));
            chase->addChild(SetBehaviorState(BehaviorState::Chase));
            chase->addChild(MoveToPlayer());
            hostileBehavior->addChild(std::move(chase));
        }

        // Return and calm down after losing player
        {
            auto returnCalm = Sequence();
            returnCalm->addChild(Inverter(IsAwareOfPlayer()));
            returnCalm->addChild(SetHostility(HostilityLevel::Neutral, HostilityTrigger::Timeout));
            returnCalm->addChild(ForgetPlayer());
            returnCalm->addChild(SetBehaviorState(BehaviorState::Return));
            returnCalm->addChild(ReturnToSpawn());
            hostileBehavior->addChild(std::move(returnCalm));
        }

        whenHostile->addChild(std::move(hostileBehavior));
        root->addChild(std::move(whenHostile));
    }

    // === Branch 2: Player invaded personal space - become hostile ===
    {
        auto provoked = Sequence();
        provoked->addChild(CanSeePlayer());
        provoked->addChild(IsPlayerInPersonalSpace());
        provoked->addChild(SetHostility(HostilityLevel::Hostile, HostilityTrigger::PlayerProximity));
        provoked->addChild(Log("Get away from me!"));
        root->addChild(std::move(provoked));
    }

    // === Branch 3: Player nearby - watch suspiciously ===
    {
        auto suspicious = Sequence();
        suspicious->addChild(CanSeePlayer());
        suspicious->addChild(IsPlayerInRange(10.0f));
        suspicious->addChild(SetBehaviorState(BehaviorState::Idle));
        suspicious->addChild(LookAtPlayer());
        root->addChild(std::move(suspicious));
    }

    // === Branch 4: Patrol ===
    {
        auto patrol = Sequence();
        patrol->addChild(HasPatrolPath());
        patrol->addChild(SetBehaviorState(BehaviorState::Patrol));
        patrol->addChild(Patrol());
        root->addChild(std::move(patrol));
    }

    // === Branch 5: Idle ===
    {
        auto idle = Sequence();
        idle->addChild(SetBehaviorState(BehaviorState::Idle));
        idle->addChild(Idle());
        root->addChild(std::move(idle));
    }

    tree->setRoot(std::move(root));
    return tree;
}

// ============================================================================
// Afraid NPC - Flees from the player
// ============================================================================
// Behavior:
//   1. If player visible and close -> Flee
//   2. If fled far enough -> Stop fleeing
//   3. If player nearby but not too close -> Watch nervously
//   4. If has patrol path -> Patrol cautiously
//   5. Otherwise -> Idle (graze, etc.)
// ============================================================================

inline std::unique_ptr<BehaviorTree> CreateAfraidBehavior() {
    auto tree = std::make_unique<BehaviorTree>();

    auto root = Selector();

    // === Branch 1: Flee from close player ===
    {
        auto flee = Sequence();
        flee->addChild(CanSeePlayer());
        flee->addChild(IsPlayerInRange(20.0f));  // Start fleeing at 20m
        flee->addChild(Inverter(HasFledFarEnough()));
        flee->addChild(SetBehaviorState(BehaviorState::Flee));
        flee->addChild(FleeFromPlayer());
        root->addChild(std::move(flee));
    }

    // === Branch 2: Fled far enough - stop and rest ===
    {
        auto rest = Sequence();
        rest->addChild(HasFledFarEnough());
        rest->addChild(SetBehaviorState(BehaviorState::Idle));
        rest->addChild(Wait(2.0f));
        rest->addChild(ForgetPlayer());
        root->addChild(std::move(rest));
    }

    // === Branch 3: Player visible but not too close - nervous watching ===
    {
        auto nervous = Sequence();
        nervous->addChild(CanSeePlayer());
        nervous->addChild(SetBehaviorState(BehaviorState::Idle));
        nervous->addChild(LookAtPlayer());
        root->addChild(std::move(nervous));
    }

    // === Branch 4: Return to spawn if far ===
    {
        auto returnHome = Sequence();
        returnHome->addChild(Inverter(IsAtSpawnPosition(15.0f)));
        returnHome->addChild(Inverter(IsAwareOfPlayer()));
        returnHome->addChild(SetBehaviorState(BehaviorState::Return));
        returnHome->addChild(ReturnToSpawn());
        root->addChild(std::move(returnHome));
    }

    // === Branch 5: Patrol (graze) ===
    {
        auto patrol = Sequence();
        patrol->addChild(HasPatrolPath());
        patrol->addChild(SetBehaviorState(BehaviorState::Patrol));
        patrol->addChild(Patrol());
        root->addChild(std::move(patrol));
    }

    // === Branch 6: Idle ===
    {
        auto idle = Sequence();
        idle->addChild(SetBehaviorState(BehaviorState::Idle));
        idle->addChild(Idle());
        root->addChild(std::move(idle));
    }

    tree->setRoot(std::move(root));
    return tree;
}

// ============================================================================
// Friendly NPC - Never attacks, may approach and interact
// ============================================================================
// Behavior:
//   1. If player nearby -> Wave/greet (cooldown), then follow briefly
//   2. If has patrol path -> Patrol
//   3. Otherwise -> Idle
// ============================================================================

inline std::unique_ptr<BehaviorTree> CreateFriendlyBehavior() {
    auto tree = std::make_unique<BehaviorTree>();

    auto root = Selector();

    // === Branch 1: Greet player when they come close ===
    {
        auto greet = Sequence();
        greet->addChild(CanSeePlayer());
        greet->addChild(IsPlayerInRange(8.0f));
        greet->addChild(SetBehaviorState(BehaviorState::Idle));

        // Greet with cooldown (don't spam greetings)
        auto greetAction = Cooldown(Log("Hello there!"), 10.0f);

        auto greetSeq = Sequence();
        greetSeq->addChild(std::move(greetAction));
        greetSeq->addChild(LookAtPlayer());
        greetSeq->addChild(Wait(2.0f));

        greet->addChild(Succeeder(std::move(greetSeq)));  // Always succeed even if on cooldown
        greet->addChild(LookAtPlayer());
        root->addChild(std::move(greet));
    }

    // === Branch 2: Patrol ===
    {
        auto patrol = Sequence();
        patrol->addChild(HasPatrolPath());
        patrol->addChild(SetBehaviorState(BehaviorState::Patrol));
        patrol->addChild(Patrol());
        root->addChild(std::move(patrol));
    }

    // === Branch 3: Idle ===
    {
        auto idle = Sequence();
        idle->addChild(SetBehaviorState(BehaviorState::Idle));
        idle->addChild(Idle());
        root->addChild(std::move(idle));
    }

    tree->setRoot(std::move(root));
    return tree;
}

// ============================================================================
// Guard NPC - Patrols, warns intruders, then attacks
// ============================================================================
// More sophisticated neutral that gives warnings before attacking
// ============================================================================

inline std::unique_ptr<BehaviorTree> CreateGuardBehavior() {
    auto tree = std::make_unique<BehaviorTree>();

    auto root = Selector();

    // === Branch 1: Combat mode (after warning) ===
    {
        auto combat = Sequence();
        combat->addChild(IsHostile());

        auto combatBehavior = Selector();

        // Attack in range
        {
            auto attack = Sequence();
            attack->addChild(CanSeePlayer());
            attack->addChild(IsInAttackRange());
            attack->addChild(SetBehaviorState(BehaviorState::Attack));
            attack->addChild(Attack());
            combatBehavior->addChild(std::move(attack));
        }

        // Chase
        {
            auto chase = Sequence();
            chase->addChild(HasHighAwareness());
            chase->addChild(Inverter(IsPlayerOutOfChaseRange()));
            chase->addChild(SetBehaviorState(BehaviorState::Chase));
            chase->addChild(MoveToPlayer());
            combatBehavior->addChild(std::move(chase));
        }

        // Lost player - return and reset
        {
            auto reset = Sequence();
            reset->addChild(Inverter(IsAwareOfPlayer()));
            reset->addChild(SetHostility(HostilityLevel::Neutral, HostilityTrigger::PlayerFled));
            reset->addChild(ForgetPlayer());
            reset->addChild(SetBehaviorState(BehaviorState::Return));
            reset->addChild(ReturnToSpawn());
            combatBehavior->addChild(std::move(reset));
        }

        combat->addChild(std::move(combatBehavior));
        root->addChild(std::move(combat));
    }

    // === Branch 2: Warning phase - player too close ===
    {
        auto warn = Sequence();
        warn->addChild(IsNeutral());
        warn->addChild(CanSeePlayer());
        warn->addChild(IsPlayerInPersonalSpace());
        warn->addChild(SetBehaviorState(BehaviorState::Idle));
        warn->addChild(LookAtPlayer());

        // First warning
        auto warnOnce = Cooldown(
            Sequence(),  // Need proper sequence
            5.0f
        );

        // Check if already warned (using blackboard)
        auto checkWarned = Sequence();
        checkWarned->addChild(BlackboardHas("warned_player"));
        checkWarned->addChild(SetHostility(HostilityLevel::Hostile, HostilityTrigger::PlayerProximity));
        checkWarned->addChild(Log("I warned you!"));

        auto firstWarn = Sequence();
        firstWarn->addChild(Inverter(BlackboardHas("warned_player")));
        firstWarn->addChild(Log("Halt! Come no closer!"));
        firstWarn->addChild(SetBlackboard<bool>("warned_player", true));

        auto warnSelector = Selector();
        warnSelector->addChild(std::move(checkWarned));
        warnSelector->addChild(std::move(firstWarn));

        warn->addChild(std::move(warnSelector));
        root->addChild(std::move(warn));
    }

    // === Branch 3: Player visible - watch ===
    {
        auto watch = Sequence();
        watch->addChild(CanSeePlayer());
        watch->addChild(IsPlayerInRange(15.0f));
        watch->addChild(SetBehaviorState(BehaviorState::Idle));
        watch->addChild(LookAtPlayer());
        watch->addChild(ClearBlackboard("warned_player"));  // Reset warning if they backed off
        root->addChild(std::move(watch));
    }

    // === Branch 4: Patrol ===
    {
        auto patrol = Sequence();
        patrol->addChild(HasPatrolPath());
        patrol->addChild(SetBehaviorState(BehaviorState::Patrol));
        patrol->addChild(ClearBlackboard("warned_player"));
        patrol->addChild(Patrol());
        root->addChild(std::move(patrol));
    }

    // === Branch 5: Idle at post ===
    {
        auto idle = Sequence();
        idle->addChild(SetBehaviorState(BehaviorState::Idle));
        idle->addChild(Idle());
        root->addChild(std::move(idle));
    }

    tree->setRoot(std::move(root));
    return tree;
}

// Factory function to create behavior tree based on hostility
inline std::unique_ptr<BehaviorTree> CreateBehaviorTree(HostilityLevel hostility) {
    switch (hostility) {
        case HostilityLevel::Hostile:
            return CreateHostileBehavior();
        case HostilityLevel::Neutral:
            return CreateNeutralBehavior();
        case HostilityLevel::Afraid:
            return CreateAfraidBehavior();
        case HostilityLevel::Friendly:
            return CreateFriendlyBehavior();
        default:
            return CreateNeutralBehavior();
    }
}

} // namespace NPCBehaviorTrees
