# DotLanth Test Suite
# Meson build configuration for unit tests

# =============================================================================
# Test Utility Directories
# =============================================================================

test_utils_inc = include_directories('dotvm/test_utils')
replication_test_inc = include_directories('dotvm/core/state/replication')

# =============================================================================
# Main Test Suite
# =============================================================================

dotvm_test_sources = files(
    # Core VM tests
    'dotvm/core/value_test.cpp',
    'dotvm/core/register_file_test.cpp',
    'dotvm/core/instruction_test.cpp',
    'dotvm/core/bytecode_test.cpp',
    'dotvm/core/memory_test.cpp',
    'dotvm/core/arch_config_test.cpp',
    'dotvm/core/alu_test.cpp',
    'dotvm/core/arch_register_file_test.cpp',
    'dotvm/core/vm_context_test.cpp',
    'dotvm/core/interop_32_64_test.cpp',
    'dotvm/core/security_hardening_test.cpp',
    'dotvm/core/fuzz_edge_cases_test.cpp',
    'dotvm/core/concepts_test.cpp',
    'dotvm/core/executor_test.cpp',
    'dotvm/core/fp_executor_test.cpp',

    # Execution engine tests
    'dotvm/exec/execution_engine_test.cpp',
    'dotvm/exec/memory_load_store_test.cpp',
    'dotvm/exec/call_stack_test.cpp',
    'dotvm/exec/execution_limits_test.cpp',
    'dotvm/exec/comparison_ops_test.cpp',
    'dotvm/exec/debug_mode_test.cpp',
    'dotvm/exec/exception_handling_test.cpp',
    'dotvm/exec/simd_exec_test.cpp',

    # SIMD subsystem tests
    'dotvm/core/simd/cpu_features_test.cpp',
    'dotvm/core/simd/vector_types_test.cpp',
    'dotvm/core/simd/vector_register_file_test.cpp',
    'dotvm/core/simd/simd_alu_test.cpp',

    # VmContext SIMD integration tests
    'dotvm/core/vm_context_simd_test.cpp',

    # BigInt tests
    'dotvm/core/bigint/bigint_test.cpp',

    # Crypto subsystem tests
    'dotvm/core/crypto/sha256_test.cpp',
    'dotvm/core/crypto/aes_test.cpp',
    'dotvm/core/crypto/blake3_test.cpp',
    'dotvm/core/crypto/keccak_test.cpp',
    'dotvm/core/crypto/ed25519_test.cpp',
    'dotvm/core/crypto/aes256gcm_test.cpp',
    'dotvm/core/crypto/crypto_executor_test.cpp',

    # Test utilities and mock tests
    'dotvm/test_utils/mock_test.cpp',
    'dotvm/test_utils/property_testing_test.cpp',

    # Capability system tests (SEC-001)
    'dotvm/core/capability_test.cpp',

    # Permission model tests (SEC-002)
    'dotvm/core/permission_test.cpp',

    # Security context tests (SEC-003)
    'dotvm/core/security/security_context_test.cpp',

    # Opcode permission tests (SEC-005)
    'dotvm/core/security/opcode_permission_test.cpp',

    # Isolation manager tests (SEC-007)
    'dotvm/core/security/isolation_manager_test.cpp',

    # Secret management tests (SEC-008)
    'dotvm/core/security/secrets/secret_test.cpp',
    'dotvm/core/security/secrets/environment_provider_test.cpp',
    'dotvm/core/security/secrets/secret_manager_test.cpp',

    # Audit logging tests (SEC-006)
    'dotvm/core/security/audit_event_test.cpp',
    'dotvm/core/security/async_audit_logger_test.cpp',

    # Resource limiter tests (SEC-004)
    'dotvm/core/resource_limiter_test.cpp',

    # Policy engine tests (SEC-009)
    'dotvm/core/policy/json_parser_test.cpp',
    'dotvm/core/policy/decision_tree_test.cpp',
    'dotvm/core/policy/policy_engine_test.cpp',

    # DSL parser tests (DSL-001)
    'dotvm/core/dsl/lexer_test.cpp',
    'dotvm/core/dsl/parser_test.cpp',
    'dotvm/core/dsl/integration_test.cpp',
    'dotvm/core/dsl/benchmark_test.cpp',

    # DSL IR generator tests (DSL-002)
    'dsl/compiler/ir_builder_test.cpp',
    'dsl/compiler/optimizer_test.cpp',
    'dsl/compiler/codegen_test.cpp',
    'dsl/compiler/bytecode_emitter_test.cpp',
    'dsl/compiler/integration_test.cpp',

    # DSL stdlib tests (DSL-004)
    'dsl/stdlib/registry_test.cpp',
    'dsl/stdlib/capability_test.cpp',
    'dsl/stdlib/integration_test.cpp',

    # Assembly lexer/parser tests (TOOL-004)
    'asm/asm_lexer_test.cpp',
    'asm/asm_parser_test.cpp',
    'asm/asm_integration_test.cpp',

    # Disassembler tests (TOOL-008)
    'dotvm/core/disassembler_test.cpp',

    # Bytecode Inspector tests (TOOL-009)
    'dotvm/core/inspector_test.cpp',

    # Dependency graph tests
    'dotvm/core/graph/dependency_graph_test.cpp',

    # Dot scheduler tests (DEP-002)
    'dotvm/core/graph/dot_scheduler_test.cpp',

    # State backend tests (STATE-001)
    'dotvm/core/state/state_backend_test.cpp',

    # MVCC tests (STATE-002)
    'dotvm/core/state/mvcc_test.cpp',

    # Transaction manager tests (STATE-003)
    'dotvm/core/state/transaction_manager_test.cpp',

    # State opcodes tests (STATE-004)
    'dotvm/exec/state_opcodes_test.cpp',

    # Database bridge tests (STATE-005)
    'dotvm/exec/database_bridge_test.cpp',

    # PostgreSQL backend tests (STATE-006)
    'dotvm/core/state/postgres_backend_test.cpp',

    # WAL tests (STATE-007)
    'dotvm/core/state/wal_error_test.cpp',
    'dotvm/core/state/log_record_test.cpp',
    'dotvm/core/state/write_ahead_log_test.cpp',
    'dotvm/core/state/wal_backend_test.cpp',
    'dotvm/core/state/wal_benchmark_test.cpp',

    # MPT tests (STATE-008)
    'dotvm/core/state/mpt_types_test.cpp',
    'dotvm/core/state/rlp_test.cpp',
    'dotvm/core/state/mpt_node_test.cpp',
    'dotvm/core/state/mpt_test.cpp',
    'dotvm/core/state/mpt_proof_test.cpp',
    'dotvm/core/state/mpt_benchmark_test.cpp',

    # Error formatter tests (MAINT-002)
    'dotvm/core/error_formatter_test.cpp',

    # Snapshot Manager tests (STATE-009)
    'dotvm/core/state/snapshot_manager_test.cpp',
    'dotvm/core/state/snapshot_benchmark_test.cpp',

    # State Import/Export tests (STATE-010)
    'dotvm/core/state/state_import_export_test.cpp',

    # Query Optimizer tests (STATE-011)
    'dotvm/core/state/query_ast_test.cpp',
    'dotvm/core/state/statistics_test.cpp',
    'dotvm/core/state/cost_model_test.cpp',
    'dotvm/core/state/execution_plan_test.cpp',
    'dotvm/core/state/query_optimizer_test.cpp',
    'dotvm/core/state/query_optimizer_benchmark_test.cpp',

    # Object Type Schema tests (DEP-003)
    'dotvm/core/schema/schema_error_test.cpp',
    'dotvm/core/schema/property_type_test.cpp',
    'dotvm/core/schema/validator_test.cpp',
    'dotvm/core/schema/link_def_test.cpp',
    'dotvm/core/schema/object_type_test.cpp',
    'dotvm/core/schema/schema_registry_test.cpp',
    'dotvm/core/schema/migration_test.cpp',
    'dotvm/core/schema/supply_chain_test.cpp',

    # Relationship Model tests (DEP-004)
    'dotvm/core/link/object_id_test.cpp',
    'dotvm/core/link/link_error_test.cpp',
    'dotvm/core/link/link_key_encoder_test.cpp',
    'dotvm/core/link/link_manager_test.cpp',
    'dotvm/core/link/link_cascade_test.cpp',
    'dotvm/core/link/link_traversal_test.cpp',
    'dotvm/core/link/link_integration_test.cpp',

    # Standard Library Runtime tests (GROUP-C)
    'dotvm/core/string_pool_test.cpp',
    'dotvm/core/collections_test.cpp',
    'dotvm/core/io/sandbox_test.cpp',

    # Multi-Node State Replication tests (STATE-006)
    'dotvm/core/state/replication/message_serializer_test.cpp',
    'dotvm/core/state/replication/transport_test.cpp',
    'dotvm/core/state/replication/raft_state_test.cpp',
    'dotvm/core/state/replication/raft_log_test.cpp',
    'dotvm/core/state/replication/delta_publisher_test.cpp',
    'dotvm/core/state/replication/delta_subscriber_test.cpp',
    'dotvm/core/state/replication/snapshot_sender_test.cpp',
    'dotvm/core/state/replication/snapshot_receiver_test.cpp',
    'dotvm/core/state/replication/replication_manager_test.cpp',

    # Test cluster infrastructure (STATE-006)
    'dotvm/core/state/replication/test_cluster_test.cpp',

    # Fault injection test utility (STATE-006)
    'dotvm/core/state/replication/fault_injector_test.cpp',

    # Chaos/fault injection integration tests (STATE-006)
    'dotvm/core/state/replication/chaos_test.cpp',

    # Package Manager tests (PRD-007)
    'dotvm/pkg/version_test.cpp',
    'dotvm/pkg/version_constraint_test.cpp',
    'dotvm/pkg/lock_file_test.cpp',
    'dotvm/pkg/package_cache_test.cpp',
    'dotvm/pkg/package_registry_test.cpp',
    'dotvm/pkg/dependency_resolver_test.cpp',
    'dotvm/pkg/package_manager_test.cpp',
)

