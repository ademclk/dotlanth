# DotLanth - AI-Powered Automation Platform
# Meson build system configuration
#
# Quick start (uses Clang+libc++ by default):
#   ./meson-setup build
#   meson compile -C build
#   meson test -C build
#
# Or explicitly with native file:
#   meson setup build --native-file cross/clang-libcxx.ini
#
# Sanitizer builds:
#   ./meson-setup build-asan --native-file cross/clang-asan.ini --buildtype=debug
#   ./meson-setup build-tsan --native-file cross/clang-tsan.ini --buildtype=debug -Djit=false

project('dotlanth',
    'cpp', 'c',
    version: '26.1.0-alpha',
    license: 'MIT',
    meson_version: '>=1.2.0',
    default_options: [
        'cpp_std=c++26',
        'c_std=c17',
        'warning_level=3',
        'werror=true',
        'b_ndebug=if-release',
        'b_pie=true',
        'default_library=static',
    ]
)

# =============================================================================
# Compiler Detection and Configuration
# =============================================================================

cpp = meson.get_compiler('cpp')
cc = meson.get_compiler('c')
host_system = host_machine.system()
host_cpu = host_machine.cpu_family()

# Recommend Clang+libc++ for best C++26 support and sanitizer compatibility
if cpp.get_id() != 'clang'
    warning('Using ' + cpp.get_id() + ' compiler. Clang+libc++ is recommended.')
    warning('Run: meson setup build --native-file cross/clang-libcxx.ini')
endif

# Strict warning flags for our code (not applied to dependencies)
dotvm_warning_flags = []
dotvm_link_flags = []

if cpp.get_id() == 'clang' or cpp.get_id() == 'gcc'
    dotvm_warning_flags += [
        '-Wall',
        '-Wextra',
        '-Wpedantic',
        '-Wconversion',
        '-Wsign-conversion',
        '-Wno-unused-parameter',
    ]

    # GCC 14+ specific workarounds for false positives
    if cpp.get_id() == 'gcc' and cpp.version().version_compare('>=14.0')
        dotvm_warning_flags += [
            '-Wno-free-nonheap-object',
            '-Wno-tautological-compare',
        ]
    endif

    # SIMD optimizations
    if get_option('simd')
        dotvm_warning_flags += ['-march=native']
    endif
elif cpp.get_id() == 'msvc'
    dotvm_warning_flags += [
        '/W4',
        '/permissive-',
        '/Zc:__cplusplus',
    ]
endif

# C++26 flag - Meson handles this via cpp_std, but we need to ensure
# the actual flag passed is correct for the compiler version
# GCC 14+ and Clang 18+ recognize -std=c++26 or -std=c++2c
# Meson 1.3+ knows cpp_std=c++26, so no manual override needed

# Position-independent code for all targets
add_project_arguments('-fPIC', language: ['c', 'cpp'])

# =============================================================================
# Dependencies
# =============================================================================

# GoogleTest (for unit tests)
if get_option('tests')
    gtest_dep = dependency('gtest', fallback: ['gtest', 'gtest_dep'], required: true)
    gtest_main_dep = dependency('gtest_main', fallback: ['gtest', 'gtest_main_dep'], required: true)
endif

# CLI11 (for command-line tools)
if get_option('cli')
    cli11_dep = dependency('cli11', fallback: ['cli11', 'CLI11_dep'], required: true)
endif

# Google Benchmark (for benchmarks)
if get_option('benchmarks')
    benchmark_dep = dependency('benchmark', fallback: ['benchmark', 'google_benchmark_dep'], required: true)
    benchmark_main_dep = dependency('benchmark-main', fallback: ['benchmark', 'google_benchmark_main_dep'], required: true)
endif

# PostgreSQL (optional)
if get_option('postgresql')
    postgresql_dep = dependency('libpq', required: true)
endif

# =============================================================================
# Feature Detection
# =============================================================================

# JIT is only available on x86-64
jit_available = get_option('jit') and host_cpu == 'x86_64'

if get_option('jit') and not jit_available
    message('JIT compilation disabled (x86-64 required, found: @0@)'.format(host_cpu))
elif jit_available
    message('JIT compilation enabled (x86-64 detected)')
endif

# =============================================================================
# Compile Definitions
# =============================================================================

dotvm_defines = []

