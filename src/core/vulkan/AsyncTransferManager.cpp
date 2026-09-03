#include "AsyncTransferManager.h"
#include "VulkanContext.h"
#include "QueueLock.h"
#include "VmaBufferFactory.h"
#include <SDL3/SDL_log.h>

std::unique_ptr<AsyncTransferManager> AsyncTransferManager::create(VulkanContext& context) {
    auto manager = std::make_unique<AsyncTransferManager>(ConstructToken{}, context);
    if (!manager->ready()) {
        return nullptr;  // whatever was created dies with the object
    }
    return manager;
}

AsyncTransferManager::~AsyncTransferManager() {
    if (!ready()) {
        return;
    }

    waitAll();

    // Clear staging buffer pool
    {
        std::lock_guard<std::mutex> lock(stagingMutex_);
        stagingBufferPool_.clear();
    }

    // Clear any remaining transfers
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.clear();
    }

    // Timeline semaphore and command pools are released by their destructors
    SDL_Log("AsyncTransferManager: Shutdown complete");
}

AsyncTransferManager::AsyncTransferManager(ConstructToken, VulkanContext& context)
    : context_(context)
    , device_(context.getVkDevice())
    , transferQueue_(context.getVkTransferQueue())
    , graphicsQueue_(context.getVkGraphicsQueue())
    , transferQueueFamily_(context.getTransferQueueFamily())
    , graphicsQueueFamily_(context.getGraphicsQueueFamily())
    , hasDedicatedTransfer_(context.hasDedicatedTransferQueue())
    , allocator_(context.getAllocator()) {
    // Create command pool for transfer queue
    auto poolInfo = vk::CommandPoolCreateInfo{}
        .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                  vk::CommandPoolCreateFlagBits::eTransient)
        .setQueueFamilyIndex(transferQueueFamily_);

    try {
        transferCommandPool_.emplace(context.getRaiiDevice(), poolInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create command pool: %s", e.what());
        return;
    }

    // With a dedicated transfer queue, textures destined for graphics access need
    // a queue-family ownership transfer: the release is recorded on the transfer
    // queue, but the matching acquire must be recorded on the graphics queue.
    // Allocate a separate pool for those acquire command buffers.
    if (hasDedicatedTransfer_) {
        auto graphicsPoolInfo = vk::CommandPoolCreateInfo{}
            .setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
                      vk::CommandPoolCreateFlagBits::eTransient)
            .setQueueFamilyIndex(graphicsQueueFamily_);
        try {
            graphicsCommandPool_.emplace(context.getRaiiDevice(), graphicsPoolInfo);
        } catch (const vk::SystemError& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                "AsyncTransferManager: Failed to create graphics acquire command pool: %s", e.what());
            return;
        }
    }

    // Create timeline semaphore for tracking transfer completion (Vulkan 1.2)
    try {
        auto typeInfo = vk::SemaphoreTypeCreateInfo{}
            .setSemaphoreType(vk::SemaphoreType::eTimeline)
            .setInitialValue(0);

        auto semaphoreInfo = vk::SemaphoreCreateInfo{}
            .setPNext(&typeInfo);

        transferTimeline_.emplace(context.getRaiiDevice(), semaphoreInfo);
        nextTimelineValue_ = 1;
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create timeline semaphore: %s", e.what());
        return;
    }

    SDL_Log("AsyncTransferManager: Initialized with timeline semaphore (dedicated transfer: %s)",
            hasDedicatedTransfer_ ? "yes" : "no");
}

