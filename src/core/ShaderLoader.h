#pragma once

#include <vulkan/vulkan.h>
#include "VulkanRAII.h"
#include <vector>
#include <string>
#include <optional>

namespace ShaderLoader {

std::optional<std::vector<char>> readFile(const std::string& filename);

// Legacy API - returns raw handle that caller must destroy
std::optional<VkShaderModule> createShaderModule(VkDevice device, const std::vector<char>& code);
std::optional<VkShaderModule> loadShaderModule(VkDevice device, const std::string& path);

// RAII API - returns managed handle that auto-destroys
std::optional<ManagedShaderModule> createShaderModuleManaged(VkDevice device, const std::vector<char>& code);
std::optional<ManagedShaderModule> loadShaderModuleManaged(VkDevice device, const std::string& path);

}
