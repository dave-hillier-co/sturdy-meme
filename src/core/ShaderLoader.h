#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vector>
#include <string>
#include <optional>
#include <string_view>

namespace ShaderLoader {

struct RaiiTag {
    explicit RaiiTag() = default;
};

// Scope-lifetime shader module for callers that hold a plain vk::Device (no
// vk::raii::Device). Prefer the vk::raii::Device overloads where a RAII device
// is available.
class ScopedShaderModule {
public:
    ScopedShaderModule() = default;
    ScopedShaderModule(vk::Device device, vk::ShaderModule module);
    ~ScopedShaderModule();

    ScopedShaderModule(const ScopedShaderModule&) = delete;
    ScopedShaderModule& operator=(const ScopedShaderModule&) = delete;
    ScopedShaderModule(ScopedShaderModule&& other) noexcept;
    ScopedShaderModule& operator=(ScopedShaderModule&& other) noexcept;

    vk::ShaderModule get() const { return module_; }
    explicit operator bool() const { return static_cast<bool>(module_); }

private:
    void reset();

    vk::Device device_{};
    vk::ShaderModule module_{};
};

std::optional<std::vector<char>> readFile(const std::string& filename);
std::optional<vk::ShaderModule> createShaderModule(vk::Device device, const std::vector<char>& code);
std::optional<vk::ShaderModule> loadShaderModule(vk::Device device, const std::string& path);
std::optional<vk::raii::ShaderModule> createShaderModule(const vk::raii::Device& device, const std::vector<char>& code);
std::optional<vk::raii::ShaderModule> loadShaderModule(const vk::raii::Device& device, const std::string& path);
std::optional<ScopedShaderModule> createShaderModule(vk::Device device, const std::vector<char>& code, RaiiTag);
std::optional<ScopedShaderModule> loadShaderModule(vk::Device device, std::string_view path, RaiiTag);

}
