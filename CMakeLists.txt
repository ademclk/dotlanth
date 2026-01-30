cmake_minimum_required(VERSION 3.28)

project(dotlanth
    VERSION 0.1.0
    DESCRIPTION "AI-Powered Automation Platform"
    LANGUAGES CXX C
)

# Standard CMake modules for installation and packaging
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# C++26 requirement - use Clang 19+ or GCC 14+
# CMake may not recognize "26" as valid, so we fall back to 23 and add the flag manually
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Add C++26 flag for compilers that support it (C++ only, not C)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC 14+ supports -std=c++26
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++2c>)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang 19+ supports -std=c++26
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-std=c++2c>)
    # Use libc++ for better C++26 standard library support
    if(DOTVM_USE_LIBCXX)
        add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-stdlib=libc++>)
        # Apply link option only to C++ targets (CMake 3.18+)
        add_link_options($<$<LINK_LANGUAGE:CXX>:-stdlib=libc++>)
    endif()
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable position-independent code for all targets
# Required when static libraries are linked into shared libraries
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

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
option(DOTVM_USE_LIBCXX "Use libc++ instead of libstdc++ (Clang only)" OFF)

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

    # GCC 14 has false positives on certain inline patterns
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 14.0)
        list(APPEND DOTVM_WARNING_FLAGS
            -Wno-free-nonheap-object
            -Wno-tautological-compare
        )
    endif()

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
    # SEC-008: Cryptographic Operations
    src/dotvm/core/crypto/blake3.cpp
    src/dotvm/core/crypto/keccak.cpp
    src/dotvm/core/crypto/ed25519.cpp
    src/dotvm/core/crypto/aes256gcm.cpp
    src/dotvm/core/crypto/crypto_executor.cpp
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
    # Isolation boundaries (SEC-007)
    src/dotvm/core/security/isolation_manager.cpp
    # Resource limiter (SEC-004)
    src/dotvm/core/resource_limiter.cpp
    # Dependency graph
    src/dotvm/core/graph/dependency_graph.cpp
    # Policy engine (SEC-009)
    src/dotvm/core/policy/json_parser.cpp
    src/dotvm/core/policy/decision_tree.cpp
    src/dotvm/core/policy/policy_engine.cpp
    # DSL parser (DSL-001)
    src/dotvm/core/dsl/lexer.cpp
    src/dotvm/core/dsl/parser.cpp
    # DSL IR generator (DSL-002)
    src/dotvm/core/dsl/compiler/ir_builder.cpp
    src/dotvm/core/dsl/compiler/optimizer.cpp
    src/dotvm/core/dsl/compiler/lowerer.cpp
    src/dotvm/core/dsl/compiler/codegen.cpp
    src/dotvm/core/dsl/compiler/bytecode_emitter.cpp
    src/dotvm/core/dsl/compiler/dsl_compiler.cpp
    # DSL Standard Library (DSL-004)
    src/dotvm/core/dsl/stdlib/stdlib_registry.cpp
    # Syscall handler (DSL-004)
    src/dotvm/exec/syscall_handler.cpp
    # Assembly lexer/parser (TOOL-004)
    src/dotvm/core/asm/asm_lexer.cpp
    src/dotvm/core/asm/asm_parser.cpp
    # Disassembler (TOOL-008)
    src/dotvm/core/disassembler.cpp
    # Bytecode Inspector (TOOL-009)
    src/dotvm/core/inspector.cpp
    # State backend (STATE-001)
    src/dotvm/core/state/state_backend.cpp
    src/dotvm/core/state/in_memory_backend.cpp
    # Transaction manager (STATE-003)
    src/dotvm/core/state/transaction_manager.cpp
    # State execution context (STATE-004)
    src/dotvm/exec/state_execution_context.cpp
    # Database bridge (STATE-005)
    src/dotvm/exec/database_bridge.cpp
    # PostgreSQL backend config (STATE-006) - always compiled for config struct
    src/dotvm/core/state/postgres_backend.cpp
    # Write-Ahead Logging (STATE-007)
    src/dotvm/core/state/log_record.cpp
    src/dotvm/core/state/write_ahead_log.cpp
    src/dotvm/core/state/wal_backend.cpp
    # Merkle Patricia Trie (STATE-008)
    src/dotvm/core/state/mpt_types.cpp
    src/dotvm/core/state/rlp.cpp
    src/dotvm/core/state/mpt_node.cpp
    src/dotvm/core/state/merkle_patricia_trie.cpp
    src/dotvm/core/state/mpt_proof.cpp
    # Snapshot Manager (STATE-009)
    src/dotvm/core/state/snapshot_manager.cpp
    # Query Optimizer (STATE-010)
    src/dotvm/core/state/query_ast.cpp
    src/dotvm/core/state/statistics.cpp
    src/dotvm/core/state/cost_model.cpp
    src/dotvm/core/state/execution_plan.cpp
    src/dotvm/core/state/query_optimizer.cpp
    # Object Type Schema (DEP-003)
    src/dotvm/core/schema/schema_registry.cpp
)

