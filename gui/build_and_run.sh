#!/bin/bash

# Configuration
QT_PATH="/opt/homebrew/opt/qt"

echo "Building Qt6 GUI for CosmicTrajectories..."
cd gui || exit 1
mkdir -p build
cd build

# Run CMake
echo "Running CMake..."
cmake .. -DCMAKE_PREFIX_PATH=$QT_PATH -DCMAKE_BUILD_TYPE=Release

if [ $? -ne 0 ]; then
    echo "CMake configuration failed!"
    exit 1
fi

# Build
echo "Compiling..."
make -j$(sysctl -n hw.ncpu)

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

echo "Build successful! Launching..."
echo ""

# Launch the GUI
./CosmicTrajectoryGUI