if get_option('simd')
    dotvm_defines += ['-DDOTVM_SIMD_ENABLED=1']
endif

if jit_available
    dotvm_defines += ['-DDOTVM_JIT_ENABLED=1']
endif

if get_option('postgresql')
    dotvm_defines += ['-DDOTVM_POSTGRESQL_ENABLED=1']
endif

# Note: CLI11 2.4+ automatically detects C++26 and disables codecvt
# No need to define CLI11_HAS_CODECVT - it causes redefinition warnings

# =============================================================================
# Include Directories
# =============================================================================

inc_dirs = include_directories('include')

# =============================================================================
# Core Library Sources
# =============================================================================

dotvm_core_sources = files(
    # Core VM types
    'src/dotvm/core/register_file.cpp',
    'src/dotvm/core/instruction.cpp',
    'src/dotvm/core/bytecode.cpp',
    'src/dotvm/core/memory.cpp',
    'src/dotvm/core/executor.cpp',
    # Note: execution_engine.cpp is compiled separately with -Wno-pedantic
    # for computed-goto support. See execution_engine_lib below.

    # SIMD subsystem
    'src/dotvm/core/simd/cpu_features.cpp',
    'src/dotvm/core/simd/simd_alu.cpp',

    # BigInt arbitrary precision integers
    'src/dotvm/core/bigint/bigint.cpp',

    # Crypto subsystem
    'src/dotvm/core/crypto/sha256.cpp',
    'src/dotvm/core/crypto/aes.cpp',
    'src/dotvm/core/crypto/blake3.cpp',
    'src/dotvm/core/crypto/keccak.cpp',
    'src/dotvm/core/crypto/ed25519.cpp',
    'src/dotvm/core/crypto/aes256gcm.cpp',
    'src/dotvm/core/crypto/crypto_executor.cpp',

    # Capability system (SEC-001)
    'src/dotvm/core/capabilities.cpp',

    # Permission model (SEC-002)
    'src/dotvm/core/permission.cpp',

    # Security context (SEC-003)
    'src/dotvm/core/security/security_context.cpp',

    # Audit logging framework (SEC-006)
    'src/dotvm/core/security/audit_logger.cpp',
    'src/dotvm/core/security/audit_serializer.cpp',
    'src/dotvm/core/security/async_audit_logger.cpp',

    # Isolation boundaries (SEC-007)
    'src/dotvm/core/security/isolation_manager.cpp',

    # Secret management (SEC-008)
    'src/dotvm/core/security/secrets/secret.cpp',
    'src/dotvm/core/security/secrets/environment_provider.cpp',
    'src/dotvm/core/security/secrets/secret_manager.cpp',

    # Resource limiter (SEC-004)
    'src/dotvm/core/resource_limiter.cpp',

    # Dependency graph
    'src/dotvm/core/graph/dependency_graph.cpp',

    # Dot scheduler (DEP-002)
    'src/dotvm/core/graph/dot_scheduler.cpp',

    # Policy engine (SEC-009)
    'src/dotvm/core/policy/json_parser.cpp',
    'src/dotvm/core/policy/decision_tree.cpp',
    'src/dotvm/core/policy/policy_engine.cpp',

    # DSL parser (DSL-001)
    'src/dotvm/core/dsl/lexer.cpp',
    'src/dotvm/core/dsl/parser.cpp',

    # DSL IR generator (DSL-002)
    'src/dotvm/core/dsl/compiler/ir_builder.cpp',
    'src/dotvm/core/dsl/compiler/optimizer.cpp',
    'src/dotvm/core/dsl/compiler/lowerer.cpp',
    'src/dotvm/core/dsl/compiler/codegen.cpp',
    'src/dotvm/core/dsl/compiler/bytecode_emitter.cpp',
    'src/dotvm/core/dsl/compiler/dsl_compiler.cpp',

    # DSL Standard Library (DSL-004)
    'src/dotvm/core/dsl/stdlib/stdlib_registry.cpp',

    # Syscall handler (DSL-004)
    'src/dotvm/exec/syscall_handler.cpp',

    # Assembly lexer/parser (TOOL-004)
    'src/dotvm/core/asm/asm_lexer.cpp',
    'src/dotvm/core/asm/asm_parser.cpp',

    # Disassembler (TOOL-008)
    'src/dotvm/core/disassembler.cpp',

    # Bytecode Inspector (TOOL-009)
    'src/dotvm/core/inspector.cpp',

    # State backend (STATE-001)
    'src/dotvm/core/state/state_backend.cpp',
    'src/dotvm/core/state/in_memory_backend.cpp',

    # Transaction manager (STATE-003)
    'src/dotvm/core/state/transaction_manager.cpp',

    # State execution context (STATE-004)
    'src/dotvm/exec/state_execution_context.cpp',

    # Database bridge (STATE-005)
    'src/dotvm/exec/database_bridge.cpp',

    # PostgreSQL backend config (STATE-006) - always compiled for config struct
    'src/dotvm/core/state/postgres_backend.cpp',

    # Write-Ahead Logging (STATE-007)
    'src/dotvm/core/state/log_record.cpp',
    'src/dotvm/core/state/write_ahead_log.cpp',
    'src/dotvm/core/state/wal_backend.cpp',

    # Merkle Patricia Trie (STATE-008)
    'src/dotvm/core/state/mpt_types.cpp',
    'src/dotvm/core/state/rlp.cpp',
    'src/dotvm/core/state/mpt_node.cpp',
    'src/dotvm/core/state/merkle_patricia_trie.cpp',
    'src/dotvm/core/state/mpt_proof.cpp',

    # Snapshot Manager (STATE-009)
    'src/dotvm/core/state/snapshot_manager.cpp',

    # State Import/Export (STATE-010)
    'src/dotvm/core/state/state_import_export.cpp',

    # Query Optimizer (STATE-011)
    'src/dotvm/core/state/query_ast.cpp',
    'src/dotvm/core/state/statistics.cpp',
    'src/dotvm/core/state/cost_model.cpp',
    'src/dotvm/core/state/execution_plan.cpp',
    'src/dotvm/core/state/query_optimizer.cpp',

    # Object Type Schema (DEP-003)
    'src/dotvm/core/schema/schema_registry.cpp',

    # Relationship Model (DEP-004)
    'src/dotvm/core/link/object_id.cpp',
    'src/dotvm/core/link/link_key_encoder.cpp',
    'src/dotvm/core/link/link_manager.cpp',

    # Standard Library Runtime (GROUP-C)
    'src/dotvm/core/string_pool.cpp',
    'src/dotvm/core/io/filesystem_sandbox.cpp',

    # Multi-Node State Replication (STATE-006)
    'src/dotvm/core/state/replication/message_serializer.cpp',
    'src/dotvm/core/state/replication/transport.cpp',
    'src/dotvm/core/state/replication/raft_state.cpp',
    'src/dotvm/core/state/replication/raft_log.cpp',
    'src/dotvm/core/state/replication/delta_publisher.cpp',
    'src/dotvm/core/state/replication/delta_subscriber.cpp',
    'src/dotvm/core/state/replication/snapshot_sender.cpp',
    'src/dotvm/core/state/replication/snapshot_receiver.cpp',
    'src/dotvm/core/state/replication/replication_manager.cpp',

    # Package Manager (PRD-007)
    'src/dotvm/pkg/version.cpp',
    'src/dotvm/pkg/version_constraint.cpp',
    'src/dotvm/pkg/lock_file.cpp',
    'src/dotvm/pkg/package_cache.cpp',
    'src/dotvm/pkg/package_registry.cpp',
    'src/dotvm/pkg/package.cpp',
    'src/dotvm/pkg/dependency_resolver.cpp',
    'src/dotvm/pkg/package_manager.cpp',
)

