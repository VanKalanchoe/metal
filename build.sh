#!/bin/bash

set -e

PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

echo "======================================"
echo " SDL3 + C++20 + Xcode"
echo "======================================"

command -v cmake >/dev/null 2>&1 || {
echo "Error: CMake is not installed."
exit 1
}

command -v xcodebuild >/dev/null 2>&1 || {
echo "Error: Xcode command line tools are not installed."
exit 1
}

echo ""
echo "Configuring with Xcode..."

cmake -S "$PROJECT_DIR" -B "$BUILD_DIR" -G Xcode 
-DCMAKE_CXX_STANDARD=20

echo ""
echo "Building..."

cmake --build "$BUILD_DIR" --config Debug

echo ""
echo "======================================"
echo " Build successful!"
echo "======================================"

echo ""
echo "Executable:"
find "$BUILD_DIR" -type f -path "*/Debug/SDL3Example" -print

echo ""
echo "Run with:"
echo "$BUILD_DIR/Debug/SDL3Example"
