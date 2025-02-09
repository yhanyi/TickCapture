cmake_minimum_required(VERSION 3.15)

# Set CMake policies
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

project(tick_capture VERSION 0.1.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add cmake modules path
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Options
option(TICK_CAPTURE_BUILD_BENCHMARKS "Build benchmarks" ON)
option(TICK_CAPTURE_USE_DPDK "Use DPDK for packet capture" OFF)

# Dependencies
find_package(Boost REQUIRED COMPONENTS system program_options)
find_package(TBB REQUIRED)
find_package(ZeroMQ REQUIRED)
find_package(fmt REQUIRED)

if(TICK_CAPTURE_USE_DPDK)
    find_package(DPDK REQUIRED)
endif()

# Add subdirectories
add_subdirectory(src)
if(TICK_CAPTURE_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
