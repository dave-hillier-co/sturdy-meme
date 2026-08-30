#pragma once

#include <string>
#include <cstdint>

class VulkanContext;

namespace ScreenshotCapture {

// Copies the given swapchain image (expected in PresentSrcKHR layout, GPU idle)
// into host memory and writes it as a PNG. Blocks until the copy completes.
// Callers must ensure no frame is in flight (e.g. Renderer::waitIdle) first.
bool captureSwapchainImage(VulkanContext& context, uint32_t imageIndex,
                           const std::string& outputPath);

} // namespace ScreenshotCapture
