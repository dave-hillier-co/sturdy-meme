#!/bin/bash
# Build script for Claude environment (Linux only)
# Bootstraps vcpkg if needed and builds the project

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VCPKG_DIR="$SCRIPT_DIR/vcpkg"

# This script is for Linux environments without vcpkg pre-installed
# On macOS, use the standard cmake commands with your system vcpkg
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "Error: build-claude.sh is for Linux environments only."
    echo ""
    echo "On macOS, use the standard build commands:"
    echo "  cmake --preset debug && cmake --build build/debug"
    echo "  ./run-debug.sh"
    echo ""
    echo "Make sure VCPKG_ROOT is set to your vcpkg installation."
    exit 1
fi

# Run setup if vcpkg not present
if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
    echo "Running setup..."
    "$SCRIPT_DIR/setup-claude.sh"
fi

export VCPKG_ROOT="$VCPKG_DIR"
# Force vcpkg to use system cmake/ninja instead of downloading its own
export VCPKG_FORCE_SYSTEM_BINARIES=1

echo "Configuring with cmake --preset claude..."
cmake --preset claude

echo "Building..."
cmake --build build/claude

echo "Build complete!"
