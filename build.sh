#!/usr/bin/env bash
# build.sh — configure et compile le projet Solar System

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

echo "=== Configuration CMake ==="
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release

echo "=== Compilation ==="
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.logicalcpu)"

echo ""
echo "=== Lancement ==="
"$BUILD_DIR/SolarSystem"