vk::CommandBuffer AsyncTransferManager::allocateTransferCommandBuffer() {
    std::lock_guard<std::mutex> lock(commandPoolMutex_);
    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(*transferCommandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    auto buffers = device_.allocateCommandBuffers(allocInfo);
    return buffers[0];
}

void AsyncTransferManager::freeTransferCommandBuffer(vk::CommandBuffer cmd) {
    std::lock_guard<std::mutex> lock(commandPoolMutex_);
    device_.freeCommandBuffers(*transferCommandPool_, cmd);
}

vk::CommandBuffer AsyncTransferManager::allocateGraphicsCommandBuffer() {
    std::lock_guard<std::mutex> lock(commandPoolMutex_);
    auto allocInfo = vk::CommandBufferAllocateInfo{}
        .setCommandPool(*graphicsCommandPool_)
        .setLevel(vk::CommandBufferLevel::ePrimary)
        .setCommandBufferCount(1);

    auto buffers = device_.allocateCommandBuffers(allocInfo);
    return buffers[0];
}

void AsyncTransferManager::freeGraphicsCommandBuffer(vk::CommandBuffer cmd) {
    std::lock_guard<std::mutex> lock(commandPoolMutex_);
    device_.freeCommandBuffers(*graphicsCommandPool_, cmd);
}

VmaBuffer AsyncTransferManager::acquireStagingBuffer(vk::DeviceSize size) {
    // Try to find a buffer from the pool that's large enough
    {
        std::lock_guard<std::mutex> lock(stagingMutex_);
        for (auto it = stagingBufferPool_.begin(); it != stagingBufferPool_.end(); ++it) {
            VmaAllocationInfo allocInfo;
            vmaGetAllocationInfo(allocator_, it->getAllocation(), &allocInfo);
            if (allocInfo.size >= size) {
                VmaBuffer buffer = std::move(*it);
                stagingBufferPool_.erase(it);
                return buffer;
            }
        }
    }

    // Create new staging buffer
    VmaBuffer buffer;
    if (!VmaBufferFactory::createStagingBuffer(allocator_, size, buffer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
            "AsyncTransferManager: Failed to create staging buffer (size: %llu)",
            static_cast<unsigned long long>(size));
        return {};
    }
    return buffer;
}

void AsyncTransferManager::releaseStagingBuffer(VmaBuffer buffer) {
    if (!buffer) return;

    std::lock_guard<std::mutex> lock(stagingMutex_);
    if (stagingBufferPool_.size() < MAX_STAGING_POOL_SIZE) {
        stagingBufferPool_.push_back(std::move(buffer));
    }
    // Otherwise buffer is destroyed when going out of scope
}

TransferHandle AsyncTransferManager::submitBufferTransfer(
    const void* data, vk::DeviceSize size,
    vk::Buffer dstBuffer, vk::DeviceSize dstOffset,
    CompletionCallback onComplete)
{
    if (!ready() || !data || size == 0) {
        return {};
    }

    // Acquire staging buffer and copy data
    VmaBuffer staging = acquireStagingBuffer(size);
    if (!staging) {
        return {};
    }

    // Copy data to staging buffer (VMA buffers are created mapped)
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(allocator_, staging.getAllocation(), &allocInfo);
    memcpy(allocInfo.pMappedData, data, size);
    vmaFlushAllocation(allocator_, staging.getAllocation(), 0, size);

    // Allocate command buffer
    vk::CommandBuffer cmd = allocateTransferCommandBuffer();

    // Record transfer commands
    cmd.begin(vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    auto copyRegion = vk::BufferCopy{}
        .setSrcOffset(0)
        .setDstOffset(dstOffset)
        .setSize(size);
    cmd.copyBuffer(staging.get(), dstBuffer, copyRegion);

    cmd.end();

    // Assign the timeline value and submit under one lock so that, across
    // concurrent submitters, signalled values increase in submission order.
    uint64_t timelineValue;
    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(submitMutex_);
        timelineValue = nextTimelineValue_++;
        id = nextTransferId_++;

        uint64_t signalValue = timelineValue;
        auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
            .setSignalSemaphoreValueCount(1)
            .setPSignalSemaphoreValues(&signalValue);

        vk::Semaphore signalSemaphores[] = {**transferTimeline_};
        auto submitInfo = vk::SubmitInfo{}
            .setPNext(&timelineInfo)
            .setCommandBuffers(cmd)
            .setSignalSemaphores(signalSemaphores);

        transferQueue_.submit(submitInfo, nullptr);  // No fence, using timeline semaphore
    }

    // Track pending transfer
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.push_back(PendingTransfer{
            .id = id,
            .timelineValue = timelineValue,
            .cmdBuffer = cmd,
            .stagingBuffer = std::move(staging),
            .onComplete = std::move(onComplete),
            .needsOwnershipTransfer = false,
            .targetImage = nullptr,
            .finalLayout = vk::ImageLayout::eUndefined
        });
    }

    return TransferHandle{id};
}