# JIT sources (x86-64 only)
if jit_available
    dotvm_core_sources += files(
        'src/dotvm/jit/jit_code_buffer.cpp',
        'src/dotvm/jit/jit_profiler.cpp',
        'src/dotvm/jit/jit_stencil.cpp',
        'src/dotvm/jit/jit_cache.cpp',
        'src/dotvm/jit/jit_compiler.cpp',
        'src/dotvm/jit/jit_context.cpp',
        'src/dotvm/jit/jit_osr.cpp',
        'src/dotvm/core/vm_context_jit.cpp',
    )
endif

# Core library dependencies
dotvm_core_deps = []
if get_option('postgresql')
    dotvm_core_deps += [postgresql_dep]
endif

# =============================================================================
# Core Library Target
# =============================================================================

# Special handling for execution_engine.cpp (computed-goto extension)
# Meson doesn't support per-file compile flags directly, so we handle this
# by compiling it as a separate object and linking
execution_engine_flags = dotvm_warning_flags
if cpp.get_id() in ['clang', 'gcc']
    # Remove -Wpedantic for this file (computed-goto is a GCC/Clang extension)
    execution_engine_flags = []
    foreach flag : dotvm_warning_flags
        if flag != '-Wpedantic'
            execution_engine_flags += flag
        endif
    endforeach
    execution_engine_flags += '-Wno-pedantic'
