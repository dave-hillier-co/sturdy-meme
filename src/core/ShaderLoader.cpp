#include "ShaderLoader.h"
#include <SDL3/SDL.h>
#include <fstream>

namespace ShaderLoader {

std::optional<std::vector<char>> readFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open()) {
        SDL_Log("Failed to open file: %s", filename.c_str());
        return std::nullopt;
    }

    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);

    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    return buffer;
}

std::optional<vk::ShaderModule> createShaderModule(vk::Device device, const std::vector<char>& code) {
    auto createInfo = vk::ShaderModuleCreateInfo{}
        .setCodeSize(code.size())
        .setPCode(reinterpret_cast<const uint32_t*>(code.data()));

    try {
        return device.createShaderModule(createInfo);
    } catch (const vk::SystemError& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create shader module: %s", e.what());
        return std::nullopt;
    }
}

std::optional<vk::ShaderModule> loadShaderModule(vk::Device device, const std::string& path) {
    auto code = readFile(path);
    if (!code) {
        return std::nullopt;
    }
    return createShaderModule(device, *code);
}

}
