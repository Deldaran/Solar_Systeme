#!/usr/bin/env bash
# ════════════════════════════════════════
#  StartMac.sh — Build & Lance Solar System (macOS)
# ════════════════════════════════════════
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"

# ── Vérification des dépendances ─────────
if ! command -v cmake &> /dev/null; then
    echo "[ERROR] cmake introuvable. Installe-le via : brew install cmake"
    exit 1
fi

if ! command -v git &> /dev/null; then
    echo "[ERROR] git introuvable. Installe les Xcode Command Line Tools : xcode-select --install"
    exit 1
fi

# ── Configuration ─────────────────────────
echo ""
echo "╔══════════════════════════════════════╗"
echo "║   Solar System — Build (macOS)       ║"
echo "╚══════════════════════════════════════╝"
echo ""
echo "[1/2] Configuration CMake..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$(uname -m)"

# ── Compilation ───────────────────────────
echo ""
echo "[2/2] Compilation ($(sysctl -n hw.logicalcpu) threads)..."
cmake --build "$BUILD_DIR" --config Release --parallel "$(sysctl -n hw.logicalcpu)"

# ── Lancement ─────────────────────────────
echo ""
echo "✅  Build terminé — lancement..."
echo ""
"$BUILD_DIR/SolarSystem"