TransferHandle AsyncTransferManager::submitImageTransfer(
    const void* data, vk::DeviceSize size,
    vk::Image dstImage, vk::Extent3D extent,
    vk::ImageLayout finalLayout,
    uint32_t mipLevels,
    uint32_t layerCount,
    CompletionCallback onComplete)
{
    if (!ready() || !data || size == 0) {
        return {};
    }

    // Acquire staging buffer and copy data
    VmaBuffer staging = acquireStagingBuffer(size);
    if (!staging) {
        return {};
    }

    // Copy data to staging buffer
    VmaAllocationInfo allocInfo;
    vmaGetAllocationInfo(allocator_, staging.getAllocation(), &allocInfo);
    memcpy(allocInfo.pMappedData, data, size);
    vmaFlushAllocation(allocator_, staging.getAllocation(), 0, size);

    // Allocate command buffer
    vk::CommandBuffer cmd = allocateTransferCommandBuffer();

    // Record transfer commands
    cmd.begin(vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    // Transition image to transfer destination layout
    auto barrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eUndefined)
        .setNewLayout(vk::ImageLayout::eTransferDstOptimal)
        .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
        .setImage(dstImage)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(layerCount))
        .setSrcAccessMask(vk::AccessFlagBits::eNone)
        .setDstAccessMask(vk::AccessFlagBits::eTransferWrite);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTopOfPipe,
        vk::PipelineStageFlagBits::eTransfer,
        {}, {}, {}, barrier);

    // Copy buffer to image
    auto region = vk::BufferImageCopy{}
        .setBufferOffset(0)
        .setBufferRowLength(0)
        .setBufferImageHeight(0)
        .setImageSubresource(vk::ImageSubresourceLayers{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setMipLevel(0)
            .setBaseArrayLayer(0)
            .setLayerCount(layerCount))
        .setImageOffset({0, 0, 0})
        .setImageExtent(extent);

    cmd.copyBufferToImage(staging.get(), dstImage,
        vk::ImageLayout::eTransferDstOptimal, region);

    // Transition to final layout
    // If using dedicated transfer queue and final layout needs graphics access,
    // we need queue ownership transfer
    bool needsOwnershipTransfer = hasDedicatedTransfer_ &&
        (finalLayout == vk::ImageLayout::eShaderReadOnlyOptimal ||
         finalLayout == vk::ImageLayout::eGeneral);

    if (needsOwnershipTransfer) {
        // Release ownership from the transfer queue. The matching acquire is
        // recorded later on the graphics queue (see submitOwnershipAcquire) -
        // Vulkan performs no implicit acquire, so the resource is not usable by
        // the graphics queue until that acquire executes.
        auto releaseBarrier = vk::ImageMemoryBarrier{}
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(finalLayout)
            .setSrcQueueFamilyIndex(transferQueueFamily_)
            .setDstQueueFamilyIndex(graphicsQueueFamily_)
            .setImage(dstImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(layerCount))
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eNone);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eBottomOfPipe,
            {}, {}, {}, releaseBarrier);
    } else {
        // Same queue, just transition to final layout
        auto finalBarrier = vk::ImageMemoryBarrier{}
            .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
            .setNewLayout(finalLayout)
            .setSrcQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setDstQueueFamilyIndex(VK_QUEUE_FAMILY_IGNORED)
            .setImage(dstImage)
            .setSubresourceRange(vk::ImageSubresourceRange{}
                .setAspectMask(vk::ImageAspectFlagBits::eColor)
                .setBaseMipLevel(0)
                .setLevelCount(mipLevels)
                .setBaseArrayLayer(0)
                .setLayerCount(layerCount))
            .setSrcAccessMask(vk::AccessFlagBits::eTransferWrite)
            .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

        cmd.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eFragmentShader,
            {}, {}, {}, finalBarrier);
    }

    cmd.end();

    // Assign the timeline value and submit under one lock so that, across
    // concurrent submitters, signalled values increase in submission order.
    uint64_t timelineValue;
    uint64_t id;
    {
        std::lock_guard<std::mutex> lock(submitMutex_);
        timelineValue = nextTimelineValue_++;
        id = nextTransferId_++;

        uint64_t signalValue = timelineValue;
        auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
            .setSignalSemaphoreValueCount(1)
            .setPSignalSemaphoreValues(&signalValue);

        vk::Semaphore signalSemaphores[] = {**transferTimeline_};
        auto submitInfo = vk::SubmitInfo{}
            .setPNext(&timelineInfo)
            .setCommandBuffers(cmd)
            .setSignalSemaphores(signalSemaphores);

        transferQueue_.submit(submitInfo, nullptr);  // No fence, using timeline semaphore
    }

    // Track pending transfer
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        pendingTransfers_.push_back(PendingTransfer{
            .id = id,
            .timelineValue = timelineValue,
            .cmdBuffer = cmd,
            .stagingBuffer = std::move(staging),
            .onComplete = std::move(onComplete),
            .needsOwnershipTransfer = needsOwnershipTransfer,
            .targetImage = dstImage,
            .finalLayout = finalLayout,
            .mipLevels = mipLevels,
            .layerCount = layerCount
        });
    }

    return TransferHandle{id};
}

