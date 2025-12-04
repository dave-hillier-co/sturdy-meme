#!/bin/bash
# Bootstrap script for Claude build environment
# Sets up vcpkg and ninja if not available

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
VCPKG_DIR="$SCRIPT_DIR/vcpkg"

# Check if ninja is available, install if not
if ! command -v ninja &> /dev/null; then
    echo "Ninja not found. Attempting to install..."
    if command -v apt-get &> /dev/null; then
        sudo apt-get update && sudo apt-get install -y ninja-build
    elif command -v yum &> /dev/null; then
        sudo yum install -y ninja-build
    elif command -v brew &> /dev/null; then
        brew install ninja
    else
        echo "Could not install ninja automatically. Please install it manually."
        exit 1
    fi
fi

# Check if vcpkg is already set up
if [ -z "$VCPKG_ROOT" ] || [ ! -f "$VCPKG_ROOT/vcpkg" ]; then
    echo "Setting up vcpkg..."

    # Clone vcpkg if not present
    if [ ! -d "$VCPKG_DIR" ]; then
        echo "Cloning vcpkg..."
        git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
    fi

    # Bootstrap vcpkg if not already done
    if [ ! -f "$VCPKG_DIR/vcpkg" ]; then
        echo "Bootstrapping vcpkg..."
        "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
    fi

    export VCPKG_ROOT="$VCPKG_DIR"
fi

echo "VCPKG_ROOT=$VCPKG_ROOT"
echo ""
echo "Setup complete. To build, run:"
echo "  export VCPKG_ROOT=\"$VCPKG_ROOT\""
echo "  cmake --preset claude && cmake --build build/claude"
echo ""
echo "Or use the build-claude.sh script which sets up the environment automatically."
