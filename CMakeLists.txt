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

# Add C++26 flag for compilers that support it (only for C++ files)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC 14+ supports -std=c++26
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++2c>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang 19+ supports -std=c++26
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++2c>)
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
option(DOTVM_ENABLE_JIT "Enable JIT compilation (x86-64 only)" ON)
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
    src/dotvm/core/executor.cpp
    src/dotvm/exec/execution_engine.cpp
    # SIMD subsystem
    src/dotvm/core/simd/cpu_features.cpp
    src/dotvm/core/simd/simd_alu.cpp
    # BigInt arbitrary precision integers
    src/dotvm/core/bigint/bigint.cpp
    # Crypto subsystem
    src/dotvm/core/crypto/sha256.cpp
    src/dotvm/core/crypto/aes.cpp
    # Capability system (SEC-001)
    src/dotvm/core/capabilities.cpp
    # Permission model (SEC-002)
    src/dotvm/core/permission.cpp
    # Security context (SEC-003)
    src/dotvm/core/security/security_context.cpp
    # Audit logging framework (SEC-006)
    src/dotvm/core/security/audit_logger.cpp
    src/dotvm/core/security/audit_serializer.cpp
    src/dotvm/core/security/async_audit_logger.cpp
    # Resource limiter (SEC-004)
    src/dotvm/core/resource_limiter.cpp
)

# JIT subsystem (EXEC-012)
if(DOTVM_ENABLE_JIT)
    # Check for x86-64 architecture
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(dotvm_core PRIVATE
            src/dotvm/jit/jit_code_buffer.cpp
            src/dotvm/jit/jit_profiler.cpp
            src/dotvm/jit/jit_stencil.cpp
            src/dotvm/jit/jit_cache.cpp
            src/dotvm/jit/jit_compiler.cpp
            src/dotvm/jit/jit_context.cpp
            src/dotvm/jit/jit_osr.cpp
            src/dotvm/core/vm_context_jit.cpp
        )
        target_compile_definitions(dotvm_core PUBLIC DOTVM_JIT_ENABLED=1)
        message(STATUS "JIT compilation enabled (x86-64 detected)")
    else()
        message(STATUS "JIT compilation disabled (x86-64 required, found: ${CMAKE_SYSTEM_PROCESSOR})")
    endif()
endif()

