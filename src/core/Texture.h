#pragma once

#include <vulkan/vulkan.hpp>
#include <vk_mem_alloc.h>
#include <string>
#include <memory>

class Texture {
public:
    // Passkey for controlled construction via make_unique
    struct ConstructToken { explicit ConstructToken() = default; };
    explicit Texture(ConstructToken) {}

    // Factory methods - return nullptr on failure
    static std::unique_ptr<Texture> loadFromFile(const std::string& path, VmaAllocator allocator, VkDevice device,
                                                  VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                                  bool useSRGB = true);
    static std::unique_ptr<Texture> loadFromFileWithMipmaps(const std::string& path, VmaAllocator allocator, VkDevice device,
                                                             VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                                             bool useSRGB = true, bool enableAnisotropy = true);
    static std::unique_ptr<Texture> createSolidColor(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                                      VmaAllocator allocator, VkDevice device,
                                                      VkCommandPool commandPool, VkQueue queue);


    ~Texture();

    // Move-only
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    VkImageView getImageView() const { return imageView; }
    VkSampler getSampler() const { return sampler; }

private:
    bool loadInternal(const std::string& path, VmaAllocator allocator, VkDevice device,
                      VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                      bool useSRGB);
    bool loadWithMipmapsInternal(const std::string& path, VmaAllocator allocator, VkDevice device,
                                  VkCommandPool commandPool, VkQueue queue, VkPhysicalDevice physicalDevice,
                                  bool useSRGB, bool enableAnisotropy);
    bool createSolidColorInternal(uint8_t r, uint8_t g, uint8_t b, uint8_t a,
                                   VmaAllocator allocator, VkDevice device,
                                   VkCommandPool commandPool, VkQueue queue);
    bool loadDDS(const std::string& path, VmaAllocator allocator, VkDevice device,
                 VkCommandPool commandPool, VkQueue queue, bool useSRGB);
    // Record-only upload helpers: append commands to a caller-owned command
    // buffer so a full texture upload (transition -> copy -> transition) shares a
    // single queue submission + fence wait, instead of a blocking GPU round-trip
    // per step. levelCount lets a single barrier cover all mip levels at once.
    static void recordTransition(vk::CommandBuffer cmd, VkImage image,
                                 VkImageLayout oldLayout, VkImageLayout newLayout,
                                 uint32_t levelCount = 1);
    static void recordCopyBufferToImage(vk::CommandBuffer cmd, VkBuffer buffer,
                                        VkImage image, uint32_t width, uint32_t height);

    // Stored for cleanup
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView imageView = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;

    int width = 0;
    int height = 0;
};
