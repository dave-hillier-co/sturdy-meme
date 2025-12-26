#!/bin/bash
# Script to identify Vulkan RAII adoption opportunities
# Usage: ./scripts/find-raii-opportunities.sh [--summary | --detailed]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
SRC_DIR="$PROJECT_ROOT/src"

# Colors for output
RED='\033[0;31m'
YELLOW='\033[1;33m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

MODE="${1:---summary}"

echo -e "${BLUE}=== Vulkan RAII Adoption Opportunities ===${NC}"
echo ""

# Function to count and optionally show matches
search_pattern() {
    local desc="$1"
    local pattern="$2"
    local exclude="${3:-}"

    if [ -n "$exclude" ]; then
        count=$(grep -r --include="*.cpp" --include="*.h" -E "$pattern" "$SRC_DIR" 2>/dev/null | grep -v "$exclude" | wc -l)
    else
        count=$(grep -r --include="*.cpp" --include="*.h" -E "$pattern" "$SRC_DIR" 2>/dev/null | wc -l)
    fi

    if [ "$count" -gt 0 ]; then
        if [ "$count" -gt 10 ]; then
            echo -e "${RED}[$count]${NC} $desc"
        elif [ "$count" -gt 3 ]; then
            echo -e "${YELLOW}[$count]${NC} $desc"
        else
            echo -e "${GREEN}[$count]${NC} $desc"
        fi

        if [ "$MODE" == "--detailed" ]; then
            if [ -n "$exclude" ]; then
                grep -r --include="*.cpp" --include="*.h" -n -E "$pattern" "$SRC_DIR" 2>/dev/null | grep -v "$exclude" | sed 's/^/    /'
            else
                grep -r --include="*.cpp" --include="*.h" -n -E "$pattern" "$SRC_DIR" 2>/dev/null | sed 's/^/    /'
            fi
            echo ""
        fi
    fi
}

echo -e "${BLUE}--- Manual Destroy Calls (High Priority) ---${NC}"
search_pattern "vkDestroyShaderModule (no RAII wrapper exists)" "vkDestroyShaderModule"
search_pattern "vkDestroyImageView" "vkDestroyImageView"
search_pattern "vkDestroyFramebuffer" "vkDestroyFramebuffer"
search_pattern "vkDestroyPipeline" "vkDestroyPipeline[^L]"
search_pattern "vkDestroyPipelineLayout" "vkDestroyPipelineLayout"
search_pattern "vkDestroyRenderPass" "vkDestroyRenderPass"
search_pattern "vkDestroyDescriptorSetLayout" "vkDestroyDescriptorSetLayout"
search_pattern "vkDestroyDescriptorPool" "vkDestroyDescriptorPool"
search_pattern "vkDestroySampler" "vkDestroySampler"
search_pattern "vkDestroyFence" "vkDestroyFence"
search_pattern "vkDestroySemaphore" "vkDestroySemaphore"
search_pattern "vkDestroyCommandPool" "vkDestroyCommandPool"
search_pattern "vkDestroyBuffer" "vkDestroyBuffer"
search_pattern "vkDestroyImage" "vkDestroyImage[^V]"
echo ""

echo -e "${BLUE}--- VMA Manual Cleanup ---${NC}"
search_pattern "vmaDestroyBuffer" "vmaDestroyBuffer"
search_pattern "vmaDestroyImage" "vmaDestroyImage"
search_pattern "vmaDestroyAllocator" "vmaDestroyAllocator"
echo ""

echo -e "${BLUE}--- Core Resource Cleanup (Lower Priority) ---${NC}"
search_pattern "vkDestroyDevice" "vkDestroyDevice"
search_pattern "vkDestroyInstance" "vkDestroyInstance"
search_pattern "vkDestroySurfaceKHR" "vkDestroySurfaceKHR"
search_pattern "vkDestroySwapchainKHR" "vkDestroySwapchainKHR"
echo ""

echo -e "${BLUE}--- Manual destroy() Method Patterns ---${NC}"
search_pattern "void destroy(VkDevice" "void destroy\(VkDevice"
search_pattern "->destroy(" "->destroy\("
search_pattern ".destroy(device" "\.destroy\(device"
echo ""

echo -e "${BLUE}--- Raw Handle Declarations (potential RAII candidates) ---${NC}"
search_pattern "VkShaderModule (not in function params)" "VkShaderModule\s+\w+\s*[;=]"
search_pattern "VkImageView member/local" "VkImageView\s+\w+\s*[;=]" "VulkanRAII"
search_pattern "VkFramebuffer member/local" "VkFramebuffer\s+\w+\s*[;=]" "VulkanRAII"
search_pattern "VkPipeline member/local" "VkPipeline\s+\w+\s*[;=]" "VulkanRAII"
search_pattern "std::vector<VkImageView>" "std::vector<VkImageView>"
search_pattern "std::vector<VkFramebuffer>" "std::vector<VkFramebuffer>"
echo ""

echo -e "${BLUE}--- Existing RAII Usage (for reference) ---${NC}"
search_pattern "ManagedBuffer usage" "ManagedBuffer"
search_pattern "ManagedImage usage" "ManagedImage[^V]"
search_pattern "ManagedImageView usage" "ManagedImageView"
search_pattern "ManagedPipeline usage" "ManagedPipeline[^L]"
search_pattern "UniquePipeline usage" "UniquePipeline"
search_pattern "RAIIAdapter usage" "RAIIAdapter"
echo ""

echo -e "${BLUE}=== Summary ===${NC}"
echo "Run with --detailed to see file locations"
echo ""
echo "Priority for RAII adoption:"
echo "1. VkShaderModule - No wrapper exists, many manual destroys"
echo "2. Systems with mixed manual/RAII patterns"
echo "3. Classes with destroy() methods"