target_include_directories(dotvm_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Enable position-independent code for static lib to allow linking into shared lib
set_property(TARGET dotvm_core PROPERTY POSITION_INDEPENDENT_CODE ON)

target_compile_options(dotvm_core PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

# Suppress pedantic warning for computed-goto in execution engine
# Computed-goto is a GCC/Clang extension required for high-performance dispatch
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    set_source_files_properties(
        src/dotvm/exec/execution_engine.cpp
        PROPERTIES COMPILE_FLAGS "-Wno-pedantic"
    )
endif()
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
        tests/dotvm/core/concepts_test.cpp
        tests/dotvm/core/executor_test.cpp
        tests/dotvm/core/fp_executor_test.cpp
        tests/dotvm/exec/execution_engine_test.cpp
        tests/dotvm/exec/memory_load_store_test.cpp
        tests/dotvm/exec/call_stack_test.cpp
        tests/dotvm/exec/execution_limits_test.cpp
        tests/dotvm/exec/comparison_ops_test.cpp
        tests/dotvm/exec/debug_mode_test.cpp
        tests/dotvm/exec/exception_handling_test.cpp
        # SIMD subsystem tests
        tests/dotvm/core/simd/cpu_features_test.cpp
        tests/dotvm/core/simd/vector_types_test.cpp
        tests/dotvm/core/simd/vector_register_file_test.cpp
        tests/dotvm/core/simd/simd_alu_test.cpp
        # VmContext SIMD integration tests
        tests/dotvm/core/vm_context_simd_test.cpp
        # BigInt tests
        tests/dotvm/core/bigint/bigint_test.cpp
        # Crypto subsystem tests
        tests/dotvm/core/crypto/sha256_test.cpp
        tests/dotvm/core/crypto/aes_test.cpp
        # Test utilities and mock tests
        tests/dotvm/test_utils/mock_test.cpp
        # Capability system tests (SEC-001)
        tests/dotvm/core/capability_test.cpp
        # Permission model tests (SEC-002)
        tests/dotvm/core/permission_test.cpp
        # Security context tests (SEC-003)
        tests/dotvm/core/security/security_context_test.cpp
        # Opcode permission tests (SEC-005)
        tests/dotvm/core/security/opcode_permission_test.cpp
        # Audit logging tests (SEC-006)
        tests/dotvm/core/security/audit_event_test.cpp
        tests/dotvm/core/security/async_audit_logger_test.cpp
        # Resource limiter tests (SEC-004)
        tests/dotvm/core/resource_limiter_test.cpp
    )

    # JIT tests (EXEC-012)
    if(DOTVM_ENABLE_JIT AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        target_sources(dotvm_tests PRIVATE
            tests/dotvm/jit/jit_code_buffer_test.cpp
            tests/dotvm/jit/jit_profiler_test.cpp
            tests/dotvm/jit/jit_integration_test.cpp
        )
    endif()

    target_link_libraries(dotvm_tests PRIVATE
        dotvm_core
        GTest::gtest_main
    )

    target_include_directories(dotvm_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/tests/dotvm/test_utils
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

# ============================================================================
# Fuzzing Targets (Clang only, with LibFuzzer)
# ============================================================================

option(DOTVM_BUILD_FUZZERS "Build fuzzing targets (requires Clang with LibFuzzer)" OFF)

if(DOTVM_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(WARNING "Fuzzers require Clang compiler. Skipping fuzzer targets.")
    else()
        set(FUZZER_FLAGS -fsanitize=fuzzer,address -fno-omit-frame-pointer)

        # Bytecode parsing fuzzer
        add_executable(bytecode_fuzzer
            fuzz/bytecode_fuzzer.cpp
        )
        target_link_libraries(bytecode_fuzzer PRIVATE dotvm_core)
        target_compile_options(bytecode_fuzzer PRIVATE ${FUZZER_FLAGS})
        target_link_options(bytecode_fuzzer PRIVATE ${FUZZER_FLAGS})

        # C API fuzzer
        if(DOTVM_BUILD_C_API)
            add_executable(capi_fuzzer
                fuzz/capi_fuzzer.cpp
            )
            target_link_libraries(capi_fuzzer PRIVATE dotvm_c)
            target_compile_options(capi_fuzzer PRIVATE ${FUZZER_FLAGS})
            target_link_options(capi_fuzzer PRIVATE ${FUZZER_FLAGS})
        endif()

        message(STATUS "Fuzzer targets enabled. Build with: cmake --build . --target bytecode_fuzzer capi_fuzzer")
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

# ============================================================================
# Benchmarks
# ============================================================================

if(DOTVM_BUILD_BENCHMARKS)
    # Fetch Google Benchmark
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.8.3
    )

    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)

    # Arithmetic benchmark (uses Google Benchmark)
    add_executable(arithmetic_bench
        benchmarks/arithmetic_bench.cpp
    )

    target_link_libraries(arithmetic_bench PRIVATE
        dotvm_core
        benchmark::benchmark
        benchmark::benchmark_main
    )

    target_compile_options(arithmetic_bench PRIVATE ${DOTVM_WARNING_FLAGS})

    # Optimize for benchmarking
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(arithmetic_bench PRIVATE -O3 -DNDEBUG)
    endif()

    # Custom benchmark support library
    add_library(dotvm_bench STATIC
        src/dotvm/core/bench/benchmark_config.cpp
    )

    target_include_directories(dotvm_bench PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_compile_options(dotvm_bench PRIVATE ${DOTVM_WARNING_FLAGS})

    # SIMD benchmark
    add_executable(dotvm_simd_benchmark
        benchmarks/simd_benchmark.cpp
    )

    target_link_libraries(dotvm_simd_benchmark PRIVATE
        dotvm_core
        dotvm_bench
    )

    target_compile_options(dotvm_simd_benchmark PRIVATE ${DOTVM_WARNING_FLAGS})

    # BigInt benchmark
    add_executable(dotvm_bigint_benchmark
        benchmarks/bigint_benchmark.cpp
    )

    target_link_libraries(dotvm_bigint_benchmark PRIVATE
        dotvm_core
        dotvm_bench
    )

    target_compile_options(dotvm_bigint_benchmark PRIVATE ${DOTVM_WARNING_FLAGS})

    # Crypto benchmark
    add_executable(dotvm_crypto_benchmark
        benchmarks/crypto_benchmark.cpp
    )

    target_link_libraries(dotvm_crypto_benchmark PRIVATE
        dotvm_core
        dotvm_bench
    )

    target_compile_options(dotvm_crypto_benchmark PRIVATE ${DOTVM_WARNING_FLAGS})

    # Install benchmarks
    install(TARGETS arithmetic_bench dotvm_simd_benchmark dotvm_bigint_benchmark dotvm_crypto_benchmark
        RUNTIME DESTINATION bin
    )
endif()
