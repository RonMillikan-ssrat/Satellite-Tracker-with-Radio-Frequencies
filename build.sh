#!/bin/bash

# Satellite Tracker Build Script

set -e

echo "=== Satellite Tracker Build Script ==="
echo

# Check for Qt
if ! command -v qmake6 &> /dev/null && ! command -v qmake &> /dev/null; then
    echo "ERROR: Qt 6 not found!"
    echo
    echo "Please install Qt 6:"
    echo "  Ubuntu/Debian: sudo apt-get install qt6-base-dev"
    echo "  Fedora: sudo dnf install qt6-qtbase-devel"
    echo "  macOS: brew install qt@6"
    echo
    exit 1
fi

# Create build directory
if [ -d "build" ]; then
    echo "Removing existing build directory..."
    rm -rf build
fi

echo "Creating build directory..."
mkdir build
cd build

# Run CMake
echo "Running CMake..."
if command -v qmake6 &> /dev/null; then
    # Qt 6 is available
    cmake ..
elif [ -d "/opt/homebrew/opt/qt@6" ]; then
    # macOS Homebrew Qt 6
    export PATH="/opt/homebrew/opt/qt@6/bin:$PATH"
    cmake ..
else
    cmake ..
fi

# Build
echo "Building..."
make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)

echo
echo "=== Build Complete ==="
echo
echo "To run the application:"
echo "  cd build"
echo "  ./SatelliteTracker"
echo
