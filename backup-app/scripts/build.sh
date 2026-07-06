#!/bin/bash
#
# Build script for Data Backup Tool
# Usage: ./scripts/build.sh [debug|release]
#

set -e

BUILD_TYPE="${1:-release}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="${PROJECT_DIR}/build"

echo "========================================="
echo "Data Backup Tool - Build Script"
echo "========================================="
echo "Project dir: ${PROJECT_DIR}"
echo "Build type:  ${BUILD_TYPE}"
echo "Build dir:   ${BUILD_DIR}"
echo "========================================="

# Detect build type
if [ "${BUILD_TYPE}" = "debug" ]; then
    CMAKE_BUILD_TYPE="Debug"
else
    CMAKE_BUILD_TYPE="Release"
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Run CMake
echo ""
echo ">>> Running CMake..."
cmake "${PROJECT_DIR}" \
    -DCMAKE_BUILD_TYPE="${CMAKE_BUILD_TYPE}" \
    -DBUILD_TESTS=OFF

# Build
echo ""
echo ">>> Building..."
make -j$(nproc 2>/dev/null || echo 4)

echo ""
echo "========================================="
echo "Build complete!"
echo "Binary: ${BUILD_DIR}/src/backup-app"
echo "========================================="
echo ""
echo "To run: ${BUILD_DIR}/src/backup-app"
