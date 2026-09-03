#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include "VmaBuffer.h"
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class VulkanContext;

/**
 * Handle to a pending async transfer operation.
 * Check isComplete() or wait() before using the transferred resource.
 */
struct TransferHandle {
    uint64_t id = 0;

    bool isValid() const { return id != 0; }
};

/**
 * AsyncTransferManager - Non-blocking GPU transfer system.
 *
 * Implements the async transfer pattern:
 * 1. Copy data to staging buffer
 * 2. Submit transfer command with timeline semaphore signal (non-blocking)
 * 3. Poll timeline counter each frame via processPendingTransfers() (non-blocking)
 * 4. When a dedicated transfer queue is in use, submit the matching queue-family
 *    ownership *acquire* barrier on the graphics queue (Vulkan does not perform
 *    this implicitly) and track it with a fence
 * 5. Execute completion callback once the resource is owned by the graphics queue
 *
 * Key design points:
 * - Uses dedicated transfer queue when available
 * - Timeline semaphore synchronization (Vulkan 1.2) for efficient non-blocking checks
 * - Single timeline semaphore with monotonic counter vs per-transfer fences
 * - Staging buffer pooling for reduced allocation overhead
 * - Supports both buffer and image transfers
 *
 * Threading:
 * - submit*() may be called from background loader threads.
 * - processPendingTransfers()/wait*() must be called from the thread that owns
 *   the graphics queue (the render thread), because the ownership-acquire is
 *   submitted there. Internal state (command pools, timeline counter, pending
 *   list) is guarded by mutexes so submission can race with processing.
 */
class AsyncTransferManager {
public:
    using CompletionCallback = std::function<void()>;

    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    AsyncTransferManager(ConstructToken, VulkanContext& context);

    // Drains every pending transfer (waitAll), then the members release
    // themselves in reverse declaration order: staging buffers and pending
    // transfers (with their acquire fences) before the timeline semaphore,
    // before the command pools.
    ~AsyncTransferManager();

    // Non-copyable
    AsyncTransferManager(const AsyncTransferManager&) = delete;
    AsyncTransferManager& operator=(const AsyncTransferManager&) = delete;

    /**
     * Create the transfer manager.
     * @param context Vulkan context (provides device, queues, allocator; must outlive the manager)
     * @return nullptr on failure
     */
    static std::unique_ptr<AsyncTransferManager> create(VulkanContext& context);

    /**
     * Submit a buffer transfer (CPU to GPU).
     * @param data Source data pointer
     * @param size Size in bytes
     * @param dstBuffer Destination GPU buffer
     * @param dstOffset Offset into destination buffer
     * @param onComplete Optional callback when transfer completes
     * @return Handle to track transfer completion
     */
    TransferHandle submitBufferTransfer(
        const void* data, vk::DeviceSize size,
        vk::Buffer dstBuffer, vk::DeviceSize dstOffset = 0,
        CompletionCallback onComplete = nullptr);

    /**
     * Submit an image transfer (CPU to GPU).
     * Handles layout transitions: undefined -> transferDstOptimal -> finalLayout
     * @param data Source pixel data
     * @param size Size in bytes
     * @param dstImage Destination GPU image
     * @param extent Image dimensions
     * @param finalLayout Layout after transfer (usually shaderReadOnlyOptimal)
     * @param mipLevels Number of mip levels (1 for no mipmaps)
     * @param layerCount Number of array layers (1 for non-array)
     * @param onComplete Optional callback when transfer completes
     * @return Handle to track transfer completion
     */
    TransferHandle submitImageTransfer(
        const void* data, vk::DeviceSize size,
        vk::Image dstImage, vk::Extent3D extent,
        vk::ImageLayout finalLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
        uint32_t mipLevels = 1,
        uint32_t layerCount = 1,
        CompletionCallback onComplete = nullptr);

    /**
     * Check if a specific transfer is complete.
     */
    bool isComplete(TransferHandle handle) const;

    /**
     * Block until a specific transfer completes.
     */
    void wait(TransferHandle handle);

    /**
     * Poll and process completed transfers.
     * Call once per frame from the main/render thread.
     * Executes completion callbacks and releases staging resources.
     */
    void processPendingTransfers();

