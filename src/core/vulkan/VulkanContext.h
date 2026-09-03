#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>
#include <VkBootstrap.h>
#include <SDL3/SDL.h>
#include <vector>
#include <memory>
#include <optional>
#include "PipelineCache.h"
#include "VmaImage.h"

// Owning handle for a VmaAllocator (destroyed with vmaDestroyAllocator).
struct VmaAllocatorDeleter {
    void operator()(VmaAllocator allocator) const noexcept {
        if (allocator) vmaDestroyAllocator(allocator);
    }
};
using UniqueVmaAllocator = std::unique_ptr<std::remove_pointer_t<VmaAllocator>, VmaAllocatorDeleter>;

/**
 * VulkanContext encapsulates core Vulkan setup:
 * - Instance creation
 * - Surface creation
 * - Physical device selection
 * - Logical device creation
 * - Queue retrieval
 * - VMA allocator setup
 * - Swapchain management
 *
 * Construction is staged (initInstance() before the window exists, then
 * initDevice()); teardown is the destructor, which waits for the device to go
 * idle and then lets the members destroy in reverse declaration order.
 */
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();

    // Non-copyable
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    /**
     * Two-phase initialization for early Vulkan startup.
     *
     * initInstance() can be called before window creation to start
     * Vulkan instance, validation layers, and dispatcher earlier.
     *
     * initDevice() completes initialization once a window is available.
     */
    bool initInstance();
    bool initDevice(SDL_Window* window);

    /**
     * Combined init for backwards compatibility.
     * Equivalent to initInstance() + initDevice(window).
     */
    bool init(SDL_Window* window);

    bool createSwapchain();
    bool recreateSwapchain();

    // Clear all swapchain images to black (call after recreateSwapchain to prevent ghost frames)
    void clearSwapchainImages();

    // Swapchain-dependent resource creation (render pass, depth buffer, framebuffers)
    bool createSwapchainResources();
    bool recreateSwapchainResources();

    // Command pool and buffers
    bool createCommandPoolAndBuffers(uint32_t frameCount);

    void waitIdle();

    // RAII access for vulkan-hpp raii types (preferred for new code)
    const vk::raii::Instance& getRaiiInstance() const { return *raiiInstance_; }
    const vk::raii::PhysicalDevice& getRaiiPhysicalDevice() const { return *raiiPhysicalDevice_; }
    const vk::raii::Device& getRaiiDevice() const { return *raiiDevice_; }

    // vulkan-hpp handle getters (implicit conversion to VkXxx when needed)
    vk::Instance getVkInstance() const { return instance; }
    vk::PhysicalDevice getVkPhysicalDevice() const { return physicalDevice; }
    vk::Device getVkDevice() const { return device; }
    vk::Queue getVkGraphicsQueue() const { return graphicsQueue; }
    vk::Queue getVkPresentQueue() const { return presentQueue; }
    vk::Queue getVkTransferQueue() const { return transferQueue_; }
    vk::SwapchainKHR getVkSwapchain() const { return swapchain_ ? **swapchain_ : vk::SwapchainKHR{}; }
    vk::Format getVkSwapchainImageFormat() const { return swapchainImageFormat; }
    vk::Extent2D getVkSwapchainExtent() const { return swapchainExtent; }

    uint32_t getGraphicsQueueFamily() const;
    uint32_t getPresentQueueFamily() const;
    uint32_t getTransferQueueFamily() const;
    bool hasDedicatedTransferQueue() const { return hasDedicatedTransfer_; }
    VmaAllocator getAllocator() const { return allocator_.get(); }
    vk::PipelineCache getPipelineCache() const { return pipelineCache_ ? pipelineCache_->getCache() : vk::PipelineCache{}; }
    SDL_Window* getWindow() const { return window; }

    const std::vector<vk::ImageView>& getSwapchainImageViews() const { return swapchainImageViews; }
    vk::Image getSwapchainImage(uint32_t index) const {
        return index < swapchainImages.size() ? swapchainImages[index] : vk::Image{};
    }
    uint32_t getSwapchainImageCount() const { return static_cast<uint32_t>(swapchainImages.size()); }
    uint32_t getWidth() const { return swapchainExtent.width; }
    uint32_t getHeight() const { return swapchainExtent.height; }

    // Swapchain-dependent resource getters
    vk::RenderPass getRenderPass() const { return renderPass_ ? **renderPass_ : vk::RenderPass{}; }
    const vk::raii::RenderPass& getRaiiRenderPass() const { return *renderPass_; }
    vk::ImageView getDepthImageView() const { return depthImageView_ ? **depthImageView_ : vk::ImageView{}; }
    vk::Sampler getDepthSampler() const { return depthSampler_ ? **depthSampler_ : vk::Sampler{}; }
    vk::Format getDepthFormat() const { return depthFormat_; }
    const std::vector<vk::raii::Framebuffer>& getFramebuffers() const { return framebuffers_; }
    std::vector<vk::raii::Framebuffer>& getFramebuffers() { return framebuffers_; }
    uint32_t getFramebufferCount() const { return static_cast<uint32_t>(framebuffers_.size()); }

    // Command pool/buffer getters
    vk::CommandPool getCommandPool() const { return commandPool_ ? **commandPool_ : vk::CommandPool{}; }
    const vk::raii::CommandPool& getRaiiCommandPool() const { return *commandPool_; }

    /**
     * Create a command pool for use by one async-init worker task. Command
     * pools are externally synchronized, so each concurrently-running init
     * task needs its own instead of sharing the main pool. Owned by the
     * context (systems store the handle for later on-demand uploads, so the
     * pool must outlive them). Call from the main thread before workers start.
     */
    vk::CommandPool createWorkerCommandPool();
    const std::vector<vk::CommandBuffer>& getCommandBuffers() const { return commandBuffers_; }
    vk::CommandBuffer getCommandBuffer(uint32_t frameIndex) const {
        return frameIndex < commandBuffers_.size() ? commandBuffers_[frameIndex] : vk::CommandBuffer{};
    }

    const vkb::Device& getVkbDevice() const { return vkbDevice; }

    // Check if validation layers are enabled (useful for diagnostics)
    bool hasValidationLayers() const { return debugMessenger_.has_value(); }

    // Check if instance phase is complete (for two-phase init)
    bool isInstanceReady() const { return instanceReady; }

    // Check if device phase is complete (device, surface, swapchain created)
    bool isDeviceReady() const { return static_cast<bool>(device); }

    // Check if timeline semaphores are supported (always true for Vulkan 1.2+)
    bool hasTimelineSemaphores() const { return hasTimelineSemaphores_; }

    // GPU-driven indirect rendering feature support (queried + enabled at device creation).
    // vkCmdDrawIndexedIndirectCount (variable draw count from a GPU buffer).
    bool hasDrawIndirectCount() const { return hasDrawIndirectCount_; }
    // vkCmdDrawIndexedIndirect with drawCount > 1 (multiple draws from one indirect buffer).
    bool hasMultiDrawIndirect() const { return hasMultiDrawIndirect_; }
    // firstInstance != 0 in (indexed) indirect draws -> required for per-object gl_InstanceIndex.
    bool hasDrawIndirectFirstInstance() const { return hasDrawIndirectFirstInstance_; }
    // Fragment shader SSBO writes/atomics -> required for virtual texture feedback.
    bool hasFragmentStoresAndAtomics() const { return hasFragmentStoresAndAtomics_; }

