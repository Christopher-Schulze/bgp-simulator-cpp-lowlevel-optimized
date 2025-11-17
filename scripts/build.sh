#!/bin/bash
# 
# BGP Simulator Build Script
# Copyright (c) 2025 Christopher Schulze
#

set -e  # Exit immediately if a command exits with a non-zero status

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check dependencies
check_dependencies() {
    local deps=("cmake" "g++" "make")
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &> /dev/null; then
            print_error "$dep is not installed. Please install before building."
            exit 1
        fi
    done
    
    # Check for required libraries
    if ! pkg-config --exists libcurl; then
        print_warning "libcurl development headers may not be available"
    fi
    
    if ! pkg-config --exists bzip2; then
        print_warning "bzip2 development headers may not be available"
    fi
}

# Detect number of CPU cores
get_cpu_count() {
    if [[ "$OSTYPE" == "darwin"* ]]; then
        # macOS
        sysctl -n hw.ncpu
    else
        # Linux and others
        nproc 2>/dev/null || echo 4
    fi
}

# Main build function
main() {
    print_status "Starting BGP Simulator build process..."
    
    # Check dependencies
    check_dependencies
    
    # Create build directory if it doesn't exist
    if [ ! -d "build" ]; then
        print_status "Creating build directory..."
        mkdir -p build
    fi
    
    # Change to build directory
    cd build
    
    # Clean previous build artifacts
    print_status "Cleaning previous build artifacts..."
    rm -rf ./*
    
    # Configure with CMake
    print_status "Configuring with CMake..."
    cmake .. -DCMAKE_BUILD_TYPE=Release
    
    # Get CPU count for parallel build
    CPU_COUNT=$(get_cpu_count)
    print_status "Building with $CPU_COUNT parallel jobs..."
    
    # Build the project
    make -j$CPU_COUNT
    
    # Verify that the executable was created
    if [ -f "bgp_sim" ]; then
        print_status "Build completed successfully!"
        print_status "Executable: $(pwd)/bgp_sim"
        print_status "To run: ./bgp_sim data/anns.csv data/rov.csv > ribs.csv"
    else
        print_error "Build failed - executable not found"
        exit 1
    fi
}

# Execute main function
main "$@"