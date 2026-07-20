#!/bin/bash
# Terrain preprocessing pipeline runner.
#
# The pipeline itself is defined once, in CMakeLists.txt, as the
# `terrain_preprocessing` umbrella target (terrain tiles + watershed ->
# biome -> roads/towns -> virtual-texture tiles). This script is a thin
# wrapper that configures (if needed) and builds that target, so there is
# exactly one definition of stage ordering and tool parameters.
#
# Requires the input heightmap at assets/terrain/isleofwight-0m-200m.png
# (not committed - see README.md "Terrain input data").

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build/claude"
PRESET="claude"
TARGET="terrain_preprocessing"
CLEAN=false

print_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Runs the CMake-defined terrain preprocessing pipeline"
    echo "(terrain tiles + watershed -> biome -> roads/towns -> VT tiles)."
    echo ""
    echo "Options:"
    echo "  --build-dir DIR   Build directory (default: build/claude)"
    echo "  --preset NAME     CMake preset to configure with (default: claude)"
    echo "  --preview         Build the fast 2D world preview instead"
    echo "                    (composites world_preview.png/svg, skips VT tiles)"
    echo "  --clean           Delete generated/terrain_data and generated/vt_tiles first"
    echo "  -h, --help        Show this help message"
}

while [[ $# -gt 0 ]]; do
    case $1 in
        --build-dir)
            BUILD_DIR="$2"
            shift 2
            ;;
        --preset)
            PRESET="$2"
            shift 2
            ;;
        --preview)
            TARGET="preview"
            shift
            ;;
        --clean)
            CLEAN=true
            shift
            ;;
        -h|--help)
            print_usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            print_usage
            exit 1
            ;;
    esac
done

HEIGHTMAP="${SCRIPT_DIR}/assets/terrain/isleofwight-0m-200m.png"
if [[ ! -f "$HEIGHTMAP" ]]; then
    echo "Error: input heightmap not found: $HEIGHTMAP"
    echo "It is not committed to the repository - see README.md 'Terrain input data'."
    exit 1
fi

if $CLEAN; then
    echo "Cleaning generated terrain data..."
    rm -rf "${SCRIPT_DIR}/generated/terrain_data" "${SCRIPT_DIR}/generated/vt_tiles"
fi

if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
    echo "Configuring with cmake --preset $PRESET..."
    cmake --preset "$PRESET"
fi

echo "Building target '$TARGET'..."
cmake --build "$BUILD_DIR" --target "$TARGET"

echo ""
echo "Preprocessing complete. Output:"
ls -lh "${SCRIPT_DIR}/generated/terrain_data" 2>/dev/null || true
