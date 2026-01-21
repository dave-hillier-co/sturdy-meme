#pragma once

#include <memory>
#include <vector>
#include <string>
#include <functional>
#include <unordered_map>
#include <any>

// Forward declarations
struct NPC;
class PhysicsWorld;

// Result of a behavior tree node tick
enum class BTStatus {
    Success,    // Node completed successfully
    Failure,    // Node failed
    Running     // Node is still executing (will be ticked again)
};

// Blackboard for sharing data between nodes
class Blackboard {
public:
    template<typename T>
    void set(const std::string& key, T value) {
        data_[key] = std::make_any<T>(std::move(value));
    }

    template<typename T>
    T get(const std::string& key, T defaultValue = T{}) const {
        auto it = data_.find(key);
        if (it != data_.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return defaultValue;
            }
        }
        return defaultValue;
    }

    bool has(const std::string& key) const {
        return data_.find(key) != data_.end();
    }

    void remove(const std::string& key) {
        data_.erase(key);
    }

    void clear() {
        data_.clear();
    }

private:
    std::unordered_map<std::string, std::any> data_;
};

// Context passed to behavior tree nodes during tick
struct BTContext {
    NPC* npc = nullptr;
    const glm::vec3* playerPosition = nullptr;
    const PhysicsWorld* physics = nullptr;
    Blackboard* blackboard = nullptr;
    float deltaTime = 0.0f;
};

// Base class for all behavior tree nodes
class BTNode {
public:
    virtual ~BTNode() = default;

    // Tick the node and return its status
    virtual BTStatus tick(BTContext& ctx) = 0;

    // Reset the node state (called when tree restarts or node needs reset)
    virtual void reset() {}

    // Optional name for debugging
    void setName(const std::string& name) { name_ = name; }
    const std::string& getName() const { return name_; }

protected:
    std::string name_;
};

using BTNodePtr = std::unique_ptr<BTNode>;

// ============================================================================
// Composite Nodes - Have multiple children
// ============================================================================

// Base class for composite nodes
class BTComposite : public BTNode {
public:
    void addChild(BTNodePtr child) {
        children_.push_back(std::move(child));
    }

    void reset() override {
        currentChild_ = 0;
        for (auto& child : children_) {
            child->reset();
        }
    }

protected:
    std::vector<BTNodePtr> children_;
    size_t currentChild_ = 0;
};

// Selector (OR) - Returns Success if ANY child succeeds
// Tries children in order until one succeeds or all fail
class BTSelector : public BTComposite {
public:
    BTStatus tick(BTContext& ctx) override {
        while (currentChild_ < children_.size()) {
            BTStatus status = children_[currentChild_]->tick(ctx);

            if (status == BTStatus::Success) {
                currentChild_ = 0;  // Reset for next tick
                return BTStatus::Success;
            }

            if (status == BTStatus::Running) {
                return BTStatus::Running;
            }

            // Failure - try next child
            currentChild_++;
        }

        currentChild_ = 0;
        return BTStatus::Failure;
    }
};

// Sequence (AND) - Returns Success only if ALL children succeed
// Runs children in order until one fails or all succeed
class BTSequence : public BTComposite {
public:
    BTStatus tick(BTContext& ctx) override {
        while (currentChild_ < children_.size()) {
            BTStatus status = children_[currentChild_]->tick(ctx);

            if (status == BTStatus::Failure) {
                currentChild_ = 0;
                return BTStatus::Failure;
            }

            if (status == BTStatus::Running) {
                return BTStatus::Running;
            }

            // Success - continue to next child
            currentChild_++;
        }

        currentChild_ = 0;
        return BTStatus::Success;
    }
};

// Parallel - Runs all children simultaneously
// Policies determine success/failure conditions
class BTParallel : public BTComposite {
public:
    enum class Policy {
        RequireOne,  // Succeed if one child succeeds
        RequireAll   // Succeed only if all children succeed
    };

    BTParallel(Policy successPolicy = Policy::RequireAll,
               Policy failurePolicy = Policy::RequireOne)
        : successPolicy_(successPolicy), failurePolicy_(failurePolicy) {}

