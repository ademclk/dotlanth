cmake_minimum_required(VERSION 3.28)

project(dotlanth
    VERSION 0.1.0
    DESCRIPTION "AI-Powered Automation Platform"
    LANGUAGES CXX C
)

# C++26 requirement - use Clang 19+ or GCC 14+
# CMake may not recognize "26" as valid, so we fall back to 23 and add the flag manually
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add C++26 flag for compilers that support it
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC 14+ supports -std=c++26
    add_compile_options(-std=c++2c)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang 19+ supports -std=c++26
    add_compile_options(-std=c++2c)
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Build type defaults to Release
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Options
option(DOTVM_BUILD_TESTS "Build unit tests" ON)
option(DOTVM_BUILD_BENCHMARKS "Build benchmarks" OFF)
option(DOTVM_BUILD_C_API "Build C API shared library for FFI" ON)
option(DOTVM_ENABLE_SIMD "Enable SIMD optimizations" ON)
option(DOTVM_ENABLE_SANITIZERS "Enable sanitizers in Debug builds" OFF)
option(DOTVM_ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)

# Define strict warning flags (applied only to our targets, not dependencies)
set(DOTVM_WARNING_FLAGS "")
set(DOTVM_SANITIZER_FLAGS "")
set(DOTVM_COVERAGE_FLAGS "")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set(DOTVM_WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -Wconversion
        -Wsign-conversion
        -Wno-unused-parameter
    )

    if(DOTVM_ENABLE_SIMD)
        list(APPEND DOTVM_WARNING_FLAGS -march=native)
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND DOTVM_ENABLE_SANITIZERS)
        set(DOTVM_SANITIZER_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
    endif()

    # Code coverage flags (GCC and Clang)
    if(DOTVM_ENABLE_COVERAGE)
        if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
            set(DOTVM_COVERAGE_FLAGS --coverage -fprofile-arcs -ftest-coverage)
        elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            set(DOTVM_COVERAGE_FLAGS -fprofile-instr-generate -fcoverage-mapping)
        endif()
    endif()

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND DOTVM_WARNING_FLAGS -O3 -DNDEBUG)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(DOTVM_WARNING_FLAGS
        /W4
        /WX
        /permissive-
        /Zc:__cplusplus
    )

    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        list(APPEND DOTVM_WARNING_FLAGS /O2)
    endif()
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# DotVM core library
add_library(dotvm_core STATIC
    src/dotvm/core/register_file.cpp
    src/dotvm/core/instruction.cpp
    src/dotvm/core/bytecode.cpp
    src/dotvm/core/memory.cpp
)

target_include_directories(dotvm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_options(dotvm_core PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})
if(DOTVM_SANITIZER_FLAGS)
    target_link_options(dotvm_core PUBLIC ${DOTVM_SANITIZER_FLAGS})
endif()
if(DOTVM_COVERAGE_FLAGS)
    target_link_options(dotvm_core PUBLIC ${DOTVM_COVERAGE_FLAGS})
endif()

# ============================================================================
# C API Shared Library
# ============================================================================

if(DOTVM_BUILD_C_API)
    add_library(dotvm_c SHARED
        src/dotvm/dotvm_c.cpp
    )

    target_link_libraries(dotvm_c PRIVATE dotvm_core)

    target_include_directories(dotvm_c PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_compile_options(dotvm_c PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    # Define DOTVM_C_EXPORTS for DLL export on Windows
    target_compile_definitions(dotvm_c PRIVATE DOTVM_C_EXPORTS)

    # Set library version
    set_target_properties(dotvm_c PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        C_VISIBILITY_PRESET default
        CXX_VISIBILITY_PRESET default
        VISIBILITY_INLINES_HIDDEN OFF
    )

    if(DOTVM_SANITIZER_FLAGS)
        target_link_options(dotvm_c PUBLIC ${DOTVM_SANITIZER_FLAGS})
    endif()
    if(DOTVM_COVERAGE_FLAGS)
        target_link_options(dotvm_c PUBLIC ${DOTVM_COVERAGE_FLAGS})
    endif()
endif()

# Main executable
add_executable(dotlanth
    src/main.cpp
)

target_link_libraries(dotlanth PRIVATE dotvm_core)
target_compile_options(dotlanth PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

# Testing
if(DOTVM_BUILD_TESTS)
    enable_testing()

    # Fetch GoogleTest
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0
    )

    # Disable strict warnings for GoogleTest (it has some sign-conversion issues)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(CMAKE_CXX_FLAGS_BACKUP "${CMAKE_CXX_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-sign-conversion")
    FetchContent_MakeAvailable(googletest)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS_BACKUP}")

    # Test executable
    add_executable(dotvm_tests
        tests/dotvm/core/value_test.cpp
        tests/dotvm/core/register_file_test.cpp
        tests/dotvm/core/instruction_test.cpp
        tests/dotvm/core/bytecode_test.cpp
        tests/dotvm/core/memory_test.cpp
        tests/dotvm/core/arch_config_test.cpp
        tests/dotvm/core/alu_test.cpp
        tests/dotvm/core/arch_register_file_test.cpp
        tests/dotvm/core/vm_context_test.cpp
        tests/dotvm/core/interop_32_64_test.cpp
        tests/dotvm/core/security_hardening_test.cpp
        tests/dotvm/core/fuzz_edge_cases_test.cpp
    )

    target_link_libraries(dotvm_tests PRIVATE
        dotvm_core
        GTest::gtest_main
    )

    target_compile_options(dotvm_tests PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    include(GoogleTest)
    gtest_discover_tests(dotvm_tests)

    # C API tests (only if C API is enabled)
    if(DOTVM_BUILD_C_API)
        # C++ unit tests for C API
        add_executable(dotvm_c_tests
            tests/dotvm/dotvm_c_test.cpp
        )

        target_link_libraries(dotvm_c_tests PRIVATE
            dotvm_c
            GTest::gtest_main
        )

        target_compile_options(dotvm_c_tests PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

        gtest_discover_tests(dotvm_c_tests)

        # Pure C compilation/compatibility test
        add_executable(dotvm_c_compat_test
            tests/dotvm/dotvm_c_test.c
        )

        # Force C compilation (not C++)
        set_target_properties(dotvm_c_compat_test PROPERTIES
            LINKER_LANGUAGE C
        )

        target_link_libraries(dotvm_c_compat_test PRIVATE dotvm_c)

        add_test(NAME dotvm_c_compat_test COMMAND dotvm_c_compat_test)
    endif()
endif()

# Installation
install(TARGETS dotvm_core dotlanth
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

if(DOTVM_BUILD_C_API)
    install(TARGETS dotvm_c
        ARCHIVE DESTINATION lib
        LIBRARY DESTINATION lib
        RUNTIME DESTINATION bin
    )
endif()

install(DIRECTORY include/
    DESTINATION include
)