private:
    bool createInstance();
    bool createSurface();
    bool selectPhysicalDevice();
    bool createLogicalDevice();
    bool createAllocator();
    bool createPipelineCache();

    // Resize path: release the swapchain (and its image views) before
    // createSwapchain() builds the replacement. Idempotent.
    void destroySwapchain();

    // Internal helpers for swapchain resource creation
    bool createRenderPass();
    bool createDepthResources();
    bool createFramebuffers();
    bool recreateDepthResources();

    // Members are declared in creation order; destruction runs in reverse:
    // command pools -> framebuffers -> depth -> render pass -> swapchain views
    // -> swapchain -> pipeline cache -> allocator -> device -> surface
    // -> debug messenger -> instance.

    SDL_Window* window = nullptr;
    bool instanceReady = false;

    // vk-bootstrap builder results: plain handle bundles, they own nothing.
    vkb::Instance vkbInstance;
    vkb::PhysicalDevice vkbPhysicalDevice;
    vkb::Device vkbDevice;

    // Owning vulkan-hpp RAII wrappers (adopt the handles vk-bootstrap created)
    vk::raii::Context raiiContext_;
    std::unique_ptr<vk::raii::Instance> raiiInstance_;
    std::optional<vk::raii::DebugUtilsMessengerEXT> debugMessenger_;
    std::optional<vk::raii::SurfaceKHR> surface_;
    std::unique_ptr<vk::raii::PhysicalDevice> raiiPhysicalDevice_;
    std::unique_ptr<vk::raii::Device> raiiDevice_;

    // Non-owning cached copies of the handles above (plus queues)
    vk::Instance instance{};
    vk::SurfaceKHR surface{};
    vk::PhysicalDevice physicalDevice{};
    vk::Device device{};
    vk::Queue graphicsQueue{};
    vk::Queue presentQueue{};
    vk::Queue transferQueue_{};
    uint32_t transferQueueFamily_ = 0;
    bool hasDedicatedTransfer_ = false;
    bool hasTimelineSemaphores_ = false;
    bool hasDrawIndirectCount_ = false;
    bool hasMultiDrawIndirect_ = false;
    bool hasDrawIndirectFirstInstance_ = false;
    bool hasFragmentStoresAndAtomics_ = false;

    UniqueVmaAllocator allocator_;

    std::optional<PipelineCache> pipelineCache_;

    std::optional<vk::raii::SwapchainKHR> swapchain_;
    std::vector<vk::Image> swapchainImages;
    std::vector<vk::raii::ImageView> swapchainImageViewOwners_;
    std::vector<vk::ImageView> swapchainImageViews;  // non-owning view of the owners above
    vk::Format swapchainImageFormat = vk::Format::eUndefined;
    vk::Extent2D swapchainExtent{0, 0};

    // Swapchain-dependent resources (render pass, depth buffer, framebuffers)
    std::optional<vk::raii::RenderPass> renderPass_;
    VmaImage depthImage_;
    std::optional<vk::raii::ImageView> depthImageView_;
    std::optional<vk::raii::Sampler> depthSampler_;
    vk::Format depthFormat_ = vk::Format::eD32Sfloat;
    std::vector<vk::raii::Framebuffer> framebuffers_;

    // Command pool and buffers
    std::optional<vk::raii::CommandPool> commandPool_;
    std::vector<vk::raii::CommandPool> workerCommandPools_;
    std::vector<vk::CommandBuffer> commandBuffers_;
};