    BTStatus tick(BTContext& ctx) override {
        size_t successCount = 0;
        size_t failureCount = 0;

        for (auto& child : children_) {
            BTStatus status = child->tick(ctx);

            if (status == BTStatus::Success) {
                successCount++;
                if (successPolicy_ == Policy::RequireOne) {
                    return BTStatus::Success;
                }
            } else if (status == BTStatus::Failure) {
                failureCount++;
                if (failurePolicy_ == Policy::RequireOne) {
                    return BTStatus::Failure;
                }
            }
        }

        if (failurePolicy_ == Policy::RequireAll && failureCount == children_.size()) {
            return BTStatus::Failure;
        }
        if (successPolicy_ == Policy::RequireAll && successCount == children_.size()) {
            return BTStatus::Success;
        }

        return BTStatus::Running;
    }

private:
    Policy successPolicy_;
    Policy failurePolicy_;
};

// Random Selector - Picks a random child to execute
class BTRandomSelector : public BTComposite {
public:
    BTStatus tick(BTContext& ctx) override {
        if (children_.empty()) return BTStatus::Failure;

        // Shuffle order on first tick (when not running)
        if (!isRunning_) {
            shuffledIndices_.clear();
            for (size_t i = 0; i < children_.size(); i++) {
                shuffledIndices_.push_back(i);
            }
            // Simple Fisher-Yates shuffle
            for (size_t i = shuffledIndices_.size() - 1; i > 0; i--) {
                size_t j = static_cast<size_t>(rand()) % (i + 1);
                std::swap(shuffledIndices_[i], shuffledIndices_[j]);
            }
            isRunning_ = true;
        }

        while (currentChild_ < shuffledIndices_.size()) {
            BTStatus status = children_[shuffledIndices_[currentChild_]]->tick(ctx);

            if (status == BTStatus::Success) {
                reset();
                return BTStatus::Success;
            }

            if (status == BTStatus::Running) {
                return BTStatus::Running;
            }

            currentChild_++;
        }

        reset();
        return BTStatus::Failure;
    }

    void reset() override {
        BTComposite::reset();
        isRunning_ = false;
        shuffledIndices_.clear();
    }

private:
    std::vector<size_t> shuffledIndices_;
    bool isRunning_ = false;
};

// ============================================================================
// Decorator Nodes - Modify behavior of a single child
// ============================================================================

class BTDecorator : public BTNode {
public:
    void setChild(BTNodePtr child) {
        child_ = std::move(child);
    }

    void reset() override {
        if (child_) child_->reset();
    }

protected:
    BTNodePtr child_;
};

// Inverter - Inverts Success/Failure of child
class BTInverter : public BTDecorator {
public:
    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        BTStatus status = child_->tick(ctx);
        if (status == BTStatus::Success) return BTStatus::Failure;
        if (status == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running;
    }
};

// Succeeder - Always returns Success (unless Running)
class BTSucceeder : public BTDecorator {
public:
    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Success;

        BTStatus status = child_->tick(ctx);
        if (status == BTStatus::Running) return BTStatus::Running;
        return BTStatus::Success;
    }
};

// Failer - Always returns Failure (unless Running)
class BTFailer : public BTDecorator {
public:
    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        BTStatus status = child_->tick(ctx);
        if (status == BTStatus::Running) return BTStatus::Running;
        return BTStatus::Failure;
    }
};

// Repeater - Repeats child N times (or forever if count == 0)
class BTRepeater : public BTDecorator {
public:
    explicit BTRepeater(int count = 0) : repeatCount_(count) {}

    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        BTStatus status = child_->tick(ctx);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        currentCount_++;

        // Forever mode (repeatCount_ == 0) always returns Running
        if (repeatCount_ == 0) {
            child_->reset();
            return BTStatus::Running;
        }

        if (currentCount_ >= repeatCount_) {
            currentCount_ = 0;
            return status;  // Return last child status
        }

        child_->reset();
        return BTStatus::Running;
    }

    void reset() override {
        BTDecorator::reset();
        currentCount_ = 0;
    }

private:
    int repeatCount_;
    int currentCount_ = 0;
};

// RepeatUntilFail - Keeps running child until it fails
class BTRepeatUntilFail : public BTDecorator {
public:
    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        BTStatus status = child_->tick(ctx);

