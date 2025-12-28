#pragma once

#include <vulkan/vulkan.hpp>
#include <vector>
#include <string>
#include <optional>

namespace ShaderLoader {

std::optional<std::vector<char>> readFile(const std::string& filename);
std::optional<vk::ShaderModule> createShaderModule(vk::Device device, const std::vector<char>& code);
std::optional<vk::ShaderModule> loadShaderModule(vk::Device device, const std::string& path);

// Convenience overloads for raw VkDevice (for gradual migration)
inline std::optional<VkShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code) {
    auto result = createShaderModule(vk::Device(device), code);
    if (result) return static_cast<VkShaderModule>(*result);
    return std::nullopt;
}
inline std::optional<VkShaderModule> loadShaderModule(VkDevice device, const std::string& path) {
    auto result = loadShaderModule(vk::Device(device), path);
    if (result) return static_cast<VkShaderModule>(*result);
    return std::nullopt;
}

}
