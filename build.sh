#!/bin/bash
set -e

BUILD_DIR="build"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j$(nproc)

echo ""
echo "构建完成: $BUILD_DIR/TsEngine"