        if (status == BTStatus::Failure) {
            return BTStatus::Success;  // We succeeded in making it fail!
        }

        if (status == BTStatus::Success) {
            child_->reset();
        }

        return BTStatus::Running;
    }
};

// Cooldown - Prevents child from running again for a duration after success
class BTCooldown : public BTDecorator {
public:
    explicit BTCooldown(float cooldownTime) : cooldownTime_(cooldownTime) {}

    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        // Update cooldown timer
        if (remainingCooldown_ > 0.0f) {
            remainingCooldown_ -= ctx.deltaTime;
            return BTStatus::Failure;  // Still cooling down
        }

        BTStatus status = child_->tick(ctx);

        if (status == BTStatus::Success) {
            remainingCooldown_ = cooldownTime_;
        }

        return status;
    }

    void reset() override {
        BTDecorator::reset();
        remainingCooldown_ = 0.0f;
    }

private:
    float cooldownTime_;
    float remainingCooldown_ = 0.0f;
};

// TimeLimit - Fails child if it runs too long
class BTTimeLimit : public BTDecorator {
public:
    explicit BTTimeLimit(float maxTime) : maxTime_(maxTime) {}

    BTStatus tick(BTContext& ctx) override {
        if (!child_) return BTStatus::Failure;

        elapsedTime_ += ctx.deltaTime;

        if (elapsedTime_ >= maxTime_) {
            reset();
            return BTStatus::Failure;
        }

        BTStatus status = child_->tick(ctx);

        if (status != BTStatus::Running) {
            elapsedTime_ = 0.0f;
        }

        return status;
    }

    void reset() override {
        BTDecorator::reset();
        elapsedTime_ = 0.0f;
    }

private:
    float maxTime_;
    float elapsedTime_ = 0.0f;
};

// ============================================================================
// Leaf Nodes - Conditions and Actions (base classes)
// ============================================================================

// Condition - Instant check, returns Success or Failure
class BTCondition : public BTNode {
public:
    using ConditionFunc = std::function<bool(BTContext&)>;

    explicit BTCondition(ConditionFunc func) : func_(std::move(func)) {}

    BTStatus tick(BTContext& ctx) override {
        return func_(ctx) ? BTStatus::Success : BTStatus::Failure;
    }

private:
    ConditionFunc func_;
};

// Action - Can run over multiple ticks
class BTAction : public BTNode {
public:
    using ActionFunc = std::function<BTStatus(BTContext&)>;

    explicit BTAction(ActionFunc func) : func_(std::move(func)) {}

    BTStatus tick(BTContext& ctx) override {
        return func_(ctx);
    }

private:
    ActionFunc func_;
};

// Wait - Returns Running for a specified duration, then Success
class BTWait : public BTNode {
public:
    explicit BTWait(float duration) : duration_(duration) {}

    BTStatus tick(BTContext& ctx) override {
        elapsedTime_ += ctx.deltaTime;

        if (elapsedTime_ >= duration_) {
            elapsedTime_ = 0.0f;
            return BTStatus::Success;
        }

        return BTStatus::Running;
    }

    void reset() override {
        elapsedTime_ = 0.0f;
    }

private:
    float duration_;
    float elapsedTime_ = 0.0f;
};

// ============================================================================
// Behavior Tree - Root container
// ============================================================================

class BehaviorTree {
public:
    BehaviorTree() : blackboard_(std::make_unique<Blackboard>()) {}

    void setRoot(BTNodePtr root) {
        root_ = std::move(root);
    }

    BTStatus tick(NPC* npc, const glm::vec3& playerPosition,
                  const PhysicsWorld* physics, float deltaTime) {
        if (!root_) return BTStatus::Failure;

        BTContext ctx;
        ctx.npc = npc;
        ctx.playerPosition = &playerPosition;
        ctx.physics = physics;
        ctx.blackboard = blackboard_.get();
        ctx.deltaTime = deltaTime;

        return root_->tick(ctx);
    }

    void reset() {
        if (root_) root_->reset();
    }

    Blackboard& getBlackboard() { return *blackboard_; }

private:
    BTNodePtr root_;
    std::unique_ptr<Blackboard> blackboard_;
};