// Returns true once a pending transfer's GPU work has fully completed and the
// resource is owned by the queue that will consume it. For an ownership transfer
// that means the graphics-queue acquire fence has signalled, not merely that the
// transfer-queue copy finished. Caller must hold transferMutex_.
bool AsyncTransferManager::transferComplete(const PendingTransfer& t,
                                            uint64_t currentTimelineValue) {
    if (t.needsOwnershipTransfer) {
        return t.acquireSubmitted && t.acquireFence &&
               t.acquireFence->getStatus() == vk::Result::eSuccess;
    }
    return currentTimelineValue >= t.timelineValue;
}

bool AsyncTransferManager::isComplete(TransferHandle handle) const {
    if (!handle.isValid() || !transferTimeline_) return true;

    std::lock_guard<std::mutex> lock(transferMutex_);
    uint64_t currentValue = transferTimeline_->getCounterValue();
    for (const auto& transfer : pendingTransfers_) {
        if (transfer.id == handle.id) {
            return transferComplete(transfer, currentValue);
        }
    }
    return true; // Not found = already completed
}

void AsyncTransferManager::submitOwnershipAcquire(PendingTransfer& transfer) {
    // Record the queue-family ownership *acquire* on the graphics queue. The
    // old/new layouts must match the release barrier exactly; the layout
    // transition itself happens once, between the release and the acquire.
    vk::CommandBuffer cmd = allocateGraphicsCommandBuffer();

    cmd.begin(vk::CommandBufferBeginInfo{}
        .setFlags(vk::CommandBufferUsageFlagBits::eOneTimeSubmit));

    auto acquireBarrier = vk::ImageMemoryBarrier{}
        .setOldLayout(vk::ImageLayout::eTransferDstOptimal)
        .setNewLayout(transfer.finalLayout)
        .setSrcQueueFamilyIndex(transferQueueFamily_)
        .setDstQueueFamilyIndex(graphicsQueueFamily_)
        .setImage(transfer.targetImage)
        .setSubresourceRange(vk::ImageSubresourceRange{}
            .setAspectMask(vk::ImageAspectFlagBits::eColor)
            .setBaseMipLevel(0)
            .setLevelCount(transfer.mipLevels)
            .setBaseArrayLayer(0)
            .setLayerCount(transfer.layerCount))
        .setSrcAccessMask(vk::AccessFlagBits::eNone)  // memory made available by the release + semaphore
        .setDstAccessMask(vk::AccessFlagBits::eShaderRead);

    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eAllCommands,
        vk::PipelineStageFlagBits::eFragmentShader,
        {}, {}, {}, acquireBarrier);

    cmd.end();

    // The acquire must wait on the transfer-queue release: the timeline value
    // signalled by the copy/release submission provides that cross-queue
    // dependency (and makes the transfer writes visible to the graphics queue).
    uint64_t waitValue = transfer.timelineValue;
    auto timelineInfo = vk::TimelineSemaphoreSubmitInfo{}
        .setWaitSemaphoreValueCount(1)
        .setPWaitSemaphoreValues(&waitValue);

    vk::Semaphore waitSemaphores[] = {**transferTimeline_};
    vk::PipelineStageFlags waitStages[] = {vk::PipelineStageFlagBits::eAllCommands};

    vk::raii::Fence fence(context_->get().getRaiiDevice(), vk::FenceCreateInfo{});

    auto submitInfo = vk::SubmitInfo{}
        .setPNext(&timelineInfo)
        .setWaitSemaphores(waitSemaphores)
        .setWaitDstStageMask(waitStages)
        .setCommandBuffers(cmd);

    {
        GraphicsQueueLock::Guard lock(GraphicsQueueLock::mutex());
        graphicsQueue_.submit(submitInfo, *fence);
    }

    transfer.acquireCmdBuffer = cmd;
    transfer.acquireFence.emplace(std::move(fence));
    transfer.acquireSubmitted = true;
}

