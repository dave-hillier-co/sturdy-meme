#pragma once

#include "TreeOptions.h"
#include "TreeGenerator.h"
#include "loading/LoadJobQueue.h"
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <deque>
#include <mutex>

/**
 * ThreadedTreeGenerator - Generates tree meshes in parallel using LoadJobQueue
 *
 * Usage:
 * 1. Create with worker count
 * 2. Add tree generation requests via queueTree()
 * 3. Call update() periodically to retrieve completed trees
 * 4. Upload staged trees to GPU as they complete
 *
 * Thread safety:
 * - queueTree() can be called from any thread
 * - update() must be called from main thread (retrieves completed jobs)
 * - Tree mesh generation happens on worker threads (CPU only)
 */
class ThreadedTreeGenerator {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit ThreadedTreeGenerator(ConstructToken) {}

    /**
     * Request for tree generation
     */
    struct TreeRequest {
        glm::vec3 position;
        float rotation = 0.0f;
        float scale = 1.0f;
        TreeOptions options;
        uint32_t archetypeIndex = 0;  // For impostor archetype assignment
    };

    /**
     * Completed tree mesh data (CPU-side, ready for GPU upload)
     */
    struct StagedTree {
        // Branch mesh geometry
        std::vector<uint8_t> branchVertexData;
        std::vector<uint32_t> branchIndices;
        uint32_t branchVertexCount = 0;

        // Leaf instance data
        std::vector<uint8_t> leafInstanceData;  // LeafInstanceGPU structs
        uint32_t leafInstanceCount = 0;

        // Tree placement
        glm::vec3 position;
        float rotation = 0.0f;
        float scale = 1.0f;

        // Options for texture selection
        TreeOptions options;
        uint32_t archetypeIndex = 0;
    };

    /**
     * Factory: Create and initialize the threaded generator
     * @param workerCount Number of worker threads (default: 4)
     */
    static std::unique_ptr<ThreadedTreeGenerator> create(uint32_t workerCount = 4);


    ~ThreadedTreeGenerator();

    // Non-copyable, non-movable
    ThreadedTreeGenerator(const ThreadedTreeGenerator&) = delete;
    ThreadedTreeGenerator& operator=(const ThreadedTreeGenerator&) = delete;

    /**
     * Bound the number of trees that may be staged in CPU memory at once
     * (submitted to workers but not yet retrieved via getCompletedTrees()).
     * Requests beyond the cap are held as lightweight TreeRequests and fed to
     * the workers as completed trees are consumed, so staged geometry never
     * piles up faster than the consumer uploads it. 0 = unbounded (default).
     * waitForAll() lifts the cap so a blocking consumer cannot deadlock.
     */
    void setMaxStagedTrees(uint32_t cap);

    /**
     * Queue a tree for generation on a background thread
     * Thread-safe: can be called from any thread
     */
    void queueTree(const TreeRequest& request);

    /**
     * Queue multiple trees at once (more efficient)
     * Thread-safe: can be called from any thread
     */
    void queueTrees(const std::vector<TreeRequest>& requests);

    /**
     * Retrieve completed trees (call from main thread)
     * Returns trees ready for GPU upload
     */
    std::vector<StagedTree> getCompletedTrees();

    /**
     * Check if all queued trees have been generated
     */
    bool isComplete() const;

    /**
     * Get progress information
     */
    Loading::LoadProgress getProgress() const;

    /**
     * Block until all trees are generated
     */
    void waitForAll();

    /**
     * Get count of pending trees (queued but not yet returned by getCompletedTrees())
     */
    uint32_t getPendingCount() const { return pendingCount_.load(); }

    /**
     * Get count of completed trees
     */
    uint32_t getCompletedCount() const { return completedCount_.load(); }

private:
    bool init(uint32_t workerCount);

    // Move held requests onto the job queue while under the staged-tree cap
    void submitHeldRequests();

    std::unique_ptr<Loading::LoadJobQueue> jobQueue_;
    std::atomic<uint32_t> pendingCount_{0};
    std::atomic<uint32_t> completedCount_{0};
    std::atomic<uint32_t> totalQueued_{0};

    // Requests accepted but not yet handed to workers (protected by requestMutex_)
    mutable std::mutex requestMutex_;
    std::deque<TreeRequest> heldRequests_;
    uint32_t maxStaged_ = 0;
    uint32_t inFlight_ = 0;  // Submitted to workers, not yet retrieved
    uint32_t nextJobId_ = 0;
};