endif

# Build execution_engine separately with relaxed warnings
execution_engine_lib = static_library('execution_engine',
    'src/dotvm/exec/execution_engine.cpp',
    include_directories: inc_dirs,
    cpp_args: execution_engine_flags + dotvm_defines,
    dependencies: dotvm_core_deps,
)

# Remove execution_engine.cpp from main sources (it's already included via files())
# and link the separate library instead
dotvm_core_lib = static_library('dotvm_core',
    dotvm_core_sources,
    include_directories: inc_dirs,
    cpp_args: dotvm_warning_flags + dotvm_defines,
    dependencies: dotvm_core_deps,
    link_with: [execution_engine_lib],
    install: true,
)

dotvm_core_dep = declare_dependency(
    link_with: [dotvm_core_lib],
    include_directories: inc_dirs,
    compile_args: dotvm_defines,
)

# =============================================================================
# C API Shared Library
# =============================================================================

if get_option('capi')
    dotvm_c_lib = shared_library('dotvm_c',
        'src/dotvm/dotvm_c.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines + ['-DDOTVM_C_EXPORTS'],
        link_with: [dotvm_core_lib],
        version: meson.project_version(),
        soversion: '0',
        install: true,
    )

    dotvm_c_dep = declare_dependency(
        link_with: [dotvm_c_lib],
        include_directories: inc_dirs,
    )
endif

# =============================================================================
# CLI Library
# =============================================================================