    /**
     * Wait for all pending transfers to complete.
     * Useful before shutdown or when resources must be ready.
     */
    void waitAll();

    /**
     * Get count of pending transfers.
     */
    size_t getPendingCount() const;

private:
    struct PendingTransfer {
        uint64_t id;
        uint64_t timelineValue;  // Transfer-queue timeline value signalled by the copy/release
        vk::CommandBuffer cmdBuffer;  // From transfer pool
        VmaBuffer stagingBuffer;
        CompletionCallback onComplete;
        bool needsOwnershipTransfer = false;
        vk::Image targetImage;  // For queue ownership transfer
        vk::ImageLayout finalLayout = vk::ImageLayout::eUndefined;
        uint32_t mipLevels = 1;
        uint32_t layerCount = 1;

        // Queue-family ownership *acquire* tracking (dedicated transfer queue only).
        // The acquire barrier is recorded on the graphics queue and tracked by a
        // fence; the transfer is only complete once that fence is signalled.
        bool acquireSubmitted = false;
        vk::CommandBuffer acquireCmdBuffer;  // From graphics pool
        std::optional<vk::raii::Fence> acquireFence;
    };

    // Allocate command buffer from transfer pool
    vk::CommandBuffer allocateTransferCommandBuffer();

    // Free command buffer back to pool
    void freeTransferCommandBuffer(vk::CommandBuffer cmd);

    // Allocate/free command buffers from the graphics pool (ownership-acquire barriers)
    vk::CommandBuffer allocateGraphicsCommandBuffer();
    void freeGraphicsCommandBuffer(vk::CommandBuffer cmd);

    // Record + submit the queue-family ownership acquire on the graphics queue.
    // Must be called with transferMutex_ held. Populates the transfer's acquire
    // command buffer and fence. Caller must guarantee needsOwnershipTransfer.
    void submitOwnershipAcquire(PendingTransfer& transfer);

    // True once a transfer's GPU work has fully completed and the resource is
    // owned by the consuming queue (acquire fence signalled for ownership
    // transfers). Caller must hold transferMutex_.
    static bool transferComplete(const PendingTransfer& transfer,
                                 uint64_t currentTimelineValue);

    // Get or create staging buffer of at least the given size
    VmaBuffer acquireStagingBuffer(vk::DeviceSize size);

    // Return staging buffer to pool for reuse
    void releaseStagingBuffer(VmaBuffer buffer);

    std::optional<std::reference_wrapper<VulkanContext>> context_;
    vk::Device device_;
    vk::Queue transferQueue_;
    vk::Queue graphicsQueue_;  // Destination of ownership-acquire submissions
    uint32_t transferQueueFamily_ = 0;
    uint32_t graphicsQueueFamily_ = 0;
    bool hasDedicatedTransfer_ = false;
    VmaAllocator allocator_ = nullptr;

    // Command pool for transfer operations (created per transfer queue family).
    // Vulkan command pools are externally synchronised: commandPoolMutex_ guards
    // every allocate/free against the background submit / render-thread process race.
    std::optional<vk::raii::CommandPool> transferCommandPool_;
    // Command pool for ownership-acquire barriers on the graphics queue (only
    // created when a dedicated transfer queue is present).
    std::optional<vk::raii::CommandPool> graphicsCommandPool_;
    std::mutex commandPoolMutex_;

    // Timeline semaphore for tracking transfer completion (Vulkan 1.2)
    std::optional<vk::raii::Semaphore> transferTimeline_;
    uint64_t nextTimelineValue_ = 1;  // Next value to signal

    // Serialises timeline-value assignment with the queue submission so that
    // signalled values are monotonically increasing in submission order, even
    // when submit*() is called concurrently from multiple loader threads.
    std::mutex submitMutex_;

    // Pending transfers
    std::deque<PendingTransfer> pendingTransfers_;
    mutable std::mutex transferMutex_;

    // Transfer ID counter (guarded by submitMutex_)
    uint64_t nextTransferId_ = 1;

    // Staging buffer pool (for reuse)
    std::vector<VmaBuffer> stagingBufferPool_;
    std::mutex stagingMutex_;
    static constexpr size_t MAX_STAGING_POOL_SIZE = 8;

    // True once every GPU object exists (false only when create() failed part-way)
    bool ready() const { return transferTimeline_.has_value(); }
};
