#!/usr/bin/env bash
#
# Configure, build and launch the Qt6 GUI. Works from any directory, on macOS
# (Apple Silicon or Intel; Homebrew, MacPorts or the Qt online installer),
# Linux and Windows MSYS2. Dependency and SDK discovery live in CMakeLists.txt
# (gui/cmake/MacOS.cmake), so nothing here is machine-specific.
#
#   bash gui/build_and_run.sh                         build + launch
#   NO_RUN=1 bash gui/build_and_run.sh                build only
#   BUILD_DIR=/tmp/ctg bash gui/build_and_run.sh      other build directory
#   bash gui/build_and_run.sh -DCTG_WITH_THERMALFIST=OFF   extra CMake flags
#
set -euo pipefail

GUI_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${BUILD_DIR:-$GUI_DIR/build}"

if ! command -v cmake >/dev/null 2>&1; then
    echo "cmake not found. On macOS: brew install cmake qt gsl" >&2
    exit 1
fi

# A build directory copied from another machine or checkout (or left behind
# after moving the project) points at a different source tree, which CMake
# refuses to reuse: start that one afresh.
CACHE="$BUILD_DIR/CMakeCache.txt"
if [ -f "$CACHE" ]; then
    cached_src="$(sed -n 's/^CMAKE_HOME_DIRECTORY:INTERNAL=//p' "$CACHE")"
    if [ ! "$cached_src" -ef "$GUI_DIR" ]; then
        echo "Build directory was configured for $cached_src; reconfiguring."
        rm -rf "$CACHE" "$BUILD_DIR/CMakeFiles"
    fi
fi

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
elif command -v sysctl >/dev/null 2>&1; then
    JOBS="$(sysctl -n hw.ncpu)"
else
    JOBS=4
fi

echo "Configuring CosmicTrajectoryGUI in $BUILD_DIR ..."
cmake -S "$GUI_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release "$@"

echo "Compiling ($JOBS jobs) ..."
cmake --build "$BUILD_DIR" --parallel "$JOBS"

if [ "${NO_RUN:-0}" != "0" ]; then
    echo "Build successful: $BUILD_DIR/CosmicTrajectoryGUI"
    exit 0
fi

echo "Build successful! Launching..."
echo ""
cd "$BUILD_DIR"
./CosmicTrajectoryGUI