# JIT tests (x86-64 only)
if jit_available
    dotvm_test_sources += files(
        'dotvm/jit/jit_code_buffer_test.cpp',
        'dotvm/jit/jit_profiler_test.cpp',
        'dotvm/jit/jit_integration_test.cpp',
    )
endif

dotvm_tests = executable('dotvm_tests',
    dotvm_test_sources,
    include_directories: [inc_dirs, test_utils_inc, replication_test_inc],
    cpp_args: dotvm_warning_flags + dotvm_defines,
    link_with: [dotvm_core_lib],
    dependencies: [gtest_dep, gtest_main_dep],
)

test('dotvm_tests',
    dotvm_tests,
    timeout: 300,
    protocol: 'gtest',
)

# =============================================================================
# CLI Tests
# =============================================================================

if get_option('cli')
    dotvm_cli_test_sources = files(
        'dotvm/cli/terminal_test.cpp',
        'dotvm/cli/cli_test.cpp',
        'dotvm/cli/file_resolver_test.cpp',
        'dotvm/cli/formatter_test.cpp',
        'dotvm/cli/watcher_test.cpp',
        'dotvm/cli/integration_test.cpp',

        # VM CLI Executor tests (TOOL-005)
        'dotvm/cli/vm_cli_test.cpp',
        'dotvm/cli/vm_integration_test.cpp',

        # Disassembler CLI tests (TOOL-008)
        'dotvm/cli/dis_cli_test.cpp',
        'dotvm/cli/dis_integration_test.cpp',

        # Benchmark CLI tests (TOOL-010)
        'dotvm/cli/bench_cli_test.cpp',
        'dotvm/cli/bench_formatter_test.cpp',

        # Package Manager CLI tests (PRD-007)
        'dotvm/cli/pkg_cli_test.cpp',

        # Debug Client tests (TOOL-011)
        'dotvm/debugger/command_parser_test.cpp',
        'dotvm/debugger/debug_session_test.cpp',
        'dotvm/debugger/breakpoint_manager_test.cpp',
        'dotvm/debugger/watch_manager_test.cpp',
        'dotvm/debugger/debug_formatter_test.cpp',
        'dotvm/debugger/debug_client_test.cpp',

        # Documentation Generator tests (CLI-005)
        'dotvm/cli/doc/doc_comment_test.cpp',
        'dotvm/cli/doc/doc_extractor_test.cpp',

        # Bytecode Benchmark Runner tests (CLI-005 bench subcommand)
        'dotvm/cli/bench/benchmark_statistics_test.cpp',
        'dotvm/cli/bench/baseline_manager_test.cpp',
        'dotvm/cli/bench/flamegraph_collector_test.cpp',
        'dotvm/cli/vm_bench_command_test.cpp',
    )

    dotvm_cli_tests = executable('dotvm_cli_tests',
        dotvm_cli_test_sources,
        include_directories: [inc_dirs, test_utils_inc],
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_cli_lib, dotvm_core_lib],
        dependencies: [gtest_dep, gtest_main_dep, cli11_dep],
    )

    test('dotvm_cli_tests',
        dotvm_cli_tests,
        timeout: 300,
        protocol: 'gtest',
    )
endif

# =============================================================================
# C API Tests
# =============================================================================

if get_option('capi')
    # C++ unit tests for C API
    dotvm_c_tests = executable('dotvm_c_tests',
        'dotvm/dotvm_c_test.cpp',
        include_directories: inc_dirs,
        cpp_args: dotvm_warning_flags + dotvm_defines,
        link_with: [dotvm_c_lib],
        dependencies: [gtest_dep, gtest_main_dep],
    )

    test('dotvm_c_tests',
        dotvm_c_tests,
        timeout: 60,
        protocol: 'gtest',
    )

    # Pure C compilation/compatibility test
    dotvm_c_compat_test = executable('dotvm_c_compat_test',
        'dotvm/dotvm_c_test.c',
        include_directories: inc_dirs,
        link_with: [dotvm_c_lib],
    )

    test('dotvm_c_compat_test',
        dotvm_c_compat_test,
        timeout: 60,
    )
endif
