#!/bin/bash
# Run the Vulkan game from claude build
# Handles both Linux and macOS builds

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build/claude"

# Check if build exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Claude build not found. Run ./build-claude.sh first."
    exit 1
fi

# Detect platform and run appropriate binary
if [[ "$OSTYPE" == "darwin"* ]]; then
    # macOS - run from app bundle
    APP_DIR="$BUILD_DIR/vulkan-game.app/Contents/MacOS"
    if [ -f "$APP_DIR/vulkan-game" ]; then
        # Set up Vulkan ICD path for MoltenVK
        export VK_ICD_FILENAMES="/opt/homebrew/etc/vulkan/icd.d/MoltenVK_icd.json:/usr/local/share/vulkan/icd.d/MoltenVK_icd.json"
        cd "$APP_DIR"
        ./vulkan-game "$@"
    else
        echo "macOS binary not found at $APP_DIR"
        exit 1
    fi
else
    # Linux - run directly from build directory
    if [ -f "$BUILD_DIR/vulkan-game" ]; then
        cd "$BUILD_DIR"
        ./vulkan-game "$@"
    else
        echo "Linux binary not found at $BUILD_DIR/vulkan-game"
        exit 1
    fi
fi
