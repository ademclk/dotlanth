# Integration test for find_package(Dotvm)
#
# This project is designed to be built AFTER installing Dotvm to verify
# that the CMake package config files work correctly.
#
# Usage:
#   # First, install Dotvm
#   cmake -B build -DCMAKE_INSTALL_PREFIX=/tmp/dotvm-install
#   cmake --build build
#   cmake --install build
#
#   # Then, build this test project
#   cd tests/cmake-integration
#   cmake -B build -DCMAKE_PREFIX_PATH=/tmp/dotvm-install
#   cmake --build build
#   ./build/dotvm_integration_test

cmake_minimum_required(VERSION 3.28)
project(dotvm_integration_test LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the installed Dotvm package
find_package(Dotvm 0.1 REQUIRED COMPONENTS Core)

# Verify package variables are set
message(STATUS "Dotvm_FOUND: ${Dotvm_FOUND}")
message(STATUS "Dotvm_VERSION: ${Dotvm_VERSION}")
message(STATUS "Dotvm_SIMD_ENABLED: ${Dotvm_SIMD_ENABLED}")
message(STATUS "Dotvm_JIT_ENABLED: ${Dotvm_JIT_ENABLED}")
message(STATUS "Dotvm_CAPI_AVAILABLE: ${Dotvm_CAPI_AVAILABLE}")

# Basic executable that links against Dotvm::Core
add_executable(dotvm_integration_test main.cpp)
target_link_libraries(dotvm_integration_test PRIVATE Dotvm::Core)

# Optionally test CApi component if available
if(Dotvm_CAPI_AVAILABLE)
    find_package(Dotvm COMPONENTS CApi)
    if(Dotvm_CApi_FOUND)
        message(STATUS "CApi component found, building C API test")
        add_executable(dotvm_capi_integration_test capi_test.c)
        target_link_libraries(dotvm_capi_integration_test PRIVATE Dotvm::CApi)
        set_target_properties(dotvm_capi_integration_test PROPERTIES
            LINKER_LANGUAGE C
        )
    endif()
endif()

# Test that feature flags match expected values
if(NOT DEFINED Dotvm_SIMD_ENABLED)
    message(FATAL_ERROR "Dotvm_SIMD_ENABLED should be defined")
endif()

if(NOT DEFINED Dotvm_JIT_ENABLED)
    message(FATAL_ERROR "Dotvm_JIT_ENABLED should be defined")
endif()

if(NOT DEFINED Dotvm_CAPI_AVAILABLE)
    message(FATAL_ERROR "Dotvm_CAPI_AVAILABLE should be defined")
endif()

message(STATUS "Integration test project configured successfully")