if get_option('cli')
    dotvm_cli_sources = files(
        'src/dotvm/cli/terminal.cpp',
        'src/dotvm/cli/cli_app.cpp',
        'src/dotvm/cli/file_resolver.cpp',
        'src/dotvm/cli/formatter.cpp',
        'src/dotvm/cli/watcher.cpp',
        'src/dotvm/cli/commands/compile_command.cpp',
        'src/dotvm/cli/commands/check_command.cpp',
        'src/dotvm/cli/commands/format_command.cpp',
        'src/dotvm/cli/commands/watch_command.cpp',

        # VM CLI Executor (TOOL-005)
        'src/dotvm/cli/vm_cli_app.cpp',
        'src/dotvm/cli/commands/vm_run_command.cpp',
        'src/dotvm/cli/commands/vm_validate_command.cpp',
        'src/dotvm/cli/commands/vm_info_command.cpp',

        # Disassembler CLI (TOOL-008)
        'src/dotvm/cli/dis_cli_app.cpp',
        'src/dotvm/cli/commands/dis_command.cpp',

        # Bytecode Inspector CLI (TOOL-009)
        'src/dotvm/cli/info_cli_app.cpp',
        'src/dotvm/cli/info_formatter.cpp',
        'src/dotvm/cli/commands/info_command.cpp',

        # Benchmark Suite CLI (TOOL-010)
        'src/dotvm/cli/bench_cli_app.cpp',
        'src/dotvm/cli/bench_formatter.cpp',

        # Package Manager CLI (PRD-007)
        'src/dotvm/cli/pkg_cli_app.cpp',
        'src/dotvm/cli/commands/pkg_install_command.cpp',
        'src/dotvm/cli/commands/pkg_uninstall_command.cpp',
        'src/dotvm/cli/commands/pkg_list_command.cpp',
        'src/dotvm/cli/commands/pkg_update_command.cpp',

        # Debug Client (TOOL-011)
        'src/dotvm/debugger/command_parser.cpp',
        'src/dotvm/debugger/debug_session.cpp',
        'src/dotvm/debugger/breakpoint_manager.cpp',
        'src/dotvm/debugger/watch_manager.cpp',
        'src/dotvm/debugger/debug_formatter.cpp',
        'src/dotvm/debugger/debug_client.cpp',
        'src/dotvm/cli/commands/vm_debug_command.cpp',

        # Documentation Generator (CLI-005)
        'src/dotvm/cli/doc/doc_comment.cpp',
        'src/dotvm/cli/doc/doc_extractor.cpp',
        'src/dotvm/cli/doc/doc_generator.cpp',
        'src/dotvm/cli/commands/doc_command.cpp',

        # Bytecode Benchmark Runner (CLI-005 bench subcommand)
        'src/dotvm/cli/bench/benchmark_statistics.cpp',
        'src/dotvm/cli/bench/baseline_manager.cpp',
        'src/dotvm/cli/bench/flamegraph_collector.cpp',
        'src/dotvm/cli/commands/vm_bench_command.cpp',
    )

    dotvm_cli_lib = static_library('dotvm_cli',
        dotvm_cli_sources,
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )

    dotvm_cli_dep = declare_dependency(
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        include_directories: inc_dirs,
        dependencies: [cli11_dep],
    )

    # CLI Executables
    executable('dotdsl',
        'src/dotvm/cli/main.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )

    executable('dotvm',
        'src/dotvm/cli/vm_main.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )

    executable('dotdis',
        'src/dotvm/cli/dis_main.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )

    executable('dotinfo',
        'src/dotvm/cli/info_main.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )

    executable('dotpkg',
        'src/dotvm/cli/pkg_main.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [cli11_dep],
        install: true,
    )
endif

# =============================================================================
# Main Executable
# =============================================================================

executable('dotlanth',
    'src/main.cpp',
    include_directories: inc_dirs,
    cpp_args: dotvm_warning_flags + dotvm_defines,
    link_with: [dotvm_core_lib],
    install: true,
)

# =============================================================================
# Tests
# =============================================================================

if get_option('tests')
    subdir('tests')
endif

# =============================================================================
# Fuzz Targets
# =============================================================================

if get_option('fuzzers')
    if cpp.get_id() != 'clang'
        warning('Fuzzers require Clang compiler. Skipping fuzzer targets.')
    else
        subdir('fuzz')
    endif
endif

# =============================================================================
# Benchmarks
# =============================================================================

if get_option('benchmarks')
    subdir('benchmarks')
endif

# =============================================================================
# Static Analysis Targets
# =============================================================================

# clang-tidy target
clang_tidy = find_program('clang-tidy', required: false)
if clang_tidy.found()
    run_target('clang-tidy',
        command: [
            find_program('scripts/run-clang-tidy.sh'),
            meson.current_source_dir(),
            meson.current_build_dir(),
        ]
    )
endif

# cppcheck target
cppcheck = find_program('cppcheck', required: false)
if cppcheck.found()
    run_target('cppcheck',
        command: [
            cppcheck,
            '--enable=all',
            '--error-exitcode=1',
            '--suppress=missingIncludeSystem',
            '--suppress=unmatchedSuppression',
            '-I', meson.current_source_dir() / 'include',
            meson.current_source_dir() / 'src',
        ]
    )
endif

# =============================================================================
# Installation
# =============================================================================

install_subdir('include/dotvm',
    install_dir: get_option('includedir'),
)

# Package configuration
pkg = import('pkgconfig')
pkg.generate(dotvm_core_lib,
    name: 'dotvm',
    description: 'DotLanth Virtual Machine Core Library',
    version: meson.project_version(),
)

if get_option('capi')
    pkg.generate(dotvm_c_lib,
        name: 'dotvm_c',
        description: 'DotLanth Virtual Machine C API',
        version: meson.project_version(),
    )
endif

# =============================================================================
# Summary
# =============================================================================

summary({
    'SIMD': get_option('simd'),
    'JIT': jit_available,
    'Tests': get_option('tests'),
    'Benchmarks': get_option('benchmarks'),
    'C API': get_option('capi'),
    'CLI': get_option('cli'),
    'Fuzzers': get_option('fuzzers'),
    'PostgreSQL': get_option('postgresql'),
}, section: 'Features')

summary({
    'Compiler': cpp.get_id() + ' ' + cpp.version(),
    'Host CPU': host_cpu,
    'Build type': get_option('buildtype'),
}, section: 'Build')