# PostgreSQL backend subsystem (STATE-006)
option(DOTVM_ENABLE_POSTGRESQL "Enable PostgreSQL state backend" OFF)

if(DOTVM_ENABLE_POSTGRESQL)
    find_package(PostgreSQL REQUIRED)

    target_link_libraries(dotvm_core PUBLIC PostgreSQL::PostgreSQL)
    target_compile_definitions(dotvm_core PUBLIC DOTVM_POSTGRESQL_ENABLED=1)
    message(STATUS "PostgreSQL state backend enabled")
endif()

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

target_compile_options(dotvm_core PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

# Pass SIMD enable flag to source code for conditional compilation of intrinsics
if(DOTVM_ENABLE_SIMD)
    target_compile_definitions(dotvm_core PUBLIC DOTVM_SIMD_ENABLED=1)
endif()

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

# ============================================================================
# Alias Targets for Namespace Consistency
# ============================================================================
# These aliases provide the Dotvm:: namespace for use in export sets and
# allow consistent target naming in both build and install scenarios.

add_library(Dotvm::Core ALIAS dotvm_core)
if(DOTVM_BUILD_C_API)
    add_library(Dotvm::CApi ALIAS dotvm_c)
endif()

# ============================================================================
# CLI Library (DSL-003)
# ============================================================================

option(DOTVM_BUILD_CLI "Build the CLI compiler tool (dotdsl)" ON)

if(DOTVM_BUILD_CLI)
    # Fetch CLI11
    include(FetchContent)
    FetchContent_Declare(
        cli11
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.6.1
    )
    FetchContent_MakeAvailable(cli11)

    # CLI library
    add_library(dotvm_cli STATIC
        src/dotvm/cli/terminal.cpp
        src/dotvm/cli/cli_app.cpp
        src/dotvm/cli/file_resolver.cpp
        src/dotvm/cli/formatter.cpp
        src/dotvm/cli/watcher.cpp
        src/dotvm/cli/commands/compile_command.cpp
        src/dotvm/cli/commands/check_command.cpp
        src/dotvm/cli/commands/format_command.cpp
        src/dotvm/cli/commands/watch_command.cpp
        # VM CLI Executor (TOOL-005)
        src/dotvm/cli/vm_cli_app.cpp
        src/dotvm/cli/commands/vm_run_command.cpp
        src/dotvm/cli/commands/vm_validate_command.cpp
        src/dotvm/cli/commands/vm_info_command.cpp
        # Disassembler CLI (TOOL-008)
        src/dotvm/cli/dis_cli_app.cpp
        src/dotvm/cli/commands/dis_command.cpp
        # Bytecode Inspector CLI (TOOL-009)
        src/dotvm/cli/info_cli_app.cpp
        src/dotvm/cli/info_formatter.cpp
        src/dotvm/cli/commands/info_command.cpp
        # Benchmark Suite CLI (TOOL-010) - core files that don't need benchmark lib
        src/dotvm/cli/bench_cli_app.cpp
        src/dotvm/cli/bench_formatter.cpp
    )

    target_link_libraries(dotvm_cli PUBLIC
        dotvm_core
        CLI11::CLI11
    )

    target_include_directories(dotvm_cli PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_compile_options(dotvm_cli PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    # Disable CLI11's use of deprecated std::codecvt (removed in C++26)
    target_compile_definitions(dotvm_cli PRIVATE CLI11_HAS_CODECVT=0)

    if(DOTVM_SANITIZER_FLAGS)
        target_link_options(dotvm_cli PUBLIC ${DOTVM_SANITIZER_FLAGS})
    endif()
    if(DOTVM_COVERAGE_FLAGS)
        target_link_options(dotvm_cli PUBLIC ${DOTVM_COVERAGE_FLAGS})
    endif()

    add_library(Dotvm::Cli ALIAS dotvm_cli)

    # dotdsl CLI executable
    add_executable(dotdsl
        src/dotvm/cli/main.cpp
    )

    target_link_libraries(dotdsl PRIVATE dotvm_cli)
    target_compile_options(dotdsl PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    # dotvm CLI executor (TOOL-005)
    add_executable(dotvm
        src/dotvm/cli/vm_main.cpp
    )

    target_link_libraries(dotvm PRIVATE dotvm_cli)
    target_compile_options(dotvm PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    install(TARGETS dotvm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # dotdis disassembler CLI (TOOL-008)
    add_executable(dotdis
        src/dotvm/cli/dis_main.cpp
    )

    target_link_libraries(dotdis PRIVATE dotvm_cli)
    target_compile_options(dotdis PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    install(TARGETS dotdis RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

    # dotinfo bytecode inspector CLI (TOOL-009)
    add_executable(dotinfo
        src/dotvm/cli/info_main.cpp
    )

    target_link_libraries(dotinfo PRIVATE dotvm_cli)
    target_compile_options(dotinfo PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

    install(TARGETS dotinfo RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
        # SEC-008: Cryptographic Operations tests
        tests/dotvm/core/crypto/blake3_test.cpp
        tests/dotvm/core/crypto/keccak_test.cpp
        tests/dotvm/core/crypto/ed25519_test.cpp
        tests/dotvm/core/crypto/aes256gcm_test.cpp
        tests/dotvm/core/crypto/crypto_executor_test.cpp
        # Test utilities and mock tests
        tests/dotvm/test_utils/mock_test.cpp
        tests/dotvm/test_utils/property_testing_test.cpp
        # Capability system tests (SEC-001)
        tests/dotvm/core/capability_test.cpp
        # Permission model tests (SEC-002)
        tests/dotvm/core/permission_test.cpp
        # Security context tests (SEC-003)
        tests/dotvm/core/security/security_context_test.cpp
        # Opcode permission tests (SEC-005)
        tests/dotvm/core/security/opcode_permission_test.cpp
        # Isolation manager tests (SEC-007)
        tests/dotvm/core/security/isolation_manager_test.cpp
        # Audit logging tests (SEC-006)
        tests/dotvm/core/security/audit_event_test.cpp
        tests/dotvm/core/security/async_audit_logger_test.cpp
        # Resource limiter tests (SEC-004)
        tests/dotvm/core/resource_limiter_test.cpp
        # Policy engine tests (SEC-009)
        tests/dotvm/core/policy/json_parser_test.cpp
        tests/dotvm/core/policy/decision_tree_test.cpp
        tests/dotvm/core/policy/policy_engine_test.cpp
        # DSL parser tests (DSL-001)
        tests/dotvm/core/dsl/lexer_test.cpp
        tests/dotvm/core/dsl/parser_test.cpp
        tests/dotvm/core/dsl/integration_test.cpp
        tests/dotvm/core/dsl/benchmark_test.cpp
        # DSL IR generator tests (DSL-002)
        tests/dsl/compiler/ir_builder_test.cpp
        tests/dsl/compiler/optimizer_test.cpp
        tests/dsl/compiler/codegen_test.cpp
        tests/dsl/compiler/bytecode_emitter_test.cpp
        tests/dsl/compiler/integration_test.cpp
        # DSL stdlib tests (DSL-004)
        tests/dsl/stdlib/registry_test.cpp
        tests/dsl/stdlib/capability_test.cpp
        tests/dsl/stdlib/integration_test.cpp
        # Assembly lexer/parser tests (TOOL-004)
        tests/asm/asm_lexer_test.cpp
        tests/asm/asm_parser_test.cpp
        tests/asm/asm_integration_test.cpp
        # Disassembler tests (TOOL-008)
        tests/dotvm/core/disassembler_test.cpp
        # Bytecode Inspector tests (TOOL-009)
        tests/dotvm/core/inspector_test.cpp
        # Dependency graph tests
        tests/dotvm/core/graph/dependency_graph_test.cpp
        # State backend tests (STATE-001)
        tests/dotvm/core/state/state_backend_test.cpp
        # MVCC tests (STATE-002)
        tests/dotvm/core/state/mvcc_test.cpp
        # Transaction manager tests (STATE-003)
        tests/dotvm/core/state/transaction_manager_test.cpp
        # State opcodes tests (STATE-004)
        tests/dotvm/exec/state_opcodes_test.cpp
        # Database bridge tests (STATE-005)
        tests/dotvm/exec/database_bridge_test.cpp
        # PostgreSQL backend tests (STATE-006)
        tests/dotvm/core/state/postgres_backend_test.cpp
        # WAL tests (STATE-007)
        tests/dotvm/core/state/wal_error_test.cpp
        tests/dotvm/core/state/log_record_test.cpp
        tests/dotvm/core/state/write_ahead_log_test.cpp
        tests/dotvm/core/state/wal_backend_test.cpp
        tests/dotvm/core/state/wal_benchmark_test.cpp
        # MPT tests (STATE-008)
        tests/dotvm/core/state/mpt_types_test.cpp
        tests/dotvm/core/state/rlp_test.cpp
        tests/dotvm/core/state/mpt_node_test.cpp
        tests/dotvm/core/state/mpt_test.cpp
        tests/dotvm/core/state/mpt_proof_test.cpp
        tests/dotvm/core/state/mpt_benchmark_test.cpp
        # Error formatter tests (MAINT-002)
        tests/dotvm/core/error_formatter_test.cpp
        # Snapshot Manager tests (STATE-009)
        tests/dotvm/core/state/snapshot_manager_test.cpp
        tests/dotvm/core/state/snapshot_benchmark_test.cpp
        # Query Optimizer tests (STATE-010)
        tests/dotvm/core/state/query_ast_test.cpp
        tests/dotvm/core/state/statistics_test.cpp
        tests/dotvm/core/state/cost_model_test.cpp
        tests/dotvm/core/state/execution_plan_test.cpp
        tests/dotvm/core/state/query_optimizer_test.cpp
        tests/dotvm/core/state/query_optimizer_benchmark_test.cpp
        # Object Type Schema tests (DEP-003)
        tests/dotvm/core/schema/schema_error_test.cpp
        tests/dotvm/core/schema/property_type_test.cpp
        tests/dotvm/core/schema/validator_test.cpp
        tests/dotvm/core/schema/link_def_test.cpp
        tests/dotvm/core/schema/object_type_test.cpp
        tests/dotvm/core/schema/schema_registry_test.cpp
        tests/dotvm/core/schema/migration_test.cpp
        tests/dotvm/core/schema/supply_chain_test.cpp
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

    # CLI tests (DSL-003) - only if CLI is enabled
    if(DOTVM_BUILD_CLI)
        add_executable(dotvm_cli_tests
            tests/dotvm/cli/terminal_test.cpp
            tests/dotvm/cli/cli_test.cpp
            tests/dotvm/cli/file_resolver_test.cpp
            tests/dotvm/cli/formatter_test.cpp
            tests/dotvm/cli/watcher_test.cpp
            tests/dotvm/cli/integration_test.cpp
            # VM CLI Executor tests (TOOL-005)
            tests/dotvm/cli/vm_cli_test.cpp
            tests/dotvm/cli/vm_integration_test.cpp
            # Disassembler CLI tests (TOOL-008)
            tests/dotvm/cli/dis_cli_test.cpp
            tests/dotvm/cli/dis_integration_test.cpp
            # Benchmark CLI tests (TOOL-010)
            tests/dotvm/cli/bench_cli_test.cpp
            tests/dotvm/cli/bench_formatter_test.cpp
        )

        target_link_libraries(dotvm_cli_tests PRIVATE
            dotvm_cli
            GTest::gtest_main
        )

        target_compile_options(dotvm_cli_tests PRIVATE ${DOTVM_WARNING_FLAGS} ${DOTVM_SANITIZER_FLAGS} ${DOTVM_COVERAGE_FLAGS})

        gtest_discover_tests(dotvm_cli_tests)
    endif()

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

        # Set linker language based on sanitizer state.
        # When sanitizers are enabled, the linked shared library (libdotvm_c.so)
        # contains C++ UBSan symbols that require the C++ linker to resolve.
        if(DOTVM_ENABLE_SANITIZERS)
            set_target_properties(dotvm_c_compat_test PROPERTIES
                LINKER_LANGUAGE CXX
            )
        else()
            set_target_properties(dotvm_c_compat_test PROPERTIES
                LINKER_LANGUAGE C
            )
        endif()

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
        add_subdirectory(fuzz)
    endif()
endif()

# ============================================================================
# Installation and Packaging
# ============================================================================

# Determine JIT availability for package config (based on build-time detection)
if(DOTVM_ENABLE_JIT AND CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    set(DOTVM_JIT_AVAILABLE TRUE)
else()
    set(DOTVM_JIT_AVAILABLE FALSE)
endif()

# Install core library with export
install(TARGETS dotvm_core
    EXPORT DotvmTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            COMPONENT Runtime
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Runtime
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Install C API library (if enabled)
if(DOTVM_BUILD_C_API)
    install(TARGETS dotvm_c
        EXPORT DotvmTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                COMPONENT Runtime
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                COMPONENT Runtime
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

# Install main executable
install(TARGETS dotlanth
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            COMPONENT Tools
)

# Install headers
install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    COMPONENT Development
    FILES_MATCHING
        PATTERN "*.h"
        PATTERN "*.hpp"
)

# Export targets for use by downstream projects
install(EXPORT DotvmTargets
    FILE DotvmTargets.cmake
    NAMESPACE Dotvm::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dotvm
    COMPONENT Development
)

# Generate and install package config file
configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/DotvmConfig.cmake.in
    ${CMAKE_BINARY_DIR}/DotvmConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dotvm
)

# Generate version file for compatibility checking
write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/DotvmConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_BINARY_DIR}/DotvmConfig.cmake
    ${CMAKE_BINARY_DIR}/DotvmConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/Dotvm
    COMPONENT Development
)

# ============================================================================
# CPack Configuration
# ============================================================================

set(CPACK_PACKAGE_NAME "dotvm")
set(CPACK_PACKAGE_VENDOR "Dotlanth")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
set(CPACK_RESOURCE_FILE_LICENSE ${CMAKE_SOURCE_DIR}/LICENSE)
set(CPACK_PACKAGE_CONTACT "dotlanth@example.com")

# Component configuration
set(CPACK_COMPONENTS_ALL Runtime Development Tools)

set(CPACK_COMPONENT_RUNTIME_DISPLAY_NAME "Runtime Libraries")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION "Shared libraries required to run DotVM applications")
set(CPACK_COMPONENT_RUNTIME_REQUIRED ON)

set(CPACK_COMPONENT_DEVELOPMENT_DISPLAY_NAME "Development Files")
set(CPACK_COMPONENT_DEVELOPMENT_DESCRIPTION "Headers, static libraries, and CMake config files for development")
set(CPACK_COMPONENT_DEVELOPMENT_DEPENDS Runtime)

set(CPACK_COMPONENT_TOOLS_DISPLAY_NAME "Tools")
set(CPACK_COMPONENT_TOOLS_DESCRIPTION "DotVM command-line tools and executables")
set(CPACK_COMPONENT_TOOLS_DEPENDS Runtime)

# Component groups
set(CPACK_COMPONENT_RUNTIME_GROUP "Core")
set(CPACK_COMPONENT_DEVELOPMENT_GROUP "Development")
set(CPACK_COMPONENT_TOOLS_GROUP "Applications")

set(CPACK_COMPONENT_GROUP_CORE_DISPLAY_NAME "Core Components")
set(CPACK_COMPONENT_GROUP_DEVELOPMENT_DISPLAY_NAME "Development Components")
set(CPACK_COMPONENT_GROUP_APPLICATIONS_DISPLAY_NAME "Application Tools")

# Archive generators (cross-platform)
set(CPACK_GENERATOR TGZ ZIP)

# Source package configuration
set(CPACK_SOURCE_GENERATOR TGZ ZIP)
set(CPACK_SOURCE_IGNORE_FILES
    /\\.git/
    /build.*/
    /\\.cache/
    /\\.vscode/
    /\\.idea/
    \\.DS_Store
    CMakeFiles/
    CMakeCache.txt
)

include(CPack)

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

    # TOOL-010: Benchmark Suite CLI
    add_executable(dotvm_bench_cli
        benchmarks/vm_bench.cpp
        benchmarks/bench_runner.cpp
        src/dotvm/cli/bench_cli_app.cpp
        src/dotvm/cli/bench_formatter.cpp
        src/dotvm/cli/bench_main.cpp
        src/dotvm/cli/commands/bench_command.cpp
    )

    # Set output name to dotvm_bench (executable name differs from target)
    set_target_properties(dotvm_bench_cli PROPERTIES OUTPUT_NAME dotvm_bench)

    target_link_libraries(dotvm_bench_cli PRIVATE
        dotvm_core
        dotvm_cli
        CLI11::CLI11
        benchmark::benchmark
    )

    target_include_directories(dotvm_bench_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_compile_options(dotvm_bench_cli PRIVATE
        ${DOTVM_WARNING_FLAGS}
    )

    # Optimize for benchmarking
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(dotvm_bench_cli PRIVATE -O3 -DNDEBUG)
    endif()

    # Install benchmarks
    install(TARGETS arithmetic_bench dotvm_simd_benchmark dotvm_bigint_benchmark dotvm_crypto_benchmark dotvm_bench_cli
        RUNTIME DESTINATION bin
    )
endif()