void AsyncTransferManager::wait(TransferHandle handle) {
    if (!handle.isValid() || !transferTimeline_) return;

    // First block until the transfer-queue (copy/release) work has completed.
    uint64_t waitValue = 0;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        for (const auto& transfer : pendingTransfers_) {
            if (transfer.id == handle.id) {
                waitValue = transfer.timelineValue;
                break;
            }
        }
    }

    if (waitValue > 0) {
        auto waitInfo = vk::SemaphoreWaitInfo{}
            .setSemaphores(**transferTimeline_)
            .setValues(waitValue);
        (void)device_.waitSemaphores(waitInfo, UINT64_MAX);
    }

    // processPendingTransfers() submits the ownership acquire (if any) and
    // reclaims the transfer once it is fully complete. For an ownership transfer
    // the acquire fence may still be in flight, so block on it and re-process.
    for (;;) {
        processPendingTransfers();

        vk::Fence acquireFence{};
        {
            std::lock_guard<std::mutex> lock(transferMutex_);
            bool stillPending = false;
            for (const auto& transfer : pendingTransfers_) {
                if (transfer.id == handle.id) {
                    stillPending = true;
                    if (transfer.acquireFence) acquireFence = **transfer.acquireFence;
                    break;
                }
            }
            if (!stillPending) break;
        }

        if (acquireFence) {
            (void)device_.waitForFences(acquireFence, VK_TRUE, UINT64_MAX);
        }
    }
}

void AsyncTransferManager::processPendingTransfers() {
    if (!ready()) return;

    std::vector<PendingTransfer> completed;

    {
        std::lock_guard<std::mutex> lock(transferMutex_);

        // Get current timeline value once (non-blocking)
        uint64_t currentValue = transferTimeline_->getCounterValue();

        auto it = pendingTransfers_.begin();
        while (it != pendingTransfers_.end()) {
            // Submit the graphics-queue ownership acquire as soon as the transfer
            // is seen; the acquire's semaphore wait orders it after the release.
            if (it->needsOwnershipTransfer && !it->acquireSubmitted) {
                submitOwnershipAcquire(*it);
            }

            if (transferComplete(*it, currentValue)) {
                completed.push_back(std::move(*it));
                it = pendingTransfers_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Process completed transfers
    for (auto& transfer : completed) {
        // Free command buffers (transfer copy/release, and graphics acquire if any)
        freeTransferCommandBuffer(transfer.cmdBuffer);
        if (transfer.acquireCmdBuffer) {
            freeGraphicsCommandBuffer(transfer.acquireCmdBuffer);
        }

        // Return staging buffer to pool
        releaseStagingBuffer(std::move(transfer.stagingBuffer));

        // Execute completion callback (resource is now owned by the graphics queue)
        if (transfer.onComplete) {
            transfer.onComplete();
        }
    }
}

void AsyncTransferManager::waitAll() {
    if (!ready()) return;

    // Find the highest timeline value we need to wait for
    uint64_t maxValue = 0;
    {
        std::lock_guard<std::mutex> lock(transferMutex_);
        for (const auto& transfer : pendingTransfers_) {
            maxValue = std::max(maxValue, transfer.timelineValue);
        }
    }

    // Wait for all transfer-queue (copy/release) work to complete
    if (maxValue > 0) {
        auto waitInfo = vk::SemaphoreWaitInfo{}
            .setSemaphores(**transferTimeline_)
            .setValues(maxValue);
        (void)device_.waitSemaphores(waitInfo, UINT64_MAX);
    }

    // Drain everything, including in-flight graphics-queue ownership acquires:
    // each pass submits any outstanding acquires and reclaims finished transfers.
    for (;;) {
        processPendingTransfers();

        std::vector<vk::Fence> acquireFences;
        {
            std::lock_guard<std::mutex> lock(transferMutex_);
            if (pendingTransfers_.empty()) break;
            for (const auto& transfer : pendingTransfers_) {
                if (transfer.acquireFence) {
                    acquireFences.push_back(**transfer.acquireFence);
                }
            }
        }

        if (!acquireFences.empty()) {
            (void)device_.waitForFences(acquireFences, VK_TRUE, UINT64_MAX);
        }
    }
}

size_t AsyncTransferManager::getPendingCount() const {
    std::lock_guard<std::mutex> lock(transferMutex_);
    return pendingTransfers_.size();
}
